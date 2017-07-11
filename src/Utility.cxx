// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "Utility.h"
#include "ObjectProxy.h"
#include "MethodProxy.h"
#include "TFunctionHolder.h"
#include "TCustomPyTypes.h"
#include "TemplateProxy.h"
#include "CPyCppyyHelpers.h"
#include "PyCallable.h"

// Standard
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <list>
#include <mutex>
#include <sstream>
#include <utility>


//- data _____________________________________________________________________
dict_lookup_func CPyCppyy::gDictLookupOrg = 0;
Bool_t CPyCppyy::gDictLookupActive = kFALSE;

typedef std::map< std::string, std::string > TC2POperatorMapping_t;
static TC2POperatorMapping_t gC2POperatorMapping;

namespace {

   using namespace CPyCppyy::Utility;

   struct InitOperatorMapping_t {
   public:
      InitOperatorMapping_t() {
      // Initialize the global map of operator names C++ -> python.

         // gC2POperatorMapping[ "[]" ]  = "__setitem__";   // depends on return type
         // gC2POperatorMapping[ "+" ]   = "__add__";       // depends on # of args (see __pos__)
         // gC2POperatorMapping[ "-" ]   = "__sub__";       // id. (eq. __neg__)
         // gC2POperatorMapping[ "*" ]   = "__mul__";       // double meaning in C++

         gC2POperatorMapping[ "[]" ]  = "__getitem__";
         gC2POperatorMapping[ "()" ]  = "__call__";
         gC2POperatorMapping[ "/" ]   = CPYCPPYY__div__;
         gC2POperatorMapping[ "%" ]   = "__mod__";
         gC2POperatorMapping[ "**" ]  = "__pow__";
         gC2POperatorMapping[ "<<" ]  = "__lshift__";
         gC2POperatorMapping[ ">>" ]  = "__rshift__";
         gC2POperatorMapping[ "&" ]   = "__and__";
         gC2POperatorMapping[ "|" ]   = "__or__";
         gC2POperatorMapping[ "^" ]   = "__xor__";
         gC2POperatorMapping[ "~" ]   = "__inv__";
         gC2POperatorMapping[ "+=" ]  = "__iadd__";
         gC2POperatorMapping[ "-=" ]  = "__isub__";
         gC2POperatorMapping[ "*=" ]  = "__imul__";
         gC2POperatorMapping[ "/=" ]  = CPYCPPYY__idiv__;
         gC2POperatorMapping[ "%=" ]  = "__imod__";
         gC2POperatorMapping[ "**=" ] = "__ipow__";
         gC2POperatorMapping[ "<<=" ] = "__ilshift__";
         gC2POperatorMapping[ ">>=" ] = "__irshift__";
         gC2POperatorMapping[ "&=" ]  = "__iand__";
         gC2POperatorMapping[ "|=" ]  = "__ior__";
         gC2POperatorMapping[ "^=" ]  = "__ixor__";
         gC2POperatorMapping[ "==" ]  = "__eq__";
         gC2POperatorMapping[ "!=" ]  = "__ne__";
         gC2POperatorMapping[ ">" ]   = "__gt__";
         gC2POperatorMapping[ "<" ]   = "__lt__";
         gC2POperatorMapping[ ">=" ]  = "__ge__";
         gC2POperatorMapping[ "<=" ]  = "__le__";

      // the following type mappings are "exact"
         gC2POperatorMapping[ "const char*" ]  = "__str__";
         gC2POperatorMapping[ "char*" ]        = "__str__";
         gC2POperatorMapping[ "const char *" ] = gC2POperatorMapping[ "const char*" ];
         gC2POperatorMapping[ "char *" ]       = gC2POperatorMapping[ "char*" ];
         gC2POperatorMapping[ "int" ]          = "__int__";
         gC2POperatorMapping[ "long" ]         = CPYCPPYY__long__;
         gC2POperatorMapping[ "double" ]       = "__float__";

      // the following type mappings are "okay"; the assumption is that they
      // are not mixed up with the ones above or between themselves (and if
      // they are, that it is done consistently)
         gC2POperatorMapping[ "short" ]              = "__int__";
         gC2POperatorMapping[ "unsigned short" ]     = "__int__";
         gC2POperatorMapping[ "unsigned int" ]       = CPYCPPYY__long__;
         gC2POperatorMapping[ "unsigned long" ]      = CPYCPPYY__long__;
         gC2POperatorMapping[ "long long" ]          = CPYCPPYY__long__;
         gC2POperatorMapping[ "unsigned long long" ] = CPYCPPYY__long__;
         gC2POperatorMapping[ "float" ]              = "__float__";

         gC2POperatorMapping[ "->" ]  = "__follow__";       // not an actual python operator
         gC2POperatorMapping[ "=" ]   = "__assign__";       // id.

#if PY_VERSION_HEX < 0x03000000
         gC2POperatorMapping[ "bool" ] = "__nonzero__";
#else
         gC2POperatorMapping[ "bool" ] = "__bool__";
#endif
      }
   } initOperatorMapping_;

   std::once_flag sOperatorTemplateFlag;
   void InitOperatorTemplate() {
   /* TODO: move to Cppyy.cxx
      gROOT->ProcessLine(
         "namespace _pycppyy_internal { template<class C1, class C2>"
         " bool is_equal(const C1& c1, const C2& c2){ return (bool)(c1 == c2); } }" );
      gROOT->ProcessLine(
         "namespace _cpycppyy_internal { template<class C1, class C2>"
         " bool is_not_equal(const C1& c1, const C2& c2){ return (bool)(c1 != c2); } }" );
   */
   }

   inline void RemoveConst( std::string& cleanName ) {
      std::string::size_type spos = std::string::npos;
      while ( ( spos = cleanName.find( "const" ) ) != std::string::npos ) {
         cleanName.swap( cleanName.erase( spos, 5 ) );
      }
   }

} // unnamed namespace


//- public functions ---------------------------------------------------------
ULong_t CPyCppyy::PyLongOrInt_AsULong( PyObject* pyobject )
{
// Convert <pybject> to C++ unsigned long, with bounds checking, allow int -> ulong.
   ULong_t ul = PyLong_AsUnsignedLong( pyobject );
   if ( PyErr_Occurred() && PyInt_Check( pyobject ) ) {
      PyErr_Clear();
      Long_t i = PyInt_AS_LONG( pyobject );
      if ( 0 <= i ) {
         ul = (ULong_t)i;
      } else {
         PyErr_SetString( PyExc_ValueError,
            "can\'t convert negative value to unsigned long" );
      }
   }

   return ul;
}

//----------------------------------------------------------------------------
ULong64_t CPyCppyy::PyLongOrInt_AsULong64( PyObject* pyobject )
{
// Convert <pyobject> to C++ unsigned long long, with bounds checking.
   ULong64_t ull = PyLong_AsUnsignedLongLong( pyobject );
   if ( PyErr_Occurred() && PyInt_Check( pyobject ) ) {
      PyErr_Clear();
      Long_t i = PyInt_AS_LONG( pyobject );
      if ( 0 <= i ) {
         ull = (ULong64_t)i;
      } else {
         PyErr_SetString( PyExc_ValueError,
            "can\'t convert negative value to unsigned long long" );
      }
   }

   return ull;
}

//----------------------------------------------------------------------------
Bool_t CPyCppyy::Utility::AddToClass(
      PyObject* pyclass, const char* label, PyCFunction cfunc, int flags )
{
// Add the given function to the class under name 'label'.

// use list for clean-up (.so's are unloaded only at interpreter shutdown)
   static std::list< PyMethodDef > s_pymeths;

   s_pymeths.push_back( PyMethodDef() );
   PyMethodDef* pdef = &s_pymeths.back();
   pdef->ml_name  = const_cast< char* >( label );
   pdef->ml_meth  = cfunc;
   pdef->ml_flags = flags;
   pdef->ml_doc   = NULL;

   PyObject* func = PyCFunction_New( pdef, NULL );
   PyObject* method = TCustomInstanceMethod_New( func, NULL, pyclass );
   Bool_t isOk = PyObject_SetAttrString( pyclass, pdef->ml_name, method ) == 0;
   Py_DECREF( method );
   Py_DECREF( func );

   if ( PyErr_Occurred() )
      return kFALSE;

   if ( ! isOk ) {
      PyErr_Format( PyExc_TypeError, "could not add method %s", label );
      return kFALSE;
   }

   return kTRUE;
}

//----------------------------------------------------------------------------
Bool_t CPyCppyy::Utility::AddToClass( PyObject* pyclass, const char* label, const char* func )
{
// Add the given function to the class under name 'label'.
   PyObject* pyfunc = PyObject_GetAttrString( pyclass, const_cast< char* >( func ) );
   if ( ! pyfunc )
      return kFALSE;

   Bool_t isOk = PyObject_SetAttrString( pyclass, const_cast< char* >( label ), pyfunc ) == 0;

   Py_DECREF( pyfunc );
   return isOk;
}

//----------------------------------------------------------------------------
Bool_t CPyCppyy::Utility::AddToClass( PyObject* pyclass, const char* label, PyCallable* pyfunc )
{
// Add the given function to the class under name 'label'.
   MethodProxy* method =
      (MethodProxy*)PyObject_GetAttrString( pyclass, const_cast< char* >( label ) );

   if ( ! method || ! MethodProxy_Check( method ) ) {
   // not adding to existing MethodProxy; add callable directly to the class
      if ( PyErr_Occurred() )
         PyErr_Clear();
      Py_XDECREF( (PyObject*)method );
      method = MethodProxy_New( label, pyfunc );
      Bool_t isOk = PyObject_SetAttrString(
         pyclass, const_cast< char* >( label ), (PyObject*)method ) == 0;
      Py_DECREF( method );
      return isOk;
   }

   method->AddMethod( pyfunc );

   Py_DECREF( method );
   return kTRUE;
}

//----------------------------------------------------------------------------
Bool_t CPyCppyy::Utility::AddUsingToClass( PyObject* pyclass, const char* method )
{
// Helper to add base class methods to the derived class one (this covers the
// 'using' cases, which the dictionary does not provide).
   MethodProxy* derivedMethod =
         (MethodProxy*)PyObject_GetAttrString( pyclass, const_cast< char* >( method ) );
   if ( ! MethodProxy_Check( derivedMethod ) ) {
      Py_XDECREF( derivedMethod );
      return kFALSE;
   }

   PyObject* mro = PyObject_GetAttr( pyclass, PyStrings::gMRO );
   if ( ! mro || ! PyTuple_Check( mro ) ) {
      Py_XDECREF( mro );
      Py_DECREF( derivedMethod );
      return kFALSE;
   }

   MethodProxy* baseMethod = 0;
   for ( int i = 1; i < PyTuple_GET_SIZE( mro ); ++i ) {
      baseMethod = (MethodProxy*)PyObject_GetAttrString(
         PyTuple_GET_ITEM( mro, i ), const_cast< char* >( method ) );

      if ( ! baseMethod ) {
         PyErr_Clear();
         continue;
      }

      if ( MethodProxy_Check( baseMethod ) )
         break;

      Py_DECREF( baseMethod );
      baseMethod = 0;
   }

   Py_DECREF( mro );

   if ( ! MethodProxy_Check( baseMethod ) ) {
      Py_XDECREF( baseMethod );
      Py_DECREF( derivedMethod );
      return kFALSE;
   }

   derivedMethod->AddMethod( baseMethod );

   Py_DECREF( baseMethod );
   Py_DECREF( derivedMethod );

   return kTRUE;
}

//----------------------------------------------------------------------------
Bool_t CPyCppyy::Utility::AddBinaryOperator(
   PyObject* left, PyObject* right, const char* op, const char* label, const char* alt )
{
// Install the named operator (op) into the left object's class if such a function
// exists as a global overload; a label must be given if the operator is not in
// gC2POperatorMapping (i.e. if it is ambiguous at the member level).

// this should be a given, nevertheless ...
   if ( ! ObjectProxy_Check( left ) )
      return kFALSE;

// retrieve the class names to match the signature of any found global functions
   std::string rcname = ClassName( right );
   std::string lcname = ClassName( left );
   PyObject* pyclass = PyObject_GetAttr( left, PyStrings::gClass );

   Bool_t result = AddBinaryOperator( pyclass, lcname, rcname, op, label, alt );

   Py_DECREF( pyclass );
   return result;
}

//----------------------------------------------------------------------------
Bool_t CPyCppyy::Utility::AddBinaryOperator(
   PyObject* pyclass, const char* op, const char* label, const char* alt )
{
// Install binary operator op in pyclass, working on two instances of pyclass.
   PyObject* pyname = PyObject_GetAttr( pyclass, PyStrings::gCppName );
   if ( ! pyname ) pyname = PyObject_GetAttr( pyclass, PyStrings::gName );
   std::string cname = Cppyy::ResolveName( CPyCppyy_PyUnicode_AsString( pyname ) );
   Py_DECREF( pyname ); pyname = 0;

   return AddBinaryOperator( pyclass, cname, cname, op, label, alt );
}

//----------------------------------------------------------------------------
static inline Cppyy::TCppMethod_t FindAndAddOperator( const std::string& lcname, const std::string& rcname,
     const char* op, void* klass = 0 ) {
// Helper to find a function with matching signature in 'funcs'.
   std::string opname = "operator";
   opname += op;
   std::string proto = lcname + ", " + rcname;

// case of global namespace
// TODO: Cppyy.cxx ...
//   if ( ! klass )
//      return (Cppyy::TCppMethod_t)gROOT->GetGlobalFunctionWithPrototype( opname.c_str(), proto.c_str() );

// case of specific namespace
   return (Cppyy::TCppMethod_t)0; // TODO: klass->GetMethodWithPrototype( opname.c_str(), proto.c_str() );
}

Bool_t CPyCppyy::Utility::AddBinaryOperator( PyObject* pyclass, const std::string& lcname,
   const std::string& rcname, const char* op, const char* label, const char* alt )
{
// Find a global function with a matching signature and install the result on pyclass;
// in addition, __gnu_cxx, std::__1, and _cpycppyy_internal are searched pro-actively (as
// there's AFAICS no way to unearth using information).

   if ( strcmp( op, "==" ) == 0 || strcmp( op, "!=" ) == 0 )
      return kFALSE;

#if 0
// For GNU on clang, search the internal __gnu_cxx namespace for binary operators (is
// typically the case for STL iterators operator==/!=.
// TODO: use Cppyy.cxx
   static TClassRef gnucxx( "__gnu_cxx" );
   static bool gnucxx_exists = (bool)gnucxx.GetClass();

// Same for clang on Mac. TODO: find proper pre-processor magic to only use those specific
// namespaces that are actually around; although to be sure, this isn't expensive.
   static TClassRef std__1( "std::__1" );
   static bool std__1_exists = (bool)std__1.GetClass();

// One more, mostly for Mac, but again not sure whether this is not a general issue. Some
// operators are declared as friends only in classes, so then they're not found in the
// global namespace. That's why there's this little helper.
   std::call_once( sOperatorTemplateFlag, InitOperatorTemplate );
   static TClassRef _pr_int( "_cpycppyy_internal" );

   PyCallable* pyfunc = 0;
   if ( gnucxx_exists ) {
      Cppyy::TCppMethod_t func = FindAndAddOperator( lcname, rcname, op, gnucxx.GetClass() );
      if ( func ) pyfunc = new TFunctionHolder( Cppyy::GetScope( "__gnu_cxx" ), func );
   }

   if ( ! pyfunc && std__1_exists ) {
      Cppyy::TCppMethod_t func = FindAndAddOperator( lcname, rcname, op, std__1.GetClass() );
      if ( func ) pyfunc = new TFunctionHolder( Cppyy::GetScope( "std::__1" ), func );
   }

   if ( ! pyfunc ) {
      std::string::size_type pos = lcname.substr(0, lcname.find('<')).rfind( "::" );
      if ( pos != std::string::npos ) {
         TClass* lcscope = TClass::GetClass( lcname.substr( 0, pos ).c_str() );
         if ( lcscope ) {
            Cppyy::TCppMethod_t func = FindAndAddOperator( lcname, rcname, op, lcscope );
            if ( func ) pyfunc = new TFunctionHolder( Cppyy::GetScope( lcname.substr( 0, pos ) ), func );
         }
      }
   }

   if ( ! pyfunc ) {
      Cppyy::TCppMethod_t func = FindAndAddOperator( lcname, rcname, op );
      if ( func ) pyfunc = new TFunctionHolder( Cppyy::gGlobalScope, func );
   }

   if ( ! pyfunc && _pr_int.GetClass() &&
         lcname.find( "iterator" ) != std::string::npos &&
         rcname.find( "iterator" ) != std::string::npos ) {
   // TODO: gets called too often; make sure it's purely lazy calls only; also try to
   // find a better notion for which classes (other than iterators) this is supposed to
   // work; right now it fails for cases where None is passed
      std::stringstream fname;
      if ( strncmp( op, "==", 2 ) == 0 ) { fname << "is_equal<"; }
      else if ( strncmp( op, "!=", 2 ) == 0 ) { fname << "is_not_equal<"; }
      else { fname << "not_implemented<"; }
      fname  << lcname << ", " << rcname << ">";
      Cppyy::TCppMethod_t func = (Cppyy::TCppMethod_t)_pr_int->GetMethodAny( fname.str().c_str() );
      if ( func ) pyfunc = new TFunctionHolder( Cppyy::GetScope( "_cpycppyy_internal" ), func );
   }

// last chance: there could be a non-instantiated templated method
   TClass* lc = TClass::GetClass( lcname.c_str() );
   if ( lc && strcmp(op, "==") != 0 && strcmp(op, "!=") != 0 ) {
      std::string opname = "operator"; opname += op;
      gInterpreter->LoadFunctionTemplates(lc);
      gInterpreter->GetFunctionTemplate(lc->GetClassInfo(), opname.c_str());
      TFunctionTemplate*f = lc->GetFunctionTemplate(opname.c_str());
      Cppyy::TCppMethod_t func =
         (Cppyy::TCppMethod_t)lc->GetMethodWithPrototype( opname.c_str(), rcname.c_str() );
      if ( func && f ) pyfunc = new TMethodHolder( Cppyy::GetScope( lcname ), func );
   }

   if ( pyfunc ) {  // found a matching overload; add to class
      Bool_t ok = AddToClass( pyclass, label, pyfunc );
      if ( ok && alt )
         return AddToClass( pyclass, alt, label );
   }
#endif
   return kFALSE;
}

//----------------------------------------------------------------------------
PyObject* CPyCppyy::Utility::BuildTemplateName( PyObject* pyname, PyObject* args, int argoff )
{
// Helper to construct the "< type, type, ... >" part of a templated name (either
// for a class as in MakeRootTemplateClass in RootModule.cxx) or for method lookup
// (as in TemplatedMemberHook, below).
   if ( pyname )
      pyname = CPyCppyy_PyUnicode_FromString( CPyCppyy_PyUnicode_AsString( pyname ) );
   else
      pyname = CPyCppyy_PyUnicode_FromString( "" );
   CPyCppyy_PyUnicode_AppendAndDel( &pyname, CPyCppyy_PyUnicode_FromString( "<" ) );

   Py_ssize_t nArgs = PyTuple_GET_SIZE( args );
   for ( int i = argoff; i < nArgs; ++i ) {
   // add type as string to name
      PyObject* tn = PyTuple_GET_ITEM( args, i );
      if ( CPyCppyy_PyUnicode_Check( tn ) ) {
         CPyCppyy_PyUnicode_Append( &pyname, tn );
      } else if (PyObject_HasAttr( tn, PyStrings::gName ) ) {
         // __cppname__ provides a better name for C++ classes (namespaces)
         PyObject* tpName;
         if ( PyObject_HasAttr( tn, PyStrings::gCppName ) ) {
            tpName = PyObject_GetAttr( tn, PyStrings::gCppName );
         } else {
            tpName = PyObject_GetAttr( tn, PyStrings::gName );
         }
         // special case for strings
         if ( strcmp( CPyCppyy_PyUnicode_AsString( tpName ), "str" ) == 0 ) {
            Py_DECREF( tpName );
            tpName = CPyCppyy_PyUnicode_FromString( "std::string" );
         }
         CPyCppyy_PyUnicode_AppendAndDel( &pyname, tpName );
      } else if ( PyInt_Check( tn ) || PyLong_Check( tn ) || PyFloat_Check( tn ) ) {
         // last ditch attempt, works for things like int values; since this is a
         // source of errors otherwise, it is limited to specific types and not
         // generally used (str(obj) can print anything ...)
         PyObject* pystr = PyObject_Str( tn );
         CPyCppyy_PyUnicode_AppendAndDel( &pyname, pystr );
      } else {
         Py_DECREF( pyname );
         PyErr_SetString( PyExc_SyntaxError, "could not get __cppname__ from provided template argument. Is it a str, class, type or int?" );
         return 0;
      }

   // add a comma, as needed
      if ( i != nArgs - 1 )
         CPyCppyy_PyUnicode_AppendAndDel( &pyname, CPyCppyy_PyUnicode_FromString( ", " ) );
   }

// close template name; prevent '>>', which should be '> >'
   if ( CPyCppyy_PyUnicode_AsString( pyname )[ CPyCppyy_PyUnicode_GetSize( pyname ) - 1 ] == '>' )
      CPyCppyy_PyUnicode_AppendAndDel( &pyname, CPyCppyy_PyUnicode_FromString( " >" ) );
   else
      CPyCppyy_PyUnicode_AppendAndDel( &pyname, CPyCppyy_PyUnicode_FromString( ">" ) );

   return pyname;
}

//----------------------------------------------------------------------------
Bool_t CPyCppyy::Utility::InitProxy( PyObject* module, PyTypeObject* pytype, const char* name )
{
// Initialize a proxy class for use by python, and add it to the ROOT module.

// finalize proxy type
   if ( PyType_Ready( pytype ) < 0 )
      return kFALSE;

// add proxy type to the given (ROOT) module
   Py_INCREF( pytype );         // PyModule_AddObject steals reference
   if ( PyModule_AddObject( module, (char*)name, (PyObject*)pytype ) < 0 ) {
      Py_DECREF( pytype );
      return kFALSE;
   }

// declare success
   return kTRUE;
}

//----------------------------------------------------------------------------
int CPyCppyy::Utility::GetBuffer( PyObject* pyobject, char tc, int size, void*& buf, Bool_t check )
{
// Retrieve a linear buffer pointer from the given pyobject.

// special case: don't handle character strings here (yes, they're buffers, but not quite)
   if ( PyBytes_Check( pyobject ) )
      return 0;

// attempt to retrieve pointer to buffer interface
   PyBufferProcs* bufprocs = Py_TYPE(pyobject)->tp_as_buffer;

   PySequenceMethods* seqmeths = Py_TYPE(pyobject)->tp_as_sequence;
   if ( seqmeths != 0 && bufprocs != 0
#if  PY_VERSION_HEX < 0x03000000
        && bufprocs->bf_getwritebuffer != 0
        && (*(bufprocs->bf_getsegcount))( pyobject, 0 ) == 1
#else
        && bufprocs->bf_getbuffer != 0
#endif
      ) {

   // get the buffer
#if PY_VERSION_HEX < 0x03000000
      Py_ssize_t buflen = (*(bufprocs->bf_getwritebuffer))( pyobject, 0, &buf );
#else
      Py_buffer bufinfo;
      (*(bufprocs->bf_getbuffer))( pyobject, &bufinfo, PyBUF_WRITABLE );
      buf = (char*)bufinfo.buf;
      Py_ssize_t buflen = bufinfo.len;
#if PY_VERSION_HEX < 0x03010000
      PyBuffer_Release( pyobject, &bufinfo );
#else
      PyBuffer_Release( &bufinfo );
#endif
#endif

      if ( buf && check == kTRUE ) {
      // determine buffer compatibility (use "buf" as a status flag)
         PyObject* pytc = PyObject_GetAttr( pyobject, PyStrings::gTypeCode );
         if ( pytc != 0 ) {     // for array objects
            if ( CPyCppyy_PyUnicode_AsString( pytc )[0] != tc )
               buf = 0;         // no match
            Py_DECREF( pytc );
         } else if ( seqmeths->sq_length &&
                     (int)(buflen / (*(seqmeths->sq_length))( pyobject )) == size ) {
         // this is a gamble ... may or may not be ok, but that's for the user
            PyErr_Clear();
         } else if ( buflen == size ) {
         // also a gamble, but at least 1 item will fit into the buffer, so very likely ok ...
            PyErr_Clear();
         } else {
            buf = 0;                      // not compatible

         // clarify error message
            PyObject* pytype = 0, *pyvalue = 0, *pytrace = 0;
            PyErr_Fetch( &pytype, &pyvalue, &pytrace );
            PyObject* pyvalue2 = CPyCppyy_PyUnicode_FromFormat(
               (char*)"%s and given element size (%ld) do not match needed (%d)",
               CPyCppyy_PyUnicode_AsString( pyvalue ),
               seqmeths->sq_length ? (Long_t)(buflen / (*(seqmeths->sq_length))( pyobject )) : (Long_t)buflen,
               size );
            Py_DECREF( pyvalue );
            PyErr_Restore( pytype, pyvalue2, pytrace );
         }
      }

      return buflen;
   }

   return 0;
}

//----------------------------------------------------------------------------
std::string CPyCppyy::Utility::MapOperatorName( const std::string& name, Bool_t bTakesParams )
{
// Map the given C++ operator name on the python equivalent.
   if ( 8 < name.size() && name.substr( 0, 8 ) == "operator" ) {
      std::string op = name.substr( 8, std::string::npos );

   // stripping ...
      std::string::size_type start = 0, end = op.size();
      while ( start < end && isspace( op[ start ] ) ) ++start;
      while ( start < end && isspace( op[ end-1 ] ) ) --end;
      op = Cppyy::ResolveName( op.substr( start, end - start ) );

   // map C++ operator to python equivalent, or made up name if no equivalent exists
      TC2POperatorMapping_t::iterator pop = gC2POperatorMapping.find( op );
      if ( pop != gC2POperatorMapping.end() ) {
         return pop->second;

      } else if ( op == "*" ) {
      // dereference v.s. multiplication of two instances
         return bTakesParams ? "__mul__" : "__deref__";

      } else if ( op == "+" ) {
      // unary positive v.s. addition of two instances
         return bTakesParams ? "__add__" : "__pos__";

      } else if ( op == "-" ) {
      // unary negative v.s. subtraction of two instances
         return bTakesParams ? "__sub__" : "__neg__";

      } else if ( op == "++" ) {
      // prefix v.s. postfix increment
         return bTakesParams ? "__postinc__" : "__preinc__";

      } else if ( op == "--" ) {
      // prefix v.s. postfix decrement
         return bTakesParams ? "__postdec__" : "__predec__";
      }

   }

// might get here, as not all operator methods are handled (new, delete, etc.)
   return name;
}

//----------------------------------------------------------------------------
const std::string CPyCppyy::Utility::Compound( const std::string& name )
{
// Break down the compound of a fully qualified type name.
   std::string cleanName = name;
   RemoveConst( cleanName );

   std::string compound = "";
   for ( int ipos = (int)cleanName.size()-1; 0 <= ipos; --ipos ) {
      char c = cleanName[ipos];
      if ( isspace( c ) ) continue;
      if ( isalnum( c ) || c == '_' || c == '>' ) break;

      compound = c + compound;
   }

// for arrays (TODO: deal with the actual size)
   if ( compound == "]" )
       return "[]";

   return compound;
}

//----------------------------------------------------------------------------
Py_ssize_t CPyCppyy::Utility::ArraySize( const std::string& name )
{
// Extract size from an array type, if available.
   std::string cleanName = name;
   RemoveConst( cleanName );

   if ( cleanName[cleanName.size()-1] == ']' ) {
      std::string::size_type idx = cleanName.rfind( '[' );
      if ( idx != std::string::npos ) {
         const std::string asize = cleanName.substr( idx+1, cleanName.size()-2 );
         return strtoul( asize.c_str(), NULL, 0 );
      }
   }

   return -1;
}

//----------------------------------------------------------------------------
const std::string CPyCppyy::Utility::ClassName( PyObject* pyobj )
{
// Retrieve the class name from the given python object (which may be just an
// instance of the class).
   std::string clname = "<unknown>";
   PyObject* pyclass = PyObject_GetAttr( pyobj, PyStrings::gClass );
   if ( pyclass != 0 ) {
      PyObject* pyname = PyObject_GetAttr( pyclass, PyStrings::gCppName );

      if ( pyname != 0 ) {
         clname = CPyCppyy_PyUnicode_AsString( pyname );
         Py_DECREF( pyname );
      } else {
         pyname = PyObject_GetAttr( pyclass, PyStrings::gName );
         if ( pyname != 0 ) {
            clname = CPyCppyy_PyUnicode_AsString( pyname );
            Py_DECREF( pyname );
         } else {
            PyErr_Clear();
         }
      }
      Py_DECREF( pyclass );
   } else {
      PyErr_Clear();
   }

   return clname;
}

//----------------------------------------------------------------------------
PyObject* CPyCppyy::Utility::PyErr_Occurred_WithGIL()
{
// Re-acquire the GIL before calling PyErr_Occurred() in case it has been
// released; note that the p2.2 code assumes that there are no callbacks in
// C++ to python (or at least none returning errors).
#if PY_VERSION_HEX >= 0x02030000
   PyGILState_STATE gstate = PyGILState_Ensure();
   PyObject* e = PyErr_Occurred();
   PyGILState_Release( gstate );
#else
   if ( PyThreadState_GET() )
      return PyErr_Occurred();
   PyObject* e = 0;
#endif

   return e;
}
