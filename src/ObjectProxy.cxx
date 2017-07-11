// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "ObjectProxy.h"
#include "CPyCppyyHelpers.h"
#include "Utility.h"

// Standard
#include <algorithm>


//______________________________________________________________________________
//                          Python-side proxy objects
//                          =========================
//
// C++ objects are represented in Python by ObjectProxy's, which encapsulate
// them using either a pointer (normal), pointer-to-pointer (kIsReference set),
// or as an owned value (kIsValue set). Objects held as reference are never
// owned, otherwise the object is owned if kIsOwner is set.
//
// In addition to encapsulation, ObjectProxy offers pickling (using TBufferFile
// with a copy into a Python string); rudimentary comparison operators (based on
// pointer value and class comparisons); stubs for numeric operators; and a
// representation that prints the C++ pointer values, rather than the PyObject*
// ones as is the default.



////////////////////////////////////////////////////////////////////////////////
/// Destroy the held C++ object, if owned; does not deallocate the proxy.

void CPyCppyy::op_dealloc_nofree( ObjectProxy* pyobj ) {
   if ( pyobj->fFlags & ObjectProxy::kIsValue ) {
      if ( ! (pyobj->fFlags & ObjectProxy::kIsSmartPtr) ) {
         Cppyy::CallDestructor( pyobj->ObjectIsA(), pyobj->GetObject() );
         Cppyy::Deallocate( pyobj->ObjectIsA(), pyobj->GetObject() );
      } else {
         Cppyy::CallDestructor( pyobj->fSmartPtrType, pyobj->fSmartPtr );
         Cppyy::Deallocate( pyobj->fSmartPtrType, pyobj->fSmartPtr );
      }
   }
   else if ( pyobj->fObject && ( pyobj->fFlags & ObjectProxy::kIsOwner ) ) {
      if ( ! (pyobj->fFlags & ObjectProxy::kIsSmartPtr) ) {
         Cppyy::Destruct( pyobj->ObjectIsA(), pyobj->GetObject() );
      } else {
         Cppyy::Destruct( pyobj->fSmartPtrType, pyobj->fSmartPtr );
      }
   }
   pyobj->fObject = NULL;
}


////////////////////////////////////////////////////////////////////////////////

namespace CPyCppyy {
namespace {

//= CPyCppyy object proxy null-ness checking =================================
   PyObject* op_nonzero( ObjectProxy* self )
   {
   // Null of the proxy is determined by null-ness of the held C++ object.
      PyObject* result = self->GetObject() ? Py_True : Py_False;
      Py_INCREF( result );
      return result;
   }

//= CPyCppyy object explicit destruction =====================================
   PyObject* op_destruct( ObjectProxy* self )
   {
   // User access to force deletion of the object. Needed in case of a true
   // garbage collector (like in PyPy), to allow the user control over when
   // the C++ destructor is called. This method requires that the C++ object
   // is owned (no-op otherwise).
      op_dealloc_nofree( self );
      Py_INCREF( Py_None );
      return Py_None;
   }

//= CPyCppyy object dispatch support =========================================
   PyObject* op_dispatch( PyObject* self, PyObject* args, PyObject* /* kdws */ )
   {
   // User-side __dispatch__ method to allow selection of a specific overloaded
   // method. The actual selection is in the disp() method of MethodProxy.
      PyObject *mname = 0, *sigarg = 0;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!O!:__dispatch__" ),
              &CPyCppyy_PyUnicode_Type, &mname, &CPyCppyy_PyUnicode_Type, &sigarg ) )
         return 0;

   // get the named overload
      PyObject* pymeth = PyObject_GetAttr( self, mname );
      if ( ! pymeth )
         return 0;

   // get the 'disp' method to allow overload selection
      PyObject* pydisp = PyObject_GetAttrString( pymeth, const_cast<char*>( "disp" ) );
      if ( ! pydisp ) {
         Py_DECREF( pymeth );
         return 0;
      }

   // finally, call dispatch to get the specific overload
      PyObject* oload = PyObject_CallFunctionObjArgs( pydisp, sigarg, NULL );
      Py_DECREF( pydisp );
      Py_DECREF( pymeth );
      return oload;
   }

//= CPyCppyy smart pointer support ===========================================
  PyObject* op_get_smart_ptr( ObjectProxy* self )
  {
     if ( !( self->fFlags & ObjectProxy::kIsSmartPtr ) ) {
        Py_RETURN_NONE;
     }

     return (PyObject*)CPyCppyy::BindCppObject( self->fSmartPtr, self->fSmartPtrType );
  }

////////////////////////////////////////////////////////////////////////////////

   PyMethodDef op_methods[] = {
      { (char*)"__nonzero__",  (PyCFunction)op_nonzero,  METH_NOARGS, NULL },
      { (char*)"__bool__",     (PyCFunction)op_nonzero,  METH_NOARGS, NULL }, // for p3
      { (char*)"__destruct__", (PyCFunction)op_destruct, METH_NOARGS, NULL },
      { (char*)"__dispatch__", (PyCFunction)op_dispatch, METH_VARARGS, (char*)"dispatch to selected overload" },
      { (char*)"_get_smart_ptr", (PyCFunction)op_get_smart_ptr, METH_NOARGS, (char*)"get associated smart pointer, if any" },
      { (char*)NULL, NULL, 0, NULL }
   };


//= CPyCppyy object proxy construction/destruction ===========================
   ObjectProxy* op_new( PyTypeObject* subtype, PyObject*, PyObject* )
   {
   // Create a new object proxy (holder only).
      ObjectProxy* pyobj = (ObjectProxy*)subtype->tp_alloc( subtype, 0 );
      pyobj->fObject = NULL;
      pyobj->fFlags  = 0;

      return pyobj;
   }

////////////////////////////////////////////////////////////////////////////////
/// Remove (Python-side) memory held by the object proxy.

   void op_dealloc( ObjectProxy* pyobj )
   {
      op_dealloc_nofree( pyobj );
      Py_TYPE(pyobj)->tp_free( (PyObject*)pyobj );
   }

////////////////////////////////////////////////////////////////////////////////
/// Rich set of comparison objects; only equals and not-equals are defined.

   PyObject* op_richcompare( ObjectProxy* self, ObjectProxy* other, int op )
   {
      if ( op != Py_EQ && op != Py_NE ) {
         Py_INCREF( Py_NotImplemented );
         return Py_NotImplemented;
      }

      Bool_t bIsEq = false;

   // special case for None to compare True to a null-pointer
      if ( (PyObject*)other == Py_None && ! self->fObject )
         bIsEq = true;

   // type + held pointer value defines identity (will cover if other is not
   // actually an ObjectProxy, as ob_type will be unequal)
      else if ( Py_TYPE(self) == Py_TYPE(other) && self->GetObject() == other->GetObject() )
         bIsEq = true;

      if ( ( op == Py_EQ && bIsEq ) || ( op == Py_NE && ! bIsEq ) ) {
         Py_INCREF( Py_True );
         return Py_True;
      }

      Py_INCREF( Py_False );
      return Py_False;
   }

////////////////////////////////////////////////////////////////////////////////
/// Build a representation string of the object proxy that shows the address
/// of the C++ object that is held, as well as its type.

   PyObject* op_repr( ObjectProxy* pyobj )
   {
      Cppyy::TCppType_t klass = pyobj->ObjectIsA();
      std::string clName = klass ? Cppyy::GetFinalName( klass ) : "<unknown>";
      if ( pyobj->fFlags & ObjectProxy::kIsReference )
         clName.append( "*" );

      std::string smartPtrName;
      if ( pyobj->fFlags & ObjectProxy::kIsSmartPtr ) {
         Cppyy::TCppType_t smartPtrType = pyobj->fSmartPtrType;
         smartPtrName = smartPtrType ? Cppyy::GetFinalName( smartPtrType ) : "unknown smart pointer";
      }

   // need to prevent accidental derefs when just printing (usually unsafe)
      if ( ! PyObject_HasAttr( (PyObject*)pyobj, PyStrings::gDeref ) ) {
         PyObject* name = PyObject_CallMethod( (PyObject*)pyobj,
            const_cast< char* >( "GetName" ), const_cast< char* >( "" ) );

         if ( name ) {
            if ( CPyCppyy_PyUnicode_GET_SIZE( name ) != 0 ) {
               if ( pyobj->fFlags & ObjectProxy::kIsSmartPtr ) {
                  PyObject* repr = CPyCppyy_PyUnicode_FromFormat( "<cppyy.gbl.%s object (\"%s\") at %p held by %s at %p>",
                     clName.c_str(), CPyCppyy_PyUnicode_AsString( name ), pyobj->GetObject(), smartPtrName.c_str(), pyobj->fSmartPtr );
                  Py_DECREF( name );
                  return repr;
               } else {
                  PyObject* repr = CPyCppyy_PyUnicode_FromFormat( "<cppyy.gbl.%s object (\"%s\") at %p>",
                     clName.c_str(), CPyCppyy_PyUnicode_AsString( name ), pyobj->GetObject() );
                  Py_DECREF( name );
                  return repr;
               }
            }
            Py_DECREF( name );
         } else
            PyErr_Clear();
      }

   // get here if object has no method GetName() or name = ""
      if ( pyobj->fFlags & ObjectProxy::kIsSmartPtr ) {
         return CPyCppyy_PyUnicode_FromFormat( const_cast< char* >( "<cppyy.gbl.%s object at %p held by %s at %p>" ),
            clName.c_str(), pyobj->GetObject(), smartPtrName.c_str(), pyobj->fSmartPtr );
      } else {
         return CPyCppyy_PyUnicode_FromFormat( const_cast< char* >( "<cppyy.gbl.%s object at %p>" ),
                                             clName.c_str(), pyobj->GetObject() );
      }
   }


//= CPyCppyy type number stubs to allow dynamic overrides ====================
#define CPYCPPYY_STUB( name, op, pystring )                                     \
   PyObject* op_##name##_stub( PyObject* left, PyObject* right )              \
   {                                                                          \
      if ( ! ObjectProxy_Check( left ) ) {                                    \
         if ( ObjectProxy_Check( right ) ) {                                  \
            std::swap( left, right );                                         \
         } else {                                                             \
            Py_INCREF( Py_NotImplemented );                                   \
            return Py_NotImplemented;                                         \
         }                                                                    \
      }                                                                       \
   /* place holder to lazily install __name__ if a global overload is available */ \
      if ( ! Utility::AddBinaryOperator(                                      \
               left, right, #op, "__"#name"__", "__r"#name"__" ) ) {          \
         Py_INCREF( Py_NotImplemented );                                      \
         return Py_NotImplemented;                                            \
      }                                                                       \
                                                                              \
   /* redo the call, which will now go to the newly installed method */       \
      return PyObject_CallMethodObjArgs( left, pystring, right, NULL );       \
   }

CPYCPPYY_STUB( add, +, PyStrings::gAdd )
CPYCPPYY_STUB( sub, -, PyStrings::gSub )
CPYCPPYY_STUB( mul, *, PyStrings::gMul )
CPYCPPYY_STUB( div, /, PyStrings::gDiv )

////////////////////////////////////////////////////////////////////////////////

   PyNumberMethods op_as_number = {
      (binaryfunc)op_add_stub,        // nb_add
      (binaryfunc)op_sub_stub,        // nb_subtract
      (binaryfunc)op_mul_stub,        // nb_multiply
#if PY_VERSION_HEX < 0x03000000
      (binaryfunc)op_div_stub,        // nb_divide
#endif
      0,                              // nb_remainder
      0,                              // nb_divmod
      0,                              // nb_power
      0,                              // nb_negative
      0,                              // nb_positive
      0,                              // nb_absolute
      0,                              // tp_nonzero (nb_bool in p3)
      0,                              // nb_invert
      0,                              // nb_lshift
      0,                              // nb_rshift
      0,                              // nb_and
      0,                              // nb_xor
      0,                              // nb_or
#if PY_VERSION_HEX < 0x03000000
      0,                              // nb_coerce
#endif
      0,                              // nb_int
      0,                              // nb_long (nb_reserved in p3)
      0,                              // nb_float
#if PY_VERSION_HEX < 0x03000000
      0,                              // nb_oct
      0,                              // nb_hex
#endif
      0,                              // nb_inplace_add
      0,                              // nb_inplace_subtract
      0,                              // nb_inplace_multiply
#if PY_VERSION_HEX < 0x03000000
      0,                              // nb_inplace_divide
#endif
      0,                              // nb_inplace_remainder
      0,                              // nb_inplace_power
      0,                              // nb_inplace_lshift
      0,                              // nb_inplace_rshift
      0,                              // nb_inplace_and
      0,                              // nb_inplace_xor
      0                               // nb_inplace_or
#if PY_VERSION_HEX >= 0x02020000
      , 0                             // nb_floor_divide
#if PY_VERSION_HEX < 0x03000000
      , 0                             // nb_true_divide
#else
      , (binaryfunc)op_div_stub       // nb_true_divide
#endif
      , 0                             // nb_inplace_floor_divide
      , 0                             // nb_inplace_true_divide
#endif
#if PY_VERSION_HEX >= 0x02050000
      , 0                             // nb_index
#endif
#if PY_VERSION_HEX >= 0x03050000
      , 0                             // nb_matrix_multiply
      , 0                             // nb_inplace_matrix_multiply
#endif
   };

} // unnamed namespace


//= CPyCppyy object proxy type ===============================================
PyTypeObject ObjectProxy_Type = {
   PyVarObject_HEAD_INIT( &CPyCppyyType_Type, 0 )
   (char*)"cppyy.ObjectProxy",// tp_name
   sizeof(ObjectProxy),       // tp_basicsize
   0,                         // tp_itemsize
   (destructor)op_dealloc,    // tp_dealloc
   0,                         // tp_print
   0,                         // tp_getattr
   0,                         // tp_setattr
   0,                         // tp_compare
   (reprfunc)op_repr,         // tp_repr
   &op_as_number,             // tp_as_number
   0,                         // tp_as_sequence
   0,                         // tp_as_mapping
   PyBaseObject_Type.tp_hash, // tp_hash
   0,                         // tp_call
   0,                         // tp_str
   0,                         // tp_getattro
   0,                         // tp_setattro
   0,                         // tp_as_buffer
   Py_TPFLAGS_DEFAULT |
      Py_TPFLAGS_BASETYPE |
      Py_TPFLAGS_HAVE_GC |
      Py_TPFLAGS_CHECKTYPES,  // tp_flags
   (char*)"cppyy object proxy (internal)",       // tp_doc
   0,                         // tp_traverse
   0,                         // tp_clear
   (richcmpfunc)op_richcompare,                  // tp_richcompare
   0,                         // tp_weaklistoffset
   0,                         // tp_iter
   0,                         // tp_iternext
   op_methods,                // tp_methods
   0,                         // tp_members
   0,                         // tp_getset
   0,                         // tp_base
   0,                         // tp_dict
   0,                         // tp_descr_get
   0,                         // tp_descr_set
   0,                         // tp_dictoffset
   0,                         // tp_init
   0,                         // tp_alloc
   (newfunc)op_new,           // tp_new
   0,                         // tp_free
   0,                         // tp_is_gc
   0,                         // tp_bases
   0,                         // tp_mro
   0,                         // tp_cache
   0,                         // tp_subclasses
   0                          // tp_weaklist
#if PY_VERSION_HEX >= 0x02030000
   , 0                        // tp_del
#endif
#if PY_VERSION_HEX >= 0x02060000
   , 0                        // tp_version_tag
#endif
#if PY_VERSION_HEX >= 0x03040000
   , 0                        // tp_finalize
#endif
};

} // namespace CPyCppyy
