// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "CPyCppyyHelpers.h"
#include "CPyCppyyType.h"
#include "ObjectProxy.h"
#include "MethodProxy.h"
#include "TemplateProxy.h"
#include "PropertyProxy.h"
#include "Pythonize.h"
#include "TMethodHolder.h"
#include "TConstructorHolder.h"
#include "TClassMethodHolder.h"
#include "TFunctionHolder.h"
#include "TSetItemHolder.h"
#include "TTupleOfInstances.h"
#include "Utility.h"

// Standard
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <vector>


//- data _______________________________________________________________________
namespace CPyCppyy {
   extern PyObject* gThisModule;

// TODO: move this to Cppyy.cxx (if possible) (and gPinnedTypes should be a hashmap)
   extern std::vector<std::pair<Cppyy::TCppType_t, Cppyy::TCppType_t> > gPinnedTypes;
   extern std::vector<Cppyy::TCppType_t> gIgnorePinnings;
}

namespace {

// to prevent having to walk scopes, track python classes by ROOT class
   typedef std::map< Cppyy::TCppScope_t, PyObject* > PyClassMap_t;
   PyClassMap_t gPyClasses;

// helper for creating new ROOT python types
   PyObject* CreateNewROOTPythonClass( const std::string& name, PyObject* pybases )
   {
   // Create a new python shadow class with the required hierarchy and meta-classes.
      Py_XINCREF( pybases );
      if ( ! pybases ) {
         pybases = PyTuple_New( 1 );
         Py_INCREF( (PyObject*)(void*)&CPyCppyy::ObjectProxy_Type );
         PyTuple_SET_ITEM( pybases, 0, (PyObject*)(void*)&CPyCppyy::ObjectProxy_Type );
      }

      PyObject* pymetabases = PyTuple_New( PyTuple_GET_SIZE( pybases ) );
      for ( int i = 0; i < PyTuple_GET_SIZE( pybases ); ++i ) {
         PyObject* btype = (PyObject*)Py_TYPE( PyTuple_GetItem( pybases, i ) );
         Py_INCREF( btype );
         PyTuple_SET_ITEM( pymetabases, i, btype );
      }

      PyObject* args = Py_BuildValue( (char*)"sO{}", (name+"_meta").c_str(), pymetabases );
      Py_DECREF( pymetabases );

      PyObject* pymeta = PyType_Type.tp_new( &CPyCppyy::CPyCppyyType_Type, args, NULL );
      Py_DECREF( args );
      if ( ! pymeta ) {
         PyErr_Print();
         Py_DECREF( pybases );
         return 0;
      }

      args = Py_BuildValue( (char*)"sO{}", Cppyy::GetName(name).c_str(), pybases );
      PyObject* pyclass = ((PyTypeObject*)pymeta)->tp_new( (PyTypeObject*)pymeta, args, NULL );
      Py_DECREF( args );
      Py_DECREF( pymeta );

      Py_DECREF( pybases );

      return pyclass;
   }

   inline void AddPropertyToClass1(
      PyObject* pyclass, CPyCppyy::PropertyProxy* property, Bool_t isStatic )
   {
   // allow access at the instance level
      PyObject_SetAttrString( pyclass,
         const_cast< char* >( property->GetName().c_str() ), (PyObject*)property );

   // allow access at the class level (always add after setting instance level)
      if ( isStatic ) {
         PyObject_SetAttrString( (PyObject*)Py_TYPE(pyclass),
            const_cast< char* >( property->GetName().c_str() ), (PyObject*)property );
      }
    }

   void AddPropertyToClass( PyObject* pyclass,
         Cppyy::TCppScope_t scope, Cppyy::TCppIndex_t idata )
   {
      CPyCppyy::PropertyProxy* property = CPyCppyy::PropertyProxy_New( scope, idata );
      AddPropertyToClass1( pyclass, property, Cppyy::IsStaticData( scope, idata ) );
      Py_DECREF( property );
   }

   void AddPropertyToClass( PyObject* pyclass,
         Cppyy::TCppScope_t scope, const std::string& name, void* address )
   {
      CPyCppyy::PropertyProxy* property =
         CPyCppyy::PropertyProxy_NewConstant( scope, name, address );
      AddPropertyToClass1( pyclass, property, kTRUE );
      Py_DECREF( property );
   }


} // unnamed namespace


//- helpers --------------------------------------------------------------------
namespace {

   using namespace CPyCppyy;

   inline void AddToGlobalScope(
      const char* label, const char* /* hdr */, void* obj, Cppyy::TCppType_t klass )
   {
   // Bind the given object with the given class in the global scope with the
   // given label for its reference.
      PyModule_AddObject( gThisModule, const_cast< char* >( label ),
         CPyCppyy::BindCppObjectNoCast( obj, klass ) );
   }

} // unnamed namespace


//- public functions ---------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Collect methods and data for the given scope, and add them to the given python
/// proxy object.

static int BuildScopeProxyDict( Cppyy::TCppScope_t scope, PyObject* pyclass ) {
// some properties that'll affect building the dictionary
   Bool_t isNamespace = Cppyy::IsNamespace( scope );
   Bool_t hasConstructor = kFALSE;

// load all public methods and data members
   typedef std::vector< PyCallable* > Callables_t;
   typedef std::map< std::string, Callables_t > CallableCache_t;
   CallableCache_t cache;

// bypass custom __getattr__ for efficiency
   getattrofunc oldgetattro = Py_TYPE(pyclass)->tp_getattro;
   Py_TYPE(pyclass)->tp_getattro = PyType_Type.tp_getattro;

// functions in namespaces are properly found through lazy lookup, so do not
// create them until needed (the same is not true for data members)
   const Cppyy::TCppIndex_t nMethods =
      Cppyy::IsNamespace( scope ) ? 0 : Cppyy::GetNumMethods( scope );
   for ( Cppyy::TCppIndex_t imeth = 0; imeth < nMethods; ++imeth ) {
      Cppyy::TCppMethod_t method = Cppyy::GetMethod( scope, imeth );

   // process the method based on its name
      std::string mtName = Cppyy::GetMethodName( method );

   // special case trackers
      Bool_t setupSetItem = kFALSE;
      Bool_t isConstructor = Cppyy::IsConstructor( method );

   // filter empty names (happens for namespaces, is bug?)
      if ( mtName == "" )
         continue;

   // filter C++ destructors
      if ( mtName[0] == '~' )
         continue;

   // translate operators
      mtName = Utility::MapOperatorName( mtName, Cppyy::GetMethodNumArgs( method ) );

   // operator[]/() returning a reference type will be used for __setitem__
      if ( mtName == "__call__" || mtName == "__getitem__" ) {
         const std::string& qual_return = Cppyy::ResolveName( Cppyy::GetMethodResultType( method ) );
         if ( qual_return.find( "const", 0, 5 ) == std::string::npos ) {
            const std::string& cpd = Utility::Compound( qual_return );
            if ( ! cpd.empty() && cpd[ cpd.size() - 1 ] == '&' ) {
               setupSetItem = kTRUE;
            }
         }
      }

   // decide on method type: member or static (which includes globals)
      Bool_t isStatic = Cppyy::IsStaticMethod( method );

   // template members; handled by adding a dispatcher to the class
      std::string tmplName = "";
      if ( ! (isNamespace || isStatic || isConstructor) && mtName[mtName.size()-1] == '>' ) {
         tmplName = mtName.substr( 0, mtName.find('<') );
      // TODO: the following is incorrect if both base and derived have the same
      // templated method (but that is an unlikely scenario anyway)
         PyObject* attr = PyObject_GetAttrString( pyclass, const_cast< char* >( tmplName.c_str() ) );
         if ( ! TemplateProxy_Check( attr ) ) {
            PyErr_Clear();
            TemplateProxy* pytmpl = TemplateProxy_New( tmplName, pyclass );
            if ( MethodProxy_Check( attr ) ) pytmpl->AddOverload( (MethodProxy*)attr );
            PyObject_SetAttrString(
               pyclass, const_cast< char* >( tmplName.c_str() ), (PyObject*)pytmpl );
            Py_DECREF( pytmpl );
         }
         Py_XDECREF( attr );
      // continue processing to actually add the method so that the proxy can find
      // it on the class when called explicitly
      }

   // public methods are normally visible, private methods are mangled python-wise
   // note the overload implications which are name based, and note that rootcint
   // does not create the interface methods for private/protected methods ...
      if ( ! Cppyy::IsPublicMethod( method ) ) {
         if ( isConstructor )                // don't expose private ctors
            continue;
         else {                              // mangle private methods
         //            const std::string& clName = TClassEdit::ShortType(
         //               Cppyy::GetFinalName( scope ).c_str(), TClassEdit::kDropAlloc );
            const std::string& clName = Cppyy::GetFinalName( scope );
            mtName = "_" + clName + "__" + mtName;
         }
      }

   // construct the holder
      PyCallable* pycall = 0;
      if ( isStatic )                        // class method
         pycall = new TClassMethodHolder( scope, method );
      else if ( isNamespace )                // free function
         pycall = new TFunctionHolder( scope, method );
      else if ( isConstructor ) {            // constructor
         pycall = new TConstructorHolder( scope, method );
         mtName = "__init__";
         hasConstructor = kTRUE;
      } else                                 // member function
         pycall = new TMethodHolder( scope, method );

   // lookup method dispatcher and store method
      Callables_t& md = (*(cache.insert(
         std::make_pair( mtName, Callables_t() ) ).first)).second;
      md.push_back( pycall );

   // special case for operator[]/() that returns by ref, use for getitem/call and setitem
      if ( setupSetItem ) {
         Callables_t& setitem = (*(cache.insert(
            std::make_pair( std::string( "__setitem__" ), Callables_t() ) ).first)).second;
         setitem.push_back( new TSetItemHolder( scope, method ) );
      }

   // special case for templates, add another call for the template name
      if ( ! tmplName.empty() ) {
         PyObject* attr = PyObject_GetAttrString( pyclass, const_cast< char* >( tmplName.c_str() ) );
         ((TemplateProxy*)attr)->AddTemplate( pycall->Clone() );
         Py_DECREF( attr );
      }
   }

// add a pseudo-default ctor, if none defined
   if ( ! isNamespace && ! hasConstructor )
      cache[ "__init__" ].push_back( new TConstructorHolder( scope, (Cppyy::TCppMethod_t)0 ) );

// add the methods to the class dictionary
   for ( CallableCache_t::iterator imd = cache.begin(); imd != cache.end(); ++imd ) {
   // in order to prevent removing templated editions of this method (which were set earlier,
   // above, as a different proxy object), we'll check and add this method flagged as a generic
   // one (to be picked up by the templated one as appropriate) if a template exists
      PyObject* attr = PyObject_GetAttrString( pyclass, const_cast< char* >( imd->first.c_str() ) );
      if ( TemplateProxy_Check( attr ) ) {
      // template exists, supply it with the non-templated method overloads
         for ( Callables_t::iterator cit = imd->second.begin(); cit != imd->second.end(); ++cit )
            ((TemplateProxy*)attr)->AddOverload( *cit );
      } else {
         if ( ! attr ) PyErr_Clear();
      // normal case, add a new method
         MethodProxy* method = MethodProxy_New( imd->first, imd->second );
         PyObject_SetAttrString(
            pyclass, const_cast< char* >( method->GetName().c_str() ), (PyObject*)method );
         Py_DECREF( method );
      }

      Py_XDECREF( attr );     // could have be found in base class or non-existent
   }

// collect enums; this must happen before data members, so that we can check on their existence
/* TODO: enums in Cppyy.cxx
   TClass* klass = TClass::GetClass( Cppyy::GetFinalName( scope ).c_str() );
   TList* enums = klass->GetListOfEnums();
   TIter ienum( enums );
   TEnum* e = 0;
   while ( (e = (TEnum*)ienum.Next()) ) {
      const TSeqCollection* seq = e->GetConstants();
      for ( Int_t i = 0; i < seq->GetSize(); i++ ) {
         TEnumConstant* ec = (TEnumConstant*)seq->At( i );
         AddPropertyToClass( pyclass, scope, ec->GetName(), ec->GetAddress() );
      }
   }
*/

// collect data members
   const Cppyy::TCppIndex_t nDataMembers = Cppyy::GetNumDatamembers( scope );
   for ( Cppyy::TCppIndex_t idata = 0; idata < nDataMembers; ++idata ) {
   // allow only public members
      if ( ! Cppyy::IsPublicData( scope, idata ) )
         continue;

   // enum datamembers (this in conjunction with previously collected enums above)
      if ( Cppyy::IsEnumData( scope, idata ) && Cppyy::IsStaticData( scope, idata ) ) {
      // some implementation-specific data members have no address: ignore them
         if ( ! Cppyy::GetDatamemberOffset( scope, idata ) )
            continue;

      // two options: this is a static variable, or it is the enum value, the latter
      // already exists, so check for it and move on if set
         PyObject* eset = PyObject_GetAttrString( pyclass,
            const_cast<char*>( Cppyy::GetDatamemberName( scope, idata ).c_str()) );
         if ( eset ) {
            Py_DECREF( eset );
            continue;
         }

         PyErr_Clear();

      // it could still be that this is an anonymous enum, which is not in the list
      // provided by the class
         if ( strstr( Cppyy::GetDatamemberType( scope, idata ).c_str(), "(anonymous)" ) != 0 ) {
            AddPropertyToClass( pyclass, scope, idata );
            continue;
         }
      }

   // properties (aka public (static) data members)
      AddPropertyToClass( pyclass, scope, idata );
   }

// restore custom __getattr__
   Py_TYPE(pyclass)->tp_getattro = oldgetattro;

// all ok, done
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Build a tuple of python shadow classes of all the bases of the given 'klass'.

static PyObject* BuildCppClassBases( Cppyy::TCppType_t klass )
{
   size_t nbases = Cppyy::GetNumBases( klass );

// collect bases while removing duplicates
   std::vector< std::string > uqb;
   uqb.reserve( nbases );

   for ( size_t ibase = 0; ibase < nbases; ++ibase ) {
      const std::string& name = Cppyy::GetBaseName( klass, ibase );
      if ( std::find( uqb.begin(), uqb.end(), name ) == uqb.end() ) {
         uqb.push_back( name );
      }
   }

// allocate a tuple for the base classes, special case for first base
   nbases = uqb.size();

   PyObject* pybases = PyTuple_New( nbases ? nbases : 1 );
   if ( ! pybases )
      return 0;

// build all the bases
   if ( nbases == 0 ) {
      Py_INCREF( (PyObject*)(void*)&ObjectProxy_Type );
      PyTuple_SET_ITEM( pybases, 0, (PyObject*)(void*)&ObjectProxy_Type );
   } else {
      for ( std::vector< std::string >::size_type ibase = 0; ibase < nbases; ++ibase ) {
         PyObject* pyclass = CreateScopeProxy( uqb[ ibase ] );
         if ( ! pyclass ) {
            Py_DECREF( pybases );
            return 0;
         }

         PyTuple_SET_ITEM( pybases, ibase, pyclass );
      }

   // special case, if true python types enter the hierarchy, make sure that
   // the first base seen is still the ObjectProxy_Type
      if ( ! PyObject_IsSubclass( PyTuple_GET_ITEM( pybases, 0 ), (PyObject*)&ObjectProxy_Type ) ) {
         PyObject* newpybases = PyTuple_New( nbases + 1 );
         Py_INCREF( (PyObject*)(void*)&ObjectProxy_Type );
         PyTuple_SET_ITEM( newpybases, 0, (PyObject*)(void*)&ObjectProxy_Type );
         for ( int ibase = 0; ibase < (int)nbases; ++ibase ) {
             PyObject* pyclass = PyTuple_GET_ITEM( pybases, ibase );
             Py_INCREF( pyclass );
             PyTuple_SET_ITEM( newpybases, ibase + 1, pyclass );
         }
         Py_DECREF( pybases );
         pybases = newpybases;
      }
   }

   return pybases;
}

////////////////////////////////////////////////////////////////////////////////
/// Retrieve scope proxy from the known ones.

PyObject* CPyCppyy::GetScopeProxy( Cppyy::TCppScope_t scope )
{
   PyClassMap_t::iterator pci = gPyClasses.find( scope );
   if ( pci != gPyClasses.end() ) {
      PyObject* pyclass = PyWeakref_GetObject( pci->second );
      if ( pyclass ) {
         Py_INCREF( pyclass );
         return pyclass;
      }
   }

   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Convenience function with a lookup first through the known existing proxies.

PyObject* CPyCppyy::CreateScopeProxy( Cppyy::TCppScope_t scope )
{
   PyObject* pyclass = GetScopeProxy( scope );
   if ( pyclass )
      return pyclass;

   return CreateScopeProxy( Cppyy::GetScopedFinalName( scope ) );
}

////////////////////////////////////////////////////////////////////////////////
/// Build a python shadow class for the named C++ class.

PyObject* CPyCppyy::CreateScopeProxy( PyObject*, PyObject* args )
{
   std::string cname = CPyCppyy_PyUnicode_AsString( PyTuple_GetItem( args, 0 ) );
   if ( PyErr_Occurred() )
      return nullptr;

   return CreateScopeProxy( cname );
}

////////////////////////////////////////////////////////////////////////////////
/// Build a python shadow class for the named C++ class.

PyObject* CPyCppyy::CreateScopeProxy( const std::string& scope_name, PyObject* parent )
{
   if ( scope_name.empty() || scope_name == "std" ) {
   // special cases, as gbl and gbl.std are defined in cppyy.py
      PyObject* mods = PyImport_GetModuleDict();
      PyObject* gbl = PyDict_GetItemString( mods, "cppyy.gbl" );
      if ( gbl ) {
         if ( scope_name.empty() ) {
            Py_INCREF( gbl );
            return gbl;
         } else
            return PyObject_GetAttrString( gbl, "std" );
      }
      PyErr_SetString( PyExc_SystemError, "could not locate global namespace" );
      return nullptr;
   }

// force building of the class if a parent is specified (prevents loops)
   Bool_t force = parent != 0;

// working copy
   std::string name = scope_name;

// determine complete scope name, if a python parent has been given
   std::string scName = "";
   if ( parent ) {
      PyObject* pyparent = PyObject_GetAttr( parent, PyStrings::gCppName );
      if ( ! pyparent ) pyparent = PyObject_GetAttr( parent, PyStrings::gName );
      if ( ! pyparent ) {
         PyErr_Format( PyExc_SystemError, "given scope has no name for %s", name.c_str() );
         return 0;
      }

   // should be a string
      scName = CPyCppyy_PyUnicode_AsString( pyparent );
      Py_DECREF( pyparent );
      if ( PyErr_Occurred() )
         return 0;

   // accept this parent scope and use it's name for prefixing
      Py_INCREF( parent );
   }

// retrieve ROOT class (this verifies name, and is therefore done first)
   const std::string& lookup = parent ? (scName+"::"+name) : name;
   Cppyy::TCppScope_t klass = Cppyy::GetScope( lookup );

/* TODO: hide this direct gInterpreter access ...
   if ( ! (Bool_t)klass && gInterpreter->CheckClassTemplate( lookup.c_str() ) ) {
   // a "naked" templated class is requested: return callable proxy for instantiations
      PyObject* pytcl = PyObject_GetAttr( gThisModule, PyStrings::gTemplate );
      PyObject* pytemplate = PyObject_CallFunction(
         pytcl, const_cast< char* >( "s" ), const_cast< char* >( lookup.c_str() ) );
      Py_DECREF( pytcl );

   // cache the result
      PyObject_SetAttrString( parent ? parent : gThisModule, (char*)name.c_str(), pytemplate );

   // done, next step should be a call into this template
      Py_XDECREF( parent );
      return pytemplate;
   }
*/

   if ( ! (Bool_t)klass ) {   // if so, all options have been exhausted: it doesn't exist as such
      if ( ! parent && scope_name.find( "ROOT::" ) == std::string::npos ) { // not already in ROOT::
      // final attempt, for convenience, the "ROOT" namespace isn't required, try again ...
         klass = Cppyy::GetScope( "ROOT::"+scope_name );
         if ( (Bool_t)klass ) {
            PyObject* rtns = PyObject_GetAttr( gThisModule, PyStrings::gThisModule );
            PyObject* pyclass = CreateScopeProxy( scope_name, rtns );
            Py_DECREF( rtns );
            return pyclass;
         }
      }

      PyErr_Format( PyExc_TypeError, "requested class \'%s\' does not exist", lookup.c_str() );
      Py_XDECREF( parent );
      return 0;
   }

// locate class by ID, if possible, to prevent parsing scopes/templates anew
   PyObject* pyscope = GetScopeProxy( klass );
   if ( pyscope ) {
      if ( parent ) PyObject_SetAttrString( parent, (char*)scope_name.c_str(), pyscope );
      return pyscope;
   }

// locate the parent, if necessary, for building the class if not specified
   std::string::size_type last = 0;
   if ( ! parent ) {
   // need to deal with template paremeters that can have scopes themselves
      Int_t tpl_open = 0;
      for ( std::string::size_type pos = 0; pos < name.size(); ++pos ) {
         std::string::value_type c = name[ pos ];

      // count '<' and '>' to be able to skip template contents
         if ( c == '<' )
            ++tpl_open;
         else if ( c == '>' )
            --tpl_open;

      // by only checking for "::" the last part (class name) is dropped
         else if ( tpl_open == 0 &&\
              c == ':' && pos+1 < name.size() && name[ pos+1 ] == ':' ) {
         // found a new scope part
            const std::string& part = name.substr( last, pos-last );

            PyObject* next = PyObject_GetAttrString(
               parent ? parent : gThisModule, const_cast< char* >( part.c_str() ) );

            if ( ! next ) {           // lookup failed, try to create it
               PyErr_Clear();
               next = CreateScopeProxy( part, parent );
            }
            Py_XDECREF( parent );

            if ( ! next )             // create failed, give up
               return 0;

         // found scope part
            parent = next;

         // done with part (note that pos is moved one ahead here)
            last = pos+2; ++pos;
         }

      }

      if ( parent && !CPyCppyyType_Check( parent ) ) {
      // Special case: parent found is not one of ours (it's e.g. a pure Python module), so
      // continuing would fail badly. One final lookup, then out of here ...
         std::string unscoped = scope_name.substr( last, std::string::npos );
         return PyObject_GetAttrString( parent, unscoped.c_str() );
      }
   }

// use global scope if no inner scope found
   if ( ! parent ) {
      parent = gThisModule;
      Py_INCREF( parent );
   }

// use actual class name for binding
   const std::string& actual = Cppyy::GetFinalName( klass );

// first try to retrieve an existing class representation
   PyObject* pyactual = CPyCppyy_PyUnicode_FromString( Cppyy::GetName(actual).c_str() );
   PyObject* pyclass = force ? 0 : PyObject_GetAttr( parent, pyactual );

   Bool_t bClassFound = pyclass ? kTRUE : kFALSE;

// build if the class does not yet exist
   if ( ! pyclass ) {
   // ignore error generated from the failed lookup
      PyErr_Clear();

   // construct the base classes
      PyObject* pybases = BuildCppClassBases( klass );
      if ( pybases != 0 ) {
      // create a fresh Python class, given bases, name, and empty dictionary
         pyclass = CreateNewROOTPythonClass( actual, pybases );
         Py_DECREF( pybases );
      }

   // fill the dictionary, if successful
      if ( pyclass != 0 ) {
         if ( BuildScopeProxyDict( klass, pyclass ) != 0 ) {
         // something failed in building the dictionary
            Py_DECREF( pyclass );
            pyclass = 0;
         } else {
            PyObject_SetAttr( parent, pyactual, pyclass );
         }
      }

   }

   if ( pyclass && name != actual )     // class exists, but is typedef-ed: simply map reference
      PyObject_SetAttrString( parent, const_cast< char* >( name.c_str() ), pyclass );

   if ( pyclass && ! bClassFound ) {
   // store a ref from ROOT TClass to new python class
      gPyClasses[ klass ] = PyWeakref_NewRef( pyclass, NULL );

   // add a ref in the class to its scope
      PyObject_SetAttrString( pyclass, "__scope__", CPyCppyy_PyUnicode_FromString( scName.c_str() ) );
   }

   // add __cppname__ to keep the C++ name of the class/scope
   PyObject_SetAttr( pyclass, PyStrings::gCppName, CPyCppyy_PyUnicode_FromString( actual.c_str() ) );

   // add __module__  (see https://docs.python.org/3/c-api/typeobj.html#c.PyTypeObject.tp_name) 
   std::string module;
   if( parent == gThisModule) {
      module = "cppyy";
   } else {
      PyObject* _name_ =  PyObject_GetAttr(parent, PyStrings::gName);
      PyObject* _module_ =  PyObject_GetAttr(parent, PyStrings::gModule);
      if(_module_) {
         module = CPyCppyy_PyUnicode_AsString(_module_);
         module += ".";
         Py_DECREF(_module_);
      }
      if(_name_) {
         module += CPyCppyy_PyUnicode_AsString(_name_);
         Py_DECREF(_name_);
      }
   }
   PyObject_SetAttr( pyclass, PyStrings::gModule, CPyCppyy_PyUnicode_FromString( module.c_str()) );

   Py_DECREF( pyactual );
   Py_DECREF( parent );


   if ( ! bClassFound ) {               // add python-style features to newly minted classes
      if ( ! Pythonize( pyclass, actual ) ) {
         Py_XDECREF( pyclass );
         pyclass = 0;
      }
   }


   if ( pyclass && actual != "ROOT" ) {
   // add to sys.modules to allow importing from this module
      std::string pyfullname = lookup;
      std::string::size_type pos = pyfullname.find( "::" );
      while ( pos != std::string::npos ) {
         pyfullname = pyfullname.replace( pos, 2, "." );
         pos = pyfullname.find( "::", pos );
      }
      PyObject* modules = PySys_GetObject( const_cast<char*>("modules") );
      if ( modules && PyDict_Check( modules) ) {
         PyDict_SetItemString( modules,
            const_cast<char*>(("ROOT."+pyfullname).c_str()), pyclass );
      }
   }

// all done
   return pyclass;
}

////////////////////////////////////////////////////////////////////////////////
/// get the requested name

PyObject* CPyCppyy::GetCppGlobal( PyObject*, PyObject* args )
{
   std::string ename = CPyCppyy_PyUnicode_AsString( PyTuple_GetItem( args, 0 ) );

   if ( PyErr_Occurred() )
      return 0;

   return GetCppGlobal( ename );
}

////////////////////////////////////////////////////////////////////////////////
/// try named global variable/enum (first ROOT, then Cling: sync is too slow)

PyObject* CPyCppyy::GetCppGlobal( const std::string& name )
{
/* TODO: this should all be in Cppyy.cxx, no?
   Cppyy::TCppIndex_t idata = Cppyy::GetDatamemberIndex( Cppyy::gGlobalScope, name );
   if ( 0 <= idata )
      return (PyObject*)PropertyProxy_New( Cppyy::gGlobalScope, idata );

// still here ... try functions (sync has been fixed, so is okay)
   const std::vector< Cppyy::TCppMethod_t >& methods =
      Cppyy::GetMethodsFromName( Cppyy::gGlobalScope, name );
   if ( ! methods.empty() ) {
      std::vector< PyCallable* > overloads;
      for ( auto method : methods )
         overloads.push_back( new TFunctionHolder( Cppyy::gGlobalScope, method ) );
      return (PyObject*)MethodProxy_New( name, overloads );
   }

// allow lookup into std as if global (historic)
   TDataMember* dm = TClass::GetClass( "std" )->GetDataMember( name.c_str() );
   if ( dm ) {
      Cppyy::TCppType_t klass = Cppyy::GetScope( dm->GetTrueTypeName() );
      return BindCppObjectNoCast( (void*)dm->GetOffset(), klass, kFALSE );
   }
*/
// nothing found
   PyErr_Format( PyExc_LookupError, "no such global: %s", name.c_str() );
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// only known or knowable objects will be bound (null object is ok)

PyObject* CPyCppyy::BindCppObjectNoCast(
      Cppyy::TCppObject_t address, Cppyy::TCppType_t klass, Bool_t isRef, Bool_t isValue ) {
   if ( ! klass ) {
      PyErr_SetString( PyExc_TypeError, "attempt to bind ROOT object w/o class" );
      return 0;
   }

// retrieve python class
   PyObject* pyclass = CreateScopeProxy( klass );
   if ( ! pyclass )
      return 0;                    // error has been set in CreateScopeProxy

// instantiate an object of this class
   PyObject* args = PyTuple_New(0);
   ObjectProxy* pyobj =
      (ObjectProxy*)((PyTypeObject*)pyclass)->tp_new( (PyTypeObject*)pyclass, args, NULL );
   Py_DECREF( args );
   Py_DECREF( pyclass );

// bind, register and return if successful
   if ( pyobj != 0 ) { // fill proxy value?
   // TODO: take flags directly instead of separate Bool_t args
      unsigned flags = (isRef ? ObjectProxy::kIsReference : 0) | (isValue ? ObjectProxy::kIsValue : 0);
      pyobj->Set( address, (ObjectProxy::EFlags)flags );
   }

// successful completion
   return (PyObject*)pyobj;
}

////////////////////////////////////////////////////////////////////////////////
/// if the object is a null pointer, return a typed one (as needed for overloading)

PyObject* CPyCppyy::BindCppObject( Cppyy::TCppObject_t address, Cppyy::TCppType_t klass, Bool_t isRef )
{
   if ( ! address )
      return BindCppObjectNoCast( address, klass, kFALSE );

// only known or knowable objects will be bound
   if ( ! klass ) {
      PyErr_SetString( PyExc_TypeError, "attempt to bind ROOT object w/o class" );
      return 0;
   }

// get actual class for recycling checking and/or downcasting
   Cppyy::TCppType_t clActual = isRef ? 0 : Cppyy::GetActualClass( klass, address );

// TODO: write own memory regulator like in PyPy
   void* object = 0;
#if 0
// obtain pointer to TObject base class (if possible) for memory mgmt; this is
// done before downcasting, as upcasting from the current class may be easier and
// downcasting is unnecessary if the python side object gets recycled by the
// memory regulator
   TObject* object = 0;
   static Cppyy::TCppScope_t sTObjectScope = Cppyy::GetScope( "TObject" );
   if ( ! isRef && Cppyy::IsSubtype( klass, sTObjectScope) ) {
      object = (TObject*)((Long_t)address + \
         Cppyy::GetBaseOffset( klass, sTObjectScope, address, 1 /* up-cast */ ) );

   // use the old reference if the object already exists
      PyObject* oldPyObject = TMemoryRegulator::RetrieveObject( object, clActual ? clActual : klass );
      if ( oldPyObject )
         return oldPyObject;
   }
#endif

// downcast to real class for object returns
   if ( clActual && klass != clActual ) {
      ptrdiff_t offset = Cppyy::GetBaseOffset(
         clActual, klass, address, -1 /* down-cast */, true /* report errors */ );
      if ( offset != -1 ) {   // may fail if clActual not fully defined
         address = (void*)((Long_t)address + offset);
         klass = clActual;
      }
   }


// check if type is pinned
   Bool_t ignore_pin = std::find(
     gIgnorePinnings.begin(), gIgnorePinnings.end(), klass ) != gIgnorePinnings.end();

   if ( ! ignore_pin ) {
      for ( auto it = gPinnedTypes.cbegin(); it != gPinnedTypes.cend(); ++it ) {
         if ( klass == std::get<0>(*it) || Cppyy::IsSubtype( klass, std::get<0>(*it) ) )
            klass = std::get<1>(*it);
     }
   }

// actual binding
   ObjectProxy* pyobj = (ObjectProxy*)BindCppObjectNoCast( address, klass, isRef );

/* TODO: own memory regulator
// memory management, for TObject's only (for referenced objects, it is assumed
// that the (typically global) reference itself is zeroed out (or replaced) on
// destruction; it can't thus be reliably zeroed out from the python side)
   if ( object && !(pyobj->fFlags & ObjectProxy::kIsReference) ) {
      TMemoryRegulator::RegisterObject( pyobj, object );
    }
*/

// completion (returned object may be zero w/ a python exception set)
   return (PyObject*)pyobj;
}

////////////////////////////////////////////////////////////////////////////////
/// TODO: this function exists for symmetry; need to figure out if it's useful

PyObject* CPyCppyy::BindCppObjectArray(
      Cppyy::TCppObject_t address, Cppyy::TCppType_t klass, Int_t size ) {
   return TTupleOfInstances_New( address, klass, size );
}


////////////////////////////////////////////////////////////////////////////////
/// gbl == 0 means global does not exist (rather than gbl is NULL pointer)

PyObject* CPyCppyy::BindCppGlobal( void* gbl )
{
   Py_INCREF( Py_None );
   return Py_None;
/* TODO: move to Cppyy.cxx
   if ( ! gbl || strcmp(gbl->GetName(), "") == 0 ) {
      Py_INCREF( Py_None );
      return Py_None;
   }

// determine type and cast as appropriate
   Cppyy::TCppType_t klass = Cppyy::GetScope( gbl->GetTypeName() );
   if ( klass != 0 ) {
   // handle array of objects
      if ( gbl->GetArrayDim() == 1 ) {
         return BindCppObjectArray( (void*)gbl->GetAddress(), klass, gbl->GetMaxIndex(0) );
      } else if ( gbl->GetArrayDim() ) {
         PyErr_SetString( PyExc_NotImplementedError,
            "larger than 1D arrays of objects not supported" );
         return 0;
      }

   // special case where there should be no casting:
   // TODO: WORK HERE ... restore cast
      //if ( klass->InheritsFrom( "ios_base" ) )
         //return BindCppObjectNoCast( (void*)gbl->GetAddress(), klass );

   // pointer types are bound "by-reference"
      if ( Utility::Compound( gbl->GetFullTypeName() ) != "" )
         return BindCppObject( (void*)gbl->GetAddress(), klass, kTRUE );
   }

   if ( gbl->GetAddress() &&       // check for enums and consts
        (unsigned long)gbl->GetAddress() != (unsigned long)-1 && // Cling (??)
        ( gInterpreter->ClassInfo_IsEnum( gbl->GetTypeName() ) ) ) {
      return PyInt_FromLong( (long)*((int*)gbl->GetAddress()) );
   }

// no class and no enum: for built-in types, to ensure setability
   PyObject* result = (PyObject*)PropertyProxy_New(
      Cppyy::gGlobalScope, Cppyy::GetDatamemberIndex( Cppyy::gGlobalScope, gbl->GetName() ) );
   return result;
*/
}