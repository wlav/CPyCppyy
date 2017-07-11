// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "CPyCppyyType.h"
#include "ObjectProxy.h"
#include "MethodProxy.h"
#include "TemplateProxy.h"
#include "PropertyProxy.h"
#include "TPyBufferFactory.h"
#include "TCustomPyTypes.h"
#include "TTupleOfInstances.h"
#include "CPyCppyyHelpers.h"
#include "TCallContext.h"
#include "Utility.h"

// Standard
#include <string>
#include <sstream>
#include <utility>
#include <vector>


//- from Python's dictobject.c -------------------------------------------------
#if PY_VERSION_HEX >= 0x03030000
   typedef struct PyDictKeyEntry {
      /* Cached hash code of me_key. */
      Py_hash_t me_hash;
      PyObject *me_key;
      PyObject *me_value; /* This field is only meaningful for combined tables */
   } PyDictEntry;

   typedef struct _dictkeysobject {
      Py_ssize_t dk_refcnt;
      Py_ssize_t dk_size;
      dict_lookup_func dk_lookup;
      Py_ssize_t dk_usable;
      PyDictKeyEntry dk_entries[1];
   } PyDictKeysObject;

#define CPYCPPYY_GET_DICT_LOOKUP( mp )\
   ((dict_lookup_func&)mp->ma_keys->dk_lookup)

#else

#define CPYCPPYY_GET_DICT_LOOKUP( mp )\
   ((dict_lookup_func&)mp->ma_lookup)

#endif

//- data -----------------------------------------------------------------------
static PyObject* nullptr_repr( PyObject* )
{
   return PyBytes_FromString( "nullptr" );
}

static void nullptr_dealloc( PyObject* )
{
   Py_FatalError( "deallocating nullptr" );
}

static int nullptr_nonzero( PyObject* )
{
   return 0;
}

static PyNumberMethods nullptr_as_number = {
   0, 0, 0,
#if PY_VERSION_HEX < 0x03000000
   0,
#endif
   0, 0, 0, 0, 0, 0,
   (inquiry)nullptr_nonzero,          // tp_nonzero (nb_bool in p3)
   0, 0, 0, 0, 0, 0,
#if PY_VERSION_HEX < 0x03000000
   0,                                 // nb_coerce
#endif
   0, 0, 0,
#if PY_VERSION_HEX < 0x03000000
   0, 0,
#endif
   0, 0, 0,
#if PY_VERSION_HEX < 0x03000000
   0,                                 // nb_inplace_divide
#endif
   0, 0, 0, 0, 0, 0, 0
#if PY_VERSION_HEX >= 0x02020000
   , 0                                // nb_floor_divide
#if PY_VERSION_HEX < 0x03000000
   , 0                                // nb_true_divide
#else
   , 0                                // nb_true_divide
#endif
   , 0, 0
#endif
#if PY_VERSION_HEX >= 0x02050000
   , 0                                // nb_index
#endif
#if PY_VERSION_HEX >= 0x03050000
   , 0                                // nb_matrix_multiply
   , 0                                // nb_inplace_matrix_multiply
#endif

   };

static PyTypeObject PyNullPtr_t_Type = {
   PyVarObject_HEAD_INIT( &PyType_Type, 0 )
   "nullptr_t",        // tp_name
   sizeof(PyObject),   // tp_basicsize
   0,                  // tp_itemsize
   nullptr_dealloc,    // tp_dealloc (never called)
   0, 0, 0, 0,
   nullptr_repr,       // tp_repr
   &nullptr_as_number, // tp_as_number
   0, 0,
   (hashfunc)_Py_HashPointer, // tp_hash
   0, 0, 0, 0, 0, Py_TPFLAGS_DEFAULT, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#if PY_VERSION_HEX >= 0x02030000
   , 0                 // tp_del
#endif
#if PY_VERSION_HEX >= 0x02060000
   , 0                 // tp_version_tag
#endif
#if PY_VERSION_HEX >= 0x03040000
   , 0                        // tp_finalize
#endif
};

PyObject _CPyCppyy_NullPtrStruct = {
  _PyObject_EXTRA_INIT
  1, &PyNullPtr_t_Type
};

namespace CPyCppyy {
   PyObject* gThisModule = 0;
   PyObject* gNullPtrObject = 0;
   std::vector<std::pair<Cppyy::TCppType_t, Cppyy::TCppType_t> > gPinnedTypes;
   std::vector<Cppyy::TCppType_t> gIgnorePinnings;
}


//- private helpers ------------------------------------------------------------
namespace {

   using namespace CPyCppyy;

   PyObject* RootModuleResetCallback( PyObject*, PyObject* )
   {
      gThisModule = 0;   // reference was borrowed
      Py_INCREF( Py_None );
      return Py_None;
   }

//----------------------------------------------------------------------------
   PyObject* LookupCppEntity( PyObject* pyname, PyObject* args )
   {
   // Find a match within the ROOT module for something with name 'pyname'.
      const char* cname = 0; long macro_ok = 0;
      if ( pyname && CPyCppyy_PyUnicode_CheckExact( pyname ) )
         cname = CPyCppyy_PyUnicode_AsString( pyname );
      else if ( ! ( args && PyArg_ParseTuple( args, const_cast< char* >( "s|l" ), &cname, &macro_ok ) ) )
         return 0;

   // we may have been destroyed if this code is called during shutdown
      if ( !gThisModule ) {
         PyErr_Format( PyExc_AttributeError, "%s", cname );
         return 0;
      }

      std::string name = cname;

   // block search for privates
      if ( name.size() <= 2 || name.substr( 0, 2 ) != "__" ) {
      // 1st attempt: look in myself
         PyObject* attr = PyObject_GetAttrString( gThisModule, const_cast< char* >( cname ) );
         if ( attr != 0 )
            return attr;

      // 2nd attempt: construct name as a class
         PyErr_Clear();
         attr = CreateScopeProxy( name, 0 /* parent */);
         if ( attr != 0 )
            return attr;

      // 3rd attempt: lookup name as global variable
         PyErr_Clear();
         attr = GetCppGlobal( name );
         if ( attr != 0 )
            return attr;

      // 4th attempt: global enum (pretend int, TODO: is fine for C++98, not in C++11)
         if ( Cppyy::IsEnum( name ) ) {
            Py_INCREF( &PyInt_Type );
            return (PyObject*)&PyInt_Type;
         }

      }

   // still here? raise attribute error
      PyErr_Format( PyExc_AttributeError, "%s", name.c_str() );
      return 0;
   }


//----------------------------------------------------------------------------
#if PY_VERSION_HEX >= 0x03030000
   inline PyDictKeyEntry* OrgDictLookup(
         PyDictObject* mp, PyObject* key, Py_hash_t hash, PyObject*** value_addr )
   {
      return (*gDictLookupOrg)( mp, key, hash, value_addr );
   }

#define CPYCPPYY_ORGDICT_LOOKUP( mp, key, hash, value_addr )\
   OrgDictLookup( mp, key, hash, value_addr )

   PyDictKeyEntry* RootLookDictString(
         PyDictObject* mp, PyObject* key, Py_hash_t hash, PyObject*** value_addr )
#else
   inline PyDictEntry* OrgDictLookup( PyDictObject* mp, PyObject* key, Long_t hash )
   {
       return (*gDictLookupOrg)( mp, key, hash );
   }

#define CPYCPPYY_ORGDICT_LOOKUP( mp, key, hash, value_addr )\
   OrgDictLookup( mp, key, hash )

   PyDictEntry* RootLookDictString( PyDictObject* mp, PyObject* key, Long_t hash )
#endif
   {
   // first search dictionary itself
      PyDictEntry* ep = CPYCPPYY_ORGDICT_LOOKUP( mp, key, hash, value_addr );
      if ( ! ep || (ep->me_key && ep->me_value) || gDictLookupActive )
         return ep;

   // filter for builtins
      if ( PyDict_GetItem( PyEval_GetBuiltins(), key ) != 0 ) {
         return ep;
      }

   // all failed, start calling into ROOT
      gDictLookupActive = kTRUE;

   // ROOT globals (the round-about lookup is to prevent recursion)
      PyObject* gval = PyDict_GetItem( PyModule_GetDict( gThisModule ), key );
      if ( gval ) {
         Py_INCREF( gval );
         ep->me_value = gval;
         ep->me_key   = key;
         ep->me_hash  = hash;
#if PY_VERSION_HEX >= 0x03030000
         *value_addr  = &gval;
#endif
         gDictLookupActive = kFALSE;
         return ep;
      }

   // attempt to get ROOT enum/global/class
      PyObject* val = LookupCppEntity( key, 0 );

      if ( val != 0 ) {
      // success ...

         if ( PropertyProxy_CheckExact( val ) ) {
         // don't want to add to dictionary (the proper place would be the
         // dictionary of the (meta)class), but modifying ep will be noticed no
         // matter what; just return the actual value and live with the copy in
         // the dictionary (mostly, this is correct)
            PyObject* actual_val = Py_TYPE(val)->tp_descr_get( val, NULL, NULL );
            Py_DECREF( val );
            val = actual_val;
         }

      // add reference to ROOT entity in the given dictionary
         CPYCPPYY_GET_DICT_LOOKUP( mp ) = gDictLookupOrg;     // prevent recursion
         if ( PyDict_SetItem( (PyObject*)mp, key, val ) == 0 ) {
            ep = CPYCPPYY_ORGDICT_LOOKUP( mp, key, hash, value_addr );
         } else {
            ep->me_key   = 0;
            ep->me_value = 0;
         }
         CPYCPPYY_GET_DICT_LOOKUP( mp ) = RootLookDictString; // restore

      // done with val
         Py_DECREF( val );
      } else
         PyErr_Clear();

#if PY_VERSION_HEX >= 0x03030000
      if ( mp->ma_keys->dk_usable <= 0 ) {
      // big risk that this lookup will result in a resize, so force it here
      // to be able to reset the lookup function; of course, this is nowhere
      // near fool-proof, but should cover interactive usage ...
         CPYCPPYY_GET_DICT_LOOKUP( mp ) = gDictLookupOrg;
         const int maxinsert = 5;
         PyObject* buf[maxinsert];
         for ( int varmax = 1; varmax <= maxinsert; ++varmax ) {
            for ( int ivar = 0; ivar < varmax; ++ivar ) {
               buf[ivar] = CPyCppyy_PyUnicode_FromFormat( "__ROOT_FORCE_RESIZE_%d", ivar );
               PyDict_SetItem( (PyObject*)mp, buf[ivar], Py_None);
            }
            for ( int ivar = 0; ivar < varmax; ++ivar ) {
               PyDict_DelItem( (PyObject*)mp, buf[ivar] );
               Py_DECREF( buf[ivar] );
            }
            if ( 0 < mp->ma_keys->dk_usable )
               break;
         }

      // make sure the entry pointer is still valid by re-doing the lookup
         ep = CPYCPPYY_ORGDICT_LOOKUP( mp, key, hash, value_addr );

      // full reset of all lookup functions
         gDictLookupOrg = CPYCPPYY_GET_DICT_LOOKUP( mp );
         CPYCPPYY_GET_DICT_LOOKUP( mp ) = RootLookDictString; // restore
      }
#endif

   // stopped calling into ROOT
      gDictLookupActive = kFALSE;

      return ep;
   }

//----------------------------------------------------------------------------
   PyObject* SetRootLazyLookup( PyObject*, PyObject* args )
   {
   // Modify the given dictionary to install the lookup function that also
   // tries the ROOT namespace before failing. Called on a module's dictionary,
   // this allows for lazy lookups.
      PyDictObject* dict = 0;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!" ), &PyDict_Type, &dict ) )
         return 0;

   // Notwithstanding the code changes, the following does not work for p3.3 and
   // later: once the dictionary is resized for anything other than an insert (see
   // hack in RootLookDictString), its lookup function on its keys will revert to
   // the default (lookdict_unicode_nodummy) and only if the resizing dictionary
   // has the generic lookdict function as dk_lookup for its keys, will this be
   // set on the new keys.
      CPYCPPYY_GET_DICT_LOOKUP( dict ) = RootLookDictString;

      Py_INCREF( Py_None );
      return Py_None;
   }

//----------------------------------------------------------------------------
   PyObject* MakeCppTemplateClass( PyObject*, PyObject* args )
   {
   // Create a binding for a templated class instantiation.

   // args is class name + template arguments; build full instantiation
      Py_ssize_t nArgs = PyTuple_GET_SIZE( args );
      if ( nArgs < 2 ) {
         PyErr_Format( PyExc_TypeError, "too few arguments for template instantiation" );
         return 0;
      }

   // build "< type, type, ... >" part of class name (modifies pyname)
      PyObject* pyname = Utility::BuildTemplateName( PyTuple_GET_ITEM( args, 0 ), args, 1 );
      if ( ! pyname )
         return 0;

      std::string name = CPyCppyy_PyUnicode_AsString( pyname );
      Py_DECREF( pyname );

      return CreateScopeProxy( name );
   }

//----------------------------------------------------------------------------
   void* GetObjectProxyAddress( PyObject*, PyObject* args )
   {
   // Helper to get the address (address-of-address) of various object proxy types.
      ObjectProxy* pyobj = 0;
      PyObject* pyname = 0;
      if ( PyArg_ParseTuple( args, const_cast< char* >( "O|O!" ), &pyobj,
             &CPyCppyy_PyUnicode_Type, &pyname ) &&
           ObjectProxy_Check( pyobj ) && pyobj->fObject ) {

         if ( pyname != 0 ) {
         // locate property proxy for offset info
            PropertyProxy* pyprop = 0;

            PyObject* pyclass = PyObject_GetAttr( (PyObject*)pyobj, PyStrings::gClass );

            if ( pyclass ) {
               PyObject* dict = PyObject_GetAttr( pyclass, PyStrings::gDict );
               pyprop = (PropertyProxy*)PyObject_GetItem( dict, pyname );
               Py_DECREF( dict );
            }
            Py_XDECREF( pyclass );

            if ( PropertyProxy_Check( pyprop ) ) {
            // this is an address of a value (i.e. &myobj->prop)
               void* addr = (void*)pyprop->GetAddress( pyobj );
               Py_DECREF( pyprop );
               return addr;
            }

            Py_XDECREF( pyprop );

            PyErr_Format( PyExc_TypeError,
               "%s is not a valid data member", CPyCppyy_PyUnicode_AsString( pyname ) );
            return 0;
         }

      // this is an address of an address (i.e. &myobj, with myobj of type MyObj*)
         return (void*)&pyobj->fObject;
      }

      PyErr_SetString( PyExc_ValueError, "invalid argument for AddressOf()" );
      return 0;
   }

   PyObject* _addressof_common( PyObject* dummy ) {
      if ( dummy == Py_None || dummy == gNullPtrObject ) {
         Py_INCREF( gNullPtrObject );
         return gNullPtrObject;
      }
      if ( !PyErr_Occurred() ) {
         PyObject* str = PyObject_Str( dummy );
         if ( str && CPyCppyy_PyUnicode_Check( str ) )
            PyErr_Format( PyExc_ValueError, "unknown object %s", PyBytes_AS_STRING( str ) );
         else
            PyErr_Format( PyExc_ValueError, "unknown object at %p", (void*)dummy );
         Py_XDECREF( str );
      }
      return 0;
   }

   PyObject* AddressOf( PyObject* dummy, PyObject* args )
   {
   // Return object proxy address as an indexable buffer.
      void* addr = GetObjectProxyAddress( dummy, args );
      if ( addr )
         return BufFac_t::Instance()->PyBuffer_FromMemory( (Long_t*)addr, sizeof(Long_t) );
      if ( ! addr && PyTuple_Size( args ) ) {
         Utility::GetBuffer( PyTuple_GetItem( args, 0 ), '*', 1, addr, kFALSE );
         if ( addr )
            return BufFac_t::Instance()->PyBuffer_FromMemory( (Long_t*)&addr, sizeof(Long_t) );
      }
      return 0;//_addressof_common( dummy );
   }

   PyObject* addressof( PyObject* dummy, PyObject* args )
   {
   // Return object proxy address as a value (cppyy-style), or the same for an array.
      void* addr = GetObjectProxyAddress( dummy, args );
      if ( addr )
         return PyLong_FromLong( *(Long_t*)addr );
      else if ( PyTuple_Size( args ) ) {
         PyErr_Clear();
         Utility::GetBuffer( PyTuple_GetItem( args, 0 ), '*', 1, addr, kFALSE );
         if ( addr ) return PyLong_FromLong( (Long_t)addr );
      }
      return _addressof_common( dummy );
   }

   PyObject* AsCObject( PyObject* dummy, PyObject* args )
   {
   // Return object proxy as an opaque CObject.
      void* addr = GetObjectProxyAddress( dummy, args );
      if ( addr )
         return CPyCppyy_PyCapsule_New( (void*)(*(Long_t*)addr), NULL, NULL );

      return 0;
   }

//----------------------------------------------------------------------------
   PyObject* BindObject_( void* addr, PyObject* pyname )
   {
   // Helper to factorize the common code between MakeNullPointer and BindObject.
      if ( ! CPyCppyy_PyUnicode_Check( pyname ) ) {     // name given as string
         PyObject* nattr = PyObject_GetAttr( pyname, PyStrings::gCppName );
         if ( ! nattr ) nattr = PyObject_GetAttr( pyname, PyStrings::gName );
         if ( nattr )                        // object is actually a class
            pyname = nattr;
         pyname = PyObject_Str( pyname );
         Py_XDECREF( nattr );
      } else {
         Py_INCREF( pyname );
      }

      Cppyy::TCppType_t klass = (Cppyy::TCppType_t)Cppyy::GetScope( CPyCppyy_PyUnicode_AsString( pyname ) );
      Py_DECREF( pyname );

      if ( ! klass ) {
         PyErr_SetString( PyExc_TypeError,
            "BindObject expects a valid class or class name as an argument" );
         return 0;
      }

      return BindCppObjectNoCast( addr, klass, kFALSE );
   }

//----------------------------------------------------------------------------
   PyObject* BindObject( PyObject*, PyObject* args )
   {
   // From a long representing an address or a PyCapsule/CObject, bind to a class.
      Py_ssize_t argc = PyTuple_GET_SIZE( args );
      if ( argc != 2 ) {
         PyErr_Format( PyExc_TypeError,
           "BindObject takes exactly 2 argumenst (" PY_SSIZE_T_FORMAT " given)", argc );
         return 0;
      }

   // try to convert first argument: either PyCapsule/CObject or long integer
      PyObject* pyaddr = PyTuple_GET_ITEM( args, 0 );
      void* addr = CPyCppyy_PyCapsule_GetPointer( pyaddr, NULL );
      if ( PyErr_Occurred() ) {
         PyErr_Clear();

         addr = PyLong_AsVoidPtr( pyaddr );
         if ( PyErr_Occurred() ) {
            PyErr_Clear();

         // last chance, perhaps it's a buffer/array (return from void*)
            int buflen = Utility::GetBuffer( PyTuple_GetItem( args, 0 ), '*', 1, addr, kFALSE );
            if ( ! addr || ! buflen ) {
               PyErr_SetString( PyExc_TypeError,
                  "BindObject requires a CObject or long integer as first argument" );
               return 0;
            }
         }
      }

      return BindObject_( addr, PyTuple_GET_ITEM( args, 1 ) );
   }

//----------------------------------------------------------------------------
   PyObject* MakeNullPointer( PyObject*, PyObject* args )
   {
   // Create an object of the given type point to NULL (historic note: this
   // function is older than BindObject(), which can be used instead).

      Py_ssize_t argc = PyTuple_GET_SIZE( args );
      if ( argc != 0 && argc != 1 ) {
         PyErr_Format( PyExc_TypeError,
            "MakeNullPointer takes at most 1 argument (" PY_SSIZE_T_FORMAT " given)", argc );
         return 0;
      }

   // no class given, use None as generic
      if ( argc == 0 ) {
         Py_INCREF( Py_None );
         return Py_None;
      }

      return BindObject_( 0, PyTuple_GET_ITEM( args, 0 ) );
   }

//----------------------------------------------------------------------------
   PyObject* SetMemoryPolicy( PyObject*, PyObject* args )
   {
   // Set the global memory policy, which affects object ownership when objects
   // are passed as function arguments.
      PyObject* policy = 0;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!" ), &PyInt_Type, &policy ) )
         return 0;

      Long_t l = PyInt_AS_LONG( policy );
      if ( TCallContext::SetMemoryPolicy( (TCallContext::ECallFlags)l ) ) {
         Py_INCREF( Py_None );
         return Py_None;
      }

      PyErr_Format( PyExc_ValueError, "Unknown policy %ld", l );
      return 0;
   }

//----------------------------------------------------------------------------
   PyObject* SetSignalPolicy( PyObject*, PyObject* args )
   {
   // Set the global signal policy, which determines whether a jmp address
   // should be saved to return to after a C++ segfault.
      PyObject* policy = 0;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!" ), &PyInt_Type, &policy ) )
         return 0;

      Long_t l = PyInt_AS_LONG( policy );
      if ( TCallContext::SetSignalPolicy( (TCallContext::ECallFlags)l ) ) {
         Py_INCREF( Py_None );
         return Py_None;
      }

      PyErr_Format( PyExc_ValueError, "Unknown policy %ld", l );
      return 0;
   }

//----------------------------------------------------------------------------
   PyObject* SetOwnership( PyObject*, PyObject* args )
   {
   // Set the ownership (True is python-owns) for the given object.
      ObjectProxy* pyobj = 0; PyObject* pykeep = 0;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!O!" ),
                &ObjectProxy_Type, (void*)&pyobj, &PyInt_Type, &pykeep ) )
         return 0;

      (Bool_t)PyLong_AsLong( pykeep ) ? pyobj->HoldOn() : pyobj->Release();

      Py_INCREF( Py_None );
      return Py_None;
   }

//----------------------------------------------------------------------------
   PyObject* AddSmartPtrType( PyObject*, PyObject* args )
   {
   // Add a smart pointer to the list of known smart pointer types.
      const char* type_name;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "s" ), &type_name ) )
         return nullptr;

      Cppyy::AddSmartPtrType( type_name );

      Py_RETURN_NONE;
   }

//----------------------------------------------------------------------------
   PyObject* SetTypePinning( PyObject*, PyObject* args )
   {
   // Add a pinning so that objects of type `derived' are interpreted as
   // objects of type `base'.
      CPyCppyyClass* derived = nullptr, *base = nullptr;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!O!" ),
                               &CPyCppyyType_Type, &derived,
                               &CPyCppyyType_Type, &base ) )
         return nullptr;
      gPinnedTypes.push_back( std::make_pair( derived->fCppType, base->fCppType ) );

      Py_RETURN_NONE;
   }

//----------------------------------------------------------------------------
   PyObject* IgnoreTypePinning( PyObject*, PyObject* args )
   {
   // Add an exception to the type pinning for objects of type `derived'.
      CPyCppyyClass* derived = nullptr;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!" ),
                               &CPyCppyyType_Type, &derived ) )
         return nullptr;
      gIgnorePinnings.push_back( derived->fCppType );

      Py_RETURN_NONE;
   }

//----------------------------------------------------------------------------
   PyObject* Cast( PyObject*, PyObject* args )
   {
   // Cast `obj' to type `type'.
      ObjectProxy* obj = nullptr;
      CPyCppyyClass* type = nullptr;
      if ( ! PyArg_ParseTuple( args, const_cast< char* >( "O!O!" ),
                               &ObjectProxy_Type, &obj,
                               &CPyCppyyType_Type, &type ) )
         return nullptr;
      // TODO: this misses an offset calculation, and reference type must not
      // be cast ...
      return BindCppObjectNoCast( obj->GetObject(), type->fCppType,
                                  obj->fFlags & ObjectProxy::kIsReference );
   }

} // unnamed namespace


//- data -----------------------------------------------------------------------
static PyMethodDef gCPyCppyyMethods[] = {
   { (char*) "CreateScopeProxy", (PyCFunction)CPyCppyy::CreateScopeProxy,
     METH_VARARGS, (char*) "cppyy internal function" },
   { (char*) "GetCppGlobal", (PyCFunction)CPyCppyy::GetCppGlobal,
     METH_VARARGS, (char*) "cppyy internal function" },
   { (char*) "LookupCppEntity", (PyCFunction)LookupCppEntity,
     METH_VARARGS, (char*) "cppyy internal function" },
   { (char*) "SetRootLazyLookup", (PyCFunction)SetRootLazyLookup,
     METH_VARARGS, (char*) "cppyy internal function" },
   { (char*) "MakeCppTemplateClass", (PyCFunction)MakeCppTemplateClass,
     METH_VARARGS, (char*) "cppyy internal function" },
   { (char*) "_DestroyPyStrings", (PyCFunction)CPyCppyy::DestroyPyStrings,
     METH_NOARGS, (char*) "cppyy internal function" },
   { (char*) "_ResetRootModule", (PyCFunction)RootModuleResetCallback,
     METH_NOARGS, (char*) "cppyy internal function" },
   { (char*) "AddressOf", (PyCFunction)AddressOf,
     METH_VARARGS, (char*) "Retrieve address of held object in a buffer" },
   { (char*) "addressof", (PyCFunction)addressof,
     METH_VARARGS, (char*) "Retrieve address of held object as a value" },
   { (char*) "AsCObject", (PyCFunction)AsCObject,
     METH_VARARGS, (char*) "Retrieve held object in a CObject" },
   { (char*) "BindObject", (PyCFunction)BindObject,
     METH_VARARGS, (char*) "Create an object of given type, from given address" },
   { (char*) "MakeNullPointer", (PyCFunction)MakeNullPointer,
     METH_VARARGS, (char*) "Create a NULL pointer of the given type" },
   { (char*) "SetMemoryPolicy", (PyCFunction)SetMemoryPolicy,
     METH_VARARGS, (char*) "Determines object ownership model" },
   { (char*) "SetSignalPolicy", (PyCFunction)SetSignalPolicy,
     METH_VARARGS, (char*) "Trap signals in safe mode to prevent interpreter abort" },
   { (char*) "SetOwnership", (PyCFunction)SetOwnership,
     METH_VARARGS, (char*) "Modify held C++ object ownership" },
   { (char*) "AddSmartPtrType", (PyCFunction)AddSmartPtrType,
     METH_VARARGS, (char*) "Add a smart pointer to the list of known smart pointer types" },
   { (char*) "SetTypePinning", (PyCFunction)SetTypePinning,
     METH_VARARGS, (char*) "Install a type pinning" },
   { (char*) "IgnoreTypePinning", (PyCFunction)IgnoreTypePinning,
     METH_VARARGS, (char*) "Don't pin the given type" },
   { (char*) "Cast", (PyCFunction)Cast,
     METH_VARARGS, (char*) "Cast the given object to the given type" },
   { NULL, NULL, 0, NULL }
};


#if PY_VERSION_HEX >= 0x03000000
struct module_state {
    PyObject *error;
};

#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

static int rootmodule_traverse( PyObject* m, visitproc visit, void* arg )
{
    Py_VISIT( GETSTATE( m )->error );
    return 0;
}

static int rootmodule_clear( PyObject* m )
{
    Py_CLEAR( GETSTATE( m )->error );
    return 0;
}


static struct PyModuleDef moduledef = {
   PyModuleDef_HEAD_INIT,
   "libcppyy",
   NULL,
   sizeof(struct module_state),
   gCPyCppyyMethods,
   NULL,
   rootmodule_traverse,
   rootmodule_clear,
   NULL
};


//----------------------------------------------------------------------------
#define CPYCPPYY_INIT_ERROR return NULL
extern "C" PyObject* PyInit_libcppyy()
#else
#define CPYCPPYY_INIT_ERROR return
extern "C" void initlibcppyy()
#endif
{
// Initialization of extension module libcppyy.

// load commonly used python strings
   if ( ! CPyCppyy::CreatePyStrings() )
      CPYCPPYY_INIT_ERROR;

// setup interpreter
   PyEval_InitThreads();

// prepare for lazyness
   PyObject* dict = PyDict_New();
#if PY_VERSION_HEX >= 0x03030000
   gDictLookupOrg = (dict_lookup_func)((PyDictObject*)dict)->ma_keys->dk_lookup;
#else
   gDictLookupOrg = (dict_lookup_func)((PyDictObject*)dict)->ma_lookup;
#endif
   Py_DECREF( dict );

// setup this module
#if PY_VERSION_HEX >= 0x03000000
   gThisModule = PyModule_Create( &moduledef );
#else
   gThisModule = Py_InitModule( const_cast< char* >( "libcppyy" ), gCPyCppyyMethods );
#endif
   if ( ! gThisModule )
      CPYCPPYY_INIT_ERROR;

// keep gThisModule, but do not increase its reference count even as it is borrowed,
// or a self-referencing cycle would be created

// Pythonizations ...
   PyObject* userPythonizations = PyDict_New();
   PyObject* gblList = PyList_New( 0 );
   PyDict_SetItemString( userPythonizations, "__global__", gblList );
   Py_DECREF( gblList );
   PyModule_AddObject( gThisModule, "UserPythonizations", userPythonizations );
   PyModule_AddObject( gThisModule, "UserExceptions",     PyDict_New() );
   PyModule_AddObject( gThisModule, "PythonizationScope", CPyCppyy_PyUnicode_FromString( "__global__" ) );

// inject meta type
   if ( ! Utility::InitProxy( gThisModule, &CPyCppyyType_Type, "CPyCppyyType" ) )
      CPYCPPYY_INIT_ERROR;

// inject object proxy type
   if ( ! Utility::InitProxy( gThisModule, &ObjectProxy_Type, "ObjectProxy" ) )
      CPYCPPYY_INIT_ERROR;

// inject method proxy type
   if ( ! Utility::InitProxy( gThisModule, &MethodProxy_Type, "MethodProxy" ) )
      CPYCPPYY_INIT_ERROR;

// inject template proxy type
   if ( ! Utility::InitProxy( gThisModule, &TemplateProxy_Type, "TemplateProxy" ) )
      CPYCPPYY_INIT_ERROR;

// inject property proxy type
   if ( ! Utility::InitProxy( gThisModule, &PropertyProxy_Type, "PropertyProxy" ) )
      CPYCPPYY_INIT_ERROR;

// inject custom data types
   if ( ! Utility::InitProxy( gThisModule, &TCustomFloat_Type, "Double" ) )
      CPYCPPYY_INIT_ERROR;

   if ( ! Utility::InitProxy( gThisModule, &TCustomInt_Type, "Long" ) )
      CPYCPPYY_INIT_ERROR;

   if ( ! Utility::InitProxy( gThisModule, &TCustomFloat_Type, "double" ) )
      CPYCPPYY_INIT_ERROR;

   if ( ! Utility::InitProxy( gThisModule, &TCustomInt_Type, "long" ) )
      CPYCPPYY_INIT_ERROR;

   if ( ! Utility::InitProxy( gThisModule, &TCustomInstanceMethod_Type, "InstanceMethod" ) )
      CPYCPPYY_INIT_ERROR;

   if ( ! Utility::InitProxy( gThisModule, &TTupleOfInstances_Type, "InstancesArray" ) )
      CPYCPPYY_INIT_ERROR;

   if ( ! Utility::InitProxy( gThisModule, &PyNullPtr_t_Type, "nullptr_t" ) )
      CPYCPPYY_INIT_ERROR;

// inject identifiable nullptr
   gNullPtrObject = (PyObject*)&_CPyCppyy_NullPtrStruct;
   Py_INCREF( gNullPtrObject );
   PyModule_AddObject( gThisModule, (char*)"nullptr", gNullPtrObject );

// policy labels
   PyModule_AddObject( gThisModule, (char*)"kMemoryHeuristics",
      PyInt_FromLong( (int)TCallContext::kUseHeuristics ) );
   PyModule_AddObject( gThisModule, (char*)"kMemoryStrict",
      PyInt_FromLong( (int)TCallContext::kUseStrict ) );
   PyModule_AddObject( gThisModule, (char*)"kSignalFast",
      PyInt_FromLong( (int)TCallContext::kFast ) );
   PyModule_AddObject( gThisModule, (char*)"kSignalSafe",
      PyInt_FromLong( (int)TCallContext::kSafe ) );

// gbl namespace is injected in cppyy.py

#if PY_VERSION_HEX >= 0x03000000
   Py_INCREF( gThisModule );
   return gThisModule;
#endif
}
