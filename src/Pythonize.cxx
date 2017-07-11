// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "Pythonize.h"
#include "ObjectProxy.h"
#include "MethodProxy.h"
#include "CPyCppyyHelpers.h"
#include "Utility.h"
#include "PyCallable.h"
#include "TPyBufferFactory.h"
#include "TFunctionHolder.h"
#include "Converters.h"
#include "Utility.h"

// Standard
#include <stdexcept>
#include <string>
#include <utility>

#include <stdio.h>
#include <string.h>     // only needed for Cling TMinuit workaround


//- data and local helpers ---------------------------------------------------
namespace CPyCppyy {
   extern PyObject* gThisModule;
}

namespace {

// for convenience
   using namespace CPyCppyy;

////////////////////////////////////////////////////////////////////////////////
/// prevents calls to Py_TYPE(pyclass)->tp_getattr, which is unnecessary for our
/// purposes here and could tickle problems w/ spurious lookups into ROOT meta

   Bool_t HasAttrDirect( PyObject* pyclass, PyObject* pyname, Bool_t mustBeCPyCppyy = kFALSE ) {
      PyObject* attr = PyType_Type.tp_getattro( pyclass, pyname );
      if ( attr != 0 && ( ! mustBeCPyCppyy || MethodProxy_Check( attr ) ) ) {
         Py_DECREF( attr );
         return kTRUE;
      }

      PyErr_Clear();
      return kFALSE;
   }

////////////////////////////////////////////////////////////////////////////////
/// prevents calls to descriptors

   PyObject* PyObject_GetAttrFromDict( PyObject* pyclass, PyObject* pyname ) {
      PyObject* dict = PyObject_GetAttr( pyclass, PyStrings::gDict );
      PyObject* attr = PyObject_GetItem( dict, pyname );
      Py_DECREF( dict );
      return attr;
   }

////////////////////////////////////////////////////////////////////////////////
/// Scan the name of the class and determine whether it is a template instantiation.

   inline Bool_t IsTemplatedSTLClass( const std::string& name, const std::string& klass ) {
      const int nsize = (int)name.size();
      const int ksize = (int)klass.size();

      return ( ( ksize   < nsize && name.substr(0,ksize) == klass ) ||
               ( ksize+5 < nsize && name.substr(5,ksize) == klass ) ) &&
             name.find( "::", name.find( ">" ) ) == std::string::npos;
   }

// to prevent compiler warnings about const char* -> char*
   inline PyObject* CallPyObjMethod( PyObject* obj, const char* meth )
   {
   // Helper; call method with signature: obj->meth().
      Py_INCREF( obj );
      PyObject* result = PyObject_CallMethod( obj, const_cast< char* >( meth ), const_cast< char* >( "" ) );
      Py_DECREF( obj );
      return result;
   }

////////////////////////////////////////////////////////////////////////////////
/// Helper; call method with signature: obj->meth( arg1 ).

   inline PyObject* CallPyObjMethod( PyObject* obj, const char* meth, PyObject* arg1 )
   {
      Py_INCREF( obj );
      PyObject* result = PyObject_CallMethod(
         obj, const_cast< char* >( meth ), const_cast< char* >( "O" ), arg1 );
      Py_DECREF( obj );
      return result;
   }

////////////////////////////////////////////////////////////////////////////////
/// Helper; call method with signature: obj->meth( arg1, arg2 ).

   inline PyObject* CallPyObjMethod(
      PyObject* obj, const char* meth, PyObject* arg1, PyObject* arg2 )
   {
      Py_INCREF( obj );
      PyObject* result = PyObject_CallMethod(
         obj, const_cast< char* >( meth ), const_cast< char* >( "OO" ), arg1, arg2 );
      Py_DECREF( obj );
      return result;
   }

////////////////////////////////////////////////////////////////////////////////
/// Helper; call method with signature: obj->meth( arg1, int ).

   inline PyObject* CallPyObjMethod( PyObject* obj, const char* meth, PyObject* arg1, int arg2 )
   {
      Py_INCREF( obj );
      PyObject* result = PyObject_CallMethod(
         obj, const_cast< char* >( meth ), const_cast< char* >( "Oi" ), arg1, arg2 );
      Py_DECREF( obj );
      return result;
   }


//- helpers --------------------------------------------------------------------
   PyObject* PyStyleIndex( PyObject* self, PyObject* index )
   {
   // Helper; converts python index into straight C index.
      Py_ssize_t idx = PyInt_AsSsize_t( index );
      if ( idx == (Py_ssize_t)-1 && PyErr_Occurred() )
         return 0;

      Py_ssize_t size = PySequence_Size( self );
      if ( idx >= size || ( idx < 0 && idx < -size ) ) {
         PyErr_SetString( PyExc_IndexError, "index out of range" );
         return 0;
      }

      PyObject* pyindex = 0;
      if ( idx >= 0 ) {
         Py_INCREF( index );
         pyindex = index;
      } else
         pyindex = PyLong_FromLong( size + idx );

      return pyindex;
   }

////////////////////////////////////////////////////////////////////////////////
/// Helper; call method with signature: meth( pyindex ).

   inline PyObject* CallSelfIndex( ObjectProxy* self, PyObject* idx, const char* meth )
   {
      Py_INCREF( (PyObject*)self );
      PyObject* pyindex = PyStyleIndex( (PyObject*)self, idx );
      if ( ! pyindex ) {
         Py_DECREF( (PyObject*)self );
         return 0;
      }

      PyObject* result = CallPyObjMethod( (PyObject*)self, meth, pyindex );
      Py_DECREF( pyindex );
      Py_DECREF( (PyObject*)self );
      return result;
   }

////////////////////////////////////////////////////////////////////////////////
/// Helper; convert generic python object into a boolean value.

   inline PyObject* BoolNot( PyObject* value )
   {
      if ( PyObject_IsTrue( value ) == 1 ) {
         Py_INCREF( Py_False );
         Py_DECREF( value );
         return Py_False;
      } else {
         Py_INCREF( Py_True );
         Py_XDECREF( value );
         return Py_True;
      }
   }

//- "smart pointer" behavior ---------------------------------------------------
   PyObject* DeRefGetAttr( PyObject* self, PyObject* name )
   {
   // Follow operator*() if present (available in python as __deref__), so that
   // smart pointers behave as expected.
      if ( ! CPyCppyy_PyUnicode_Check( name ) )
         PyErr_SetString( PyExc_TypeError, "getattr(): attribute name must be string" );

      PyObject* pyptr = CallPyObjMethod( self, "__deref__" );
      if ( ! pyptr )
         return 0;

   // prevent a potential infinite loop
      if ( Py_TYPE(pyptr) == Py_TYPE(self) ) {
         PyObject* val1 = PyObject_Str( self );
         PyObject* val2 = PyObject_Str( name );
         PyErr_Format( PyExc_AttributeError, "%s has no attribute \'%s\'",
            CPyCppyy_PyUnicode_AsString( val1 ), CPyCppyy_PyUnicode_AsString( val2 ) );
         Py_DECREF( val2 );
         Py_DECREF( val1 );

         Py_DECREF( pyptr );
         return 0;
      }

      PyObject* result = PyObject_GetAttr( pyptr, name );
      Py_DECREF( pyptr );
      return result;
   }

////////////////////////////////////////////////////////////////////////////////
/// Follow operator->() if present (available in python as __follow__), so that
/// smart pointers behave as expected.

   PyObject* FollowGetAttr( PyObject* self, PyObject* name )
   {
      if ( ! CPyCppyy_PyUnicode_Check( name ) )
         PyErr_SetString( PyExc_TypeError, "getattr(): attribute name must be string" );

      PyObject* pyptr = CallPyObjMethod( self, "__follow__" );
      if ( ! pyptr )
         return 0;

      PyObject* result = PyObject_GetAttr( pyptr, name );
      Py_DECREF( pyptr );
      return result;
   }

////////////////////////////////////////////////////////////////////////////////
/// Contrary to TObjectIsEqual, it can now not be relied upon that the only
/// non-ObjectProxy obj is None, as any operator==(), taking any object (e.g.
/// an enum) can be implemented. However, those cases will yield an exception
/// if presented with None.

   PyObject* GenObjectIsEqual( PyObject* self, PyObject* obj )
   {
      PyObject* result = CallPyObjMethod( self, "__cpp_eq__", obj );
      if ( ! result ) {
         PyErr_Clear();
         result = ObjectProxy_Type.tp_richcompare( self, obj, Py_EQ );
      }

      return result;
   }

////////////////////////////////////////////////////////////////////////////////
/// Reverse of GenObjectIsEqual, if operator!= defined.

   PyObject* GenObjectIsNotEqual( PyObject* self, PyObject* obj )
   {
      PyObject* result = CallPyObjMethod( self, "__cpp_ne__", obj );
      if ( ! result ) {
         PyErr_Clear();
         result = ObjectProxy_Type.tp_richcompare( self, obj, Py_NE );
      }

      return result;
   }

//- vector behavior as primitives ----------------------------------------------
   typedef struct {
      PyObject_HEAD
      PyObject*           vi_vector;
      void*               vi_data;
      CPyCppyy::TConverter* vi_converter;
      Py_ssize_t          vi_pos;
      Py_ssize_t          vi_len;
      Py_ssize_t          vi_stride;
   } vectoriterobject;

   static void vectoriter_dealloc( vectoriterobject* vi ) {
      Py_XDECREF( vi->vi_vector );
      delete vi->vi_converter;
      PyObject_GC_Del( vi );
   }

   static int vectoriter_traverse( vectoriterobject* vi, visitproc visit, void* arg ) {
      Py_VISIT( vi->vi_vector );
      return 0;
   }

   static PyObject* vectoriter_iternext( vectoriterobject* vi ) {
      if ( vi->vi_pos >= vi->vi_len )
         return nullptr;

      PyObject* result = nullptr;

      if ( vi->vi_data && vi->vi_converter ) {
         void* location  = (void*)((ptrdiff_t)vi->vi_data + vi->vi_stride * vi->vi_pos );
         result = vi->vi_converter->FromMemory( location );
      } else {
         PyObject* pyindex = PyLong_FromLong( vi->vi_pos );
         result = CallPyObjMethod( (PyObject*)vi->vi_vector, "_vector__at", pyindex );
         Py_DECREF( pyindex );
      }

      vi->vi_pos += 1;
      return result;
   }

   PyTypeObject VectorIter_Type = {
      PyVarObject_HEAD_INIT( &PyType_Type, 0 )
      (char*)"cppyy.vectoriter",  // tp_name
      sizeof(vectoriterobject),  // tp_basicsize
      0,
      (destructor)vectoriter_dealloc,            // tp_dealloc
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      Py_TPFLAGS_DEFAULT |
         Py_TPFLAGS_HAVE_GC,     // tp_flags
      0,
      (traverseproc)vectoriter_traverse,         // tp_traverse
      0, 0, 0,
      PyObject_SelfIter,         // tp_iter
      (iternextfunc)vectoriter_iternext,         // tp_iternext
      0,                         // tp_methods
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
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

   static PyObject* vector_iter( PyObject* v ) {
      vectoriterobject* vi = PyObject_GC_New( vectoriterobject, &VectorIter_Type );
      if ( ! vi ) return NULL;

      Py_INCREF( v );
      vi->vi_vector = v;

      PyObject* pyvalue_type = PyObject_GetAttrString( (PyObject*)Py_TYPE(v), "value_type" );
      PyObject* pyvalue_size = PyObject_GetAttrString( (PyObject*)Py_TYPE(v), "value_size" );

      if ( pyvalue_type && pyvalue_size ) {
         PyObject* pydata = CallPyObjMethod( v, "data" );
         if ( !pydata || Utility::GetBuffer( pydata, '*', 1, vi->vi_data, kFALSE ) == 0 )
            vi->vi_data = nullptr;
         Py_XDECREF( pydata );

         vi->vi_converter = CPyCppyy::CreateConverter( CPyCppyy_PyUnicode_AsString( pyvalue_type ) );
         vi->vi_stride    = PyLong_AsLong( pyvalue_size );
      } else {
         PyErr_Clear();
         vi->vi_data      = nullptr;
         vi->vi_converter = nullptr;
         vi->vi_stride    = 0;
      }

      Py_XDECREF( pyvalue_size );
      Py_XDECREF( pyvalue_type );

      vi->vi_len = vi->vi_pos = 0;
      vi->vi_len = PySequence_Size( v );

      _PyObject_GC_TRACK( vi );
      return (PyObject*)vi;
   }


   PyObject* VectorGetItem( ObjectProxy* self, PySliceObject* index )
   {
   // Implement python's __getitem__ for std::vector<>s.
      if ( PySlice_Check( index ) ) {
         if ( ! self->GetObject() ) {
            PyErr_SetString( PyExc_TypeError, "unsubscriptable object" );
            return 0;
         }

         PyObject* pyclass = PyObject_GetAttr( (PyObject*)self, PyStrings::gClass );
         PyObject* nseq = PyObject_CallObject( pyclass, NULL );
         Py_DECREF( pyclass );

         Py_ssize_t start, stop, step;
         PySlice_GetIndices( (CPyCppyy_PySliceCast)index, PyObject_Length( (PyObject*)self ), &start, &stop, &step );
         for ( Py_ssize_t i = start; i < stop; i += step ) {
            PyObject* pyidx = PyInt_FromSsize_t( i );
            CallPyObjMethod( nseq, "push_back", CallPyObjMethod( (PyObject*)self, "_vector__at", pyidx ) );
            Py_DECREF( pyidx );
         }

         return nseq;
      }

      return CallSelfIndex( self, (PyObject*)index, "_vector__at" );
   }

   PyObject* VectorBoolSetItem( ObjectProxy* self, PyObject* args )
   {
   // std::vector<bool> is a special-case in C++, and its return type depends on
   // the compiler: treat it special here as well
      int bval = 0; PyObject* idx = 0;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "Oi:__setitem__" ), &idx, &bval ) )
         return 0;

      if ( ! self->GetObject() ) {
         PyErr_SetString( PyExc_TypeError, "unsubscriptable object" );
         return 0;
      }

      PyObject* pyindex = PyStyleIndex( (PyObject*)self, idx );
      if ( ! pyindex )
         return 0;
      int index = (int)PyLong_AsLong( pyindex );
      Py_DECREF( pyindex );

      std::string clName = Cppyy::GetFinalName( self->ObjectIsA() );
      std::string::size_type pos = clName.find( "vector<bool" );
      if ( pos != 0 && pos != 5 /* following std:: */ ) {
         PyErr_Format( PyExc_TypeError,
                       "require object of type std::vector<bool>, but %s given",
                       Cppyy::GetFinalName( self->ObjectIsA() ).c_str() );
         return 0;
      }

   // get hold of the actual std::vector<bool> (no cast, as vector is never a base)
      std::vector<bool>* vb = (std::vector<bool>*)self->GetObject();

   // finally, set the value
      (*vb)[ index ] = (bool)bval;

      Py_INCREF( Py_None );
      return Py_None;
   }

//- map behavior as primitives ------------------------------------------------
   PyObject* MapContains( PyObject* self, PyObject* obj )
   {
   // Implement python's __contains__ for std::map<>s.
      PyObject* result = 0;

      PyObject* iter = CallPyObjMethod( self, "find", obj );
      if ( ObjectProxy_Check( iter ) ) {
         PyObject* end = CallPyObjMethod( self, "end" );
         if ( ObjectProxy_Check( end ) ) {
            if ( ! PyObject_RichCompareBool( iter, end, Py_EQ ) ) {
               Py_INCREF( Py_True );
               result = Py_True;
            }
         }
         Py_XDECREF( end );
      }
      Py_XDECREF( iter );

      if ( ! result ) {
         PyErr_Clear();            // e.g. wrong argument type, which should always lead to False
         Py_INCREF( Py_False );
         result = Py_False;
      }

      return result;
   }

//- STL container iterator support --------------------------------------------
   PyObject* StlSequenceIter( PyObject* self )
   {
   // Implement python's __iter__ for std::iterator<>s.
      PyObject* iter = CallPyObjMethod( self, "begin" );
      if ( iter ) {
         PyObject* end = CallPyObjMethod( self, "end" );
         if ( end )
            PyObject_SetAttr( iter, PyStrings::gEnd, end );
         Py_XDECREF( end );

         // add iterated collection as attribute so its refcount stays >= 1 while it's being iterated over
         PyObject_SetAttr( iter, PyUnicode_FromString("_collection"), self );
      }
      return iter;
   }

//- safe indexing for STL-like vector w/o iterator dictionaries ---------------
   PyObject* CheckedGetItem( PyObject* self, PyObject* obj )
   {
   // Implement a generic python __getitem__ for std::vector<>s that are missing
   // their std::vector<>::iterator dictionary. This is then used for iteration
   // by means of consecutive index.
      Bool_t inbounds = kFALSE;
      Py_ssize_t size = PySequence_Size( self );
      Py_ssize_t idx  = PyInt_AsSsize_t( obj );
      if ( 0 <= idx && 0 <= size && idx < size )
         inbounds = kTRUE;

      if ( inbounds ) {
         return CallPyObjMethod( self, "_getitem__unchecked", obj );
      } else if ( PyErr_Occurred() ) {
      // argument conversion problem: let method itself resolve anew and report
         PyErr_Clear();
         return CallPyObjMethod( self, "_getitem__unchecked", obj );
      } else {
         PyErr_SetString( PyExc_IndexError, "index out of range" );
      }

      return 0;
   }

//- pair as sequence to allow tuple unpacking ---------------------------------
   PyObject* PairUnpack( PyObject* self, PyObject* pyindex )
   {
   // For std::map<> iteration, unpack std::pair<>s into tuples for the loop.
      Long_t idx = PyLong_AsLong( pyindex );
      if ( idx == -1 && PyErr_Occurred() )
         return 0;

      if ( ! ObjectProxy_Check( self ) || ! ((ObjectProxy*)self)->GetObject() ) {
         PyErr_SetString( PyExc_TypeError, "unsubscriptable object" );
         return 0;
      }

      if ( (int)idx == 0 )
         return PyObject_GetAttr( self, PyStrings::gFirst );
      else if ( (int)idx == 1 )
         return PyObject_GetAttr( self, PyStrings::gSecond );

   // still here? Trigger stop iteration
      PyErr_SetString( PyExc_IndexError, "out of bounds" );
      return 0;
   }

//- simplistic len() functions -------------------------------------------------
   PyObject* ReturnTwo( ObjectProxy*, PyObject* ) {
      return PyInt_FromLong( 2 );
   }

//- string behavior as primitives ----------------------------------------------
#if PY_VERSION_HEX >= 0x03000000
// TODO: this is wrong, b/c it doesn't order
static int PyObject_Compare( PyObject* one, PyObject* other ) {
   return ! PyObject_RichCompareBool( one, other, Py_EQ );
}
#endif
   static inline PyObject* CPyCppyy_PyString_FromCppString( std::string* s ) {
      return CPyCppyy_PyUnicode_FromStringAndSize( s->c_str(), s->size() );
   }

#define CPYCPPYY_IMPLEMENT_STRING_PYTHONIZATION( type, name )                 \
   inline PyObject* name##GetData( PyObject* self ) {                         \
      if ( CPyCppyy::ObjectProxy_Check( self ) ) {                            \
         type* obj = ((type*)((ObjectProxy*)self)->GetObject());              \
         if ( obj ) {                                                         \
            return CPyCppyy_PyString_FromCppString( obj );                    \
         } else {                                                             \
            return ObjectProxy_Type.tp_str( self );                           \
         }                                                                    \
      }                                                                       \
      PyErr_Format( PyExc_TypeError, "object mismatch (%s expected)", #type );\
      return 0;                                                               \
   }                                                                          \
                                                                              \
   PyObject* name##StringRepr( PyObject* self )                               \
   {                                                                          \
      PyObject* data = name##GetData( self );                                 \
      if ( data ) {                                                           \
         PyObject* repr = CPyCppyy_PyUnicode_FromFormat( "\'%s\'", CPyCppyy_PyUnicode_AsString( data ) ); \
         Py_DECREF( data );                                                   \
         return repr;                                                         \
      }                                                                       \
      return 0;                                                               \
   }                                                                          \
                                                                              \
   PyObject* name##StringIsEqual( PyObject* self, PyObject* obj )             \
   {                                                                          \
      PyObject* data = name##GetData( self );                                 \
      if ( data ) {                                                           \
         PyObject* result = PyObject_RichCompare( data, obj, Py_EQ );         \
         Py_DECREF( data );                                                   \
         return result;                                                       \
      }                                                                       \
      return 0;                                                               \
   }                                                                          \
                                                                              \
   PyObject* name##StringIsNotEqual( PyObject* self, PyObject* obj )          \
   {                                                                          \
      PyObject* data = name##GetData( self );                                 \
      if ( data ) {                                                           \
         PyObject* result = PyObject_RichCompare( data, obj, Py_NE );         \
         Py_DECREF( data );                                                   \
         return result;                                                       \
      }                                                                       \
      return 0;                                                               \
   }

   // Only define StlStringCompare:
#define CPYCPPYY_IMPLEMENT_STRING_PYTHONIZATION_CMP( type, name )             \
   CPYCPPYY_IMPLEMENT_STRING_PYTHONIZATION( type, name )                      \
   PyObject* name##StringCompare( PyObject* self, PyObject* obj )             \
   {                                                                          \
      PyObject* data = name##GetData( self );                                 \
      int result = 0;                                                         \
      if ( data ) {                                                           \
         result = PyObject_Compare( data, obj );                              \
         Py_DECREF( data );                                                   \
      }                                                                       \
      if ( PyErr_Occurred() )                                                 \
         return 0;                                                            \
      return PyInt_FromLong( result );                                        \
   }

   CPYCPPYY_IMPLEMENT_STRING_PYTHONIZATION_CMP( std::string, Stl )


//- STL iterator behavior ------------------------------------------------------
   PyObject* StlIterNext( PyObject* self )
   {
   // Python iterator protocol __next__ for STL forward iterators.
      PyObject* next = 0;
      PyObject* last = PyObject_GetAttr( self, PyStrings::gEnd );

      if ( last != 0 ) {
      // handle special case of empty container (i.e. self is end)
         if ( PyObject_RichCompareBool( last, self, Py_EQ ) ) {
            PyErr_SetString( PyExc_StopIteration, "" );
         } else {
            PyObject* dummy = PyInt_FromLong( 1l );
            PyObject* iter = CallPyObjMethod( self, "__postinc__", dummy );
            Py_DECREF( dummy );
            if ( iter != 0 ) {
               if ( PyObject_RichCompareBool( last, iter, Py_EQ ) )
                  PyErr_SetString( PyExc_StopIteration, "" );
               else
                  next = CallPyObjMethod( iter, "__deref__" );
            } else {
               PyErr_SetString( PyExc_StopIteration, "" );
            }
            Py_XDECREF( iter );
         }
      } else {
         PyErr_SetString( PyExc_StopIteration, "" );
      }

      Py_XDECREF( last );
      return next;
   }

////////////////////////////////////////////////////////////////////////////////
/// Called if operator== not available (e.g. if a global overload as under gcc).
/// An exception is raised as the user should fix the dictionary.

   PyObject* StlIterIsEqual( PyObject* self, PyObject* other )
   {
      return PyErr_Format( PyExc_LookupError,
         "No operator==(const %s&, const %s&) available in the dictionary!",
         Utility::ClassName( self ).c_str(), Utility::ClassName( other ).c_str()  );
   }

////////////////////////////////////////////////////////////////////////////////
/// Called if operator!= not available (e.g. if a global overload as under gcc).
/// An exception is raised as the user should fix the dictionary.

   PyObject* StlIterIsNotEqual( PyObject* self, PyObject* other )
   {
      return PyErr_Format( PyExc_LookupError,
         "No operator!=(const %s&, const %s&) available in the dictionary!",
         Utility::ClassName( self ).c_str(), Utility::ClassName( other ).c_str()  );
   }

} // unnamed namespace


//- public functions -----------------------------------------------------------
Bool_t CPyCppyy::Pythonize( PyObject* pyclass, const std::string& name )
{
// Add pre-defined pythonizations (for STL and ROOT) to classes based on their
// signature and/or class name.
   if ( pyclass == 0 )
      return kFALSE;

//- method name based pythonization --------------------------------------------

// for smart pointer style classes (note fall-through)
   if ( HasAttrDirect( pyclass, PyStrings::gDeref ) ) {
      Utility::AddToClass( pyclass, "__getattr__", (PyCFunction) DeRefGetAttr, METH_O );
   } else if ( HasAttrDirect( pyclass, PyStrings::gFollow ) ) {
      Utility::AddToClass( pyclass, "__getattr__", (PyCFunction) FollowGetAttr, METH_O );
   }

// for STL containers, and user classes modeled after them
   if ( HasAttrDirect( pyclass, PyStrings::gSize ) )
      Utility::AddToClass( pyclass, "__len__", "size" );

   if ( HasAttrDirect( pyclass, PyStrings::gBegin ) && HasAttrDirect( pyclass, PyStrings::gEnd ) ) {
   // some classes may not have dicts for their iterators, making begin/end useless
   /* TODO: remove use of TClass/TMethod
      PyObject* pyfullname = PyObject_GetAttr( pyclass, PyStrings::gCppName );
      if ( ! pyfullname ) pyfullname = PyObject_GetAttr( pyclass, PyStrings::gName );
      TClass* klass = TClass::GetClass( CPyCppyy_PyUnicode_AsString( pyfullname ) );
      Py_DECREF( pyfullname );

      TMethod* meth = klass->GetMethodAllAny( "begin" );

      TClass* iklass = 0;
      if ( meth ) {
         Int_t oldl = gErrorIgnoreLevel; gErrorIgnoreLevel = 3000;
         iklass = TClass::GetClass( meth->GetReturnTypeNormalizedName().c_str() );
         gErrorIgnoreLevel = oldl;
      }

      if ( iklass && iklass->GetClassInfo() ) {
         ((PyTypeObject*)pyclass)->tp_iter     = (getiterfunc)StlSequenceIter;
         Utility::AddToClass( pyclass, "__iter__", (PyCFunction) StlSequenceIter, METH_NOARGS );
      } else if ( HasAttrDirect( pyclass, PyStrings::gGetItem ) && HasAttrDirect( pyclass, PyStrings::gLen ) ) {
         Utility::AddToClass( pyclass, "_getitem__unchecked", "__getitem__" );
         Utility::AddToClass( pyclass, "__getitem__", (PyCFunction) CheckedGetItem, METH_O );
      }
      */
   }

// search for global comparator overloads (may fail; not sure whether it isn't better to
// do this lazily just as is done for math operators, but this interplays nicely with the
// generic versions)
   Utility::AddBinaryOperator( pyclass, "==", "__eq__" );
   Utility::AddBinaryOperator( pyclass, "!=", "__ne__" );

// map operator==() through GenObjectIsEqual to allow comparison to None (kTRUE is to
// require that the located method is a MethodProxy; this prevents circular calls as
// GenObjectIsEqual is no MethodProxy)
   if ( HasAttrDirect( pyclass, PyStrings::gEq, kTRUE ) ) {
      Utility::AddToClass( pyclass, "__cpp_eq__",  "__eq__" );
      Utility::AddToClass( pyclass, "__eq__",  (PyCFunction) GenObjectIsEqual, METH_O );
   }

// map operator!=() through GenObjectIsNotEqual to allow comparison to None (see note
// on kTRUE above for __eq__)
   if ( HasAttrDirect( pyclass, PyStrings::gNe, kTRUE ) ) {
      Utility::AddToClass( pyclass, "__cpp_ne__",  "__ne__" );
      Utility::AddToClass( pyclass, "__ne__",  (PyCFunction) GenObjectIsNotEqual, METH_O );
   }


//- class name based pythonization ---------------------------------------------

   if ( IsTemplatedSTLClass( name, "vector" ) ) {

      if ( HasAttrDirect( pyclass, PyStrings::gLen ) && HasAttrDirect( pyclass, PyStrings::gAt ) ) {
         Utility::AddToClass( pyclass, "_vector__at", "at" );
      // remove iterator that was set earlier (checked __getitem__ will do the trick)
         if ( HasAttrDirect( pyclass, PyStrings::gIter ) )
            PyObject_DelAttr( pyclass, PyStrings::gIter );
      } else if ( HasAttrDirect( pyclass, PyStrings::gGetItem ) ) {
         Utility::AddToClass( pyclass, "_vector__at", "__getitem__" );   // unchecked!
      }

   // vector-optimized iterator protocol
      ((PyTypeObject*)pyclass)->tp_iter     = (getiterfunc)vector_iter;

   // helpers for iteration
   /*TODO: remove this use of gInterpreter
      TypedefInfo_t* ti = gInterpreter->TypedefInfo_Factory( (name+"::value_type").c_str() );
      if ( gInterpreter->TypedefInfo_IsValid( ti ) ) {
         PyObject* pyvalue_size = PyLong_FromLong( gInterpreter->TypedefInfo_Size( ti ) );
         PyObject_SetAttrString( pyclass, "value_size", pyvalue_size );
         Py_DECREF( pyvalue_size );

         PyObject* pyvalue_type = CPyCppyy_PyUnicode_FromString( gInterpreter->TypedefInfo_TrueName( ti ) );
         PyObject_SetAttrString( pyclass, "value_type", pyvalue_type );
         Py_DECREF( pyvalue_type );
      }
      gInterpreter->TypedefInfo_Delete( ti );
      */

   // provide a slice-able __getitem__, if possible
      if ( HasAttrDirect( pyclass, PyStrings::gVectorAt ) )
         Utility::AddToClass( pyclass, "__getitem__", (PyCFunction) VectorGetItem, METH_O );

   // std::vector<bool> is a special case in C++
      std::string::size_type pos = name.find( "vector<bool" ); // to cover all variations
      if ( pos == 0 /* at beginning */ || pos == 5 /* after std:: */ ) {
         Utility::AddToClass( pyclass, "__setitem__", (PyCFunction) VectorBoolSetItem );
      }

   }

   else if ( IsTemplatedSTLClass( name, "map" ) ) {
      Utility::AddToClass( pyclass, "__contains__", (PyCFunction) MapContains, METH_O );

   }

   else if ( IsTemplatedSTLClass( name, "pair" ) ) {
      Utility::AddToClass( pyclass, "__getitem__", (PyCFunction) PairUnpack, METH_O );
      Utility::AddToClass( pyclass, "__len__", (PyCFunction) ReturnTwo, METH_NOARGS );

   }

   else if ( name.find( "iterator" ) != std::string::npos ) {
      ((PyTypeObject*)pyclass)->tp_iternext = (iternextfunc)StlIterNext;
      Utility::AddToClass( pyclass, CPYCPPYY__next__, (PyCFunction) StlIterNext, METH_NOARGS );

   // special case, if operator== is a global overload and included in the dictionary
      if ( ! HasAttrDirect( pyclass, PyStrings::gCppEq, kTRUE ) )
         Utility::AddToClass( pyclass, "__eq__",  (PyCFunction) StlIterIsEqual, METH_O );
      if ( ! HasAttrDirect( pyclass, PyStrings::gCppNe, kTRUE ) )
         Utility::AddToClass( pyclass, "__ne__",  (PyCFunction) StlIterIsNotEqual, METH_O );

   }

   else if ( name == "string" || name == "std::string" ) {
      Utility::AddToClass( pyclass, "__repr__", (PyCFunction) StlStringRepr, METH_NOARGS );
      Utility::AddToClass( pyclass, "__str__", "c_str" );
      Utility::AddToClass( pyclass, "__cmp__", (PyCFunction) StlStringCompare, METH_O );
      Utility::AddToClass( pyclass, "__eq__",  (PyCFunction) StlStringIsEqual, METH_O );
      Utility::AddToClass( pyclass, "__ne__",  (PyCFunction) StlStringIsNotEqual, METH_O );

   }

// TODO: store these on the pythonizations module, not on gThisModule
// TODO: externalize this code and use update handlers on the python side
   PyObject* userPythonizations = PyObject_GetAttrString( gThisModule, "UserPythonizations" );
   PyObject* pythonizationScope = PyObject_GetAttrString( gThisModule, "PythonizationScope" );

   std::vector< std::string > pythonization_scopes;
   pythonization_scopes.push_back( "__global__" );

   std::string user_scope = CPyCppyy_PyUnicode_AsString( pythonizationScope );
   if ( user_scope != "__global__" ) {
      if ( PyDict_Contains( userPythonizations, pythonizationScope ) ) {
          pythonization_scopes.push_back( user_scope );
      }
   }

   Bool_t pstatus = kTRUE;

   for ( auto key = pythonization_scopes.cbegin(); key != pythonization_scopes.cend(); ++key ) {
      PyObject* tmp = PyDict_GetItemString( userPythonizations, key->c_str() );
      Py_ssize_t num_pythonizations = PyList_Size( tmp );
      PyObject* arglist = nullptr;
      if ( num_pythonizations )
         arglist = Py_BuildValue( "O,s", pyclass, name.c_str() );
      for ( Py_ssize_t i = 0; i < num_pythonizations; ++i ) {
         PyObject* pythonizor = PyList_GetItem( tmp, i );
      // TODO: detail error handling for the pythonizors
         PyObject* result = PyObject_CallObject( pythonizor, arglist );
         if ( !result ) {
            pstatus = kFALSE;
            break;
         } else
            Py_DECREF( result );
      }
      Py_XDECREF( arglist );
   }

   Py_DECREF( userPythonizations );
   Py_DECREF( pythonizationScope );


// phew! all done ...
   return pstatus;
}
