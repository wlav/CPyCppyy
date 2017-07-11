// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "Executors.h"
#include "ObjectProxy.h"
#include "TPyBufferFactory.h"
#include "TypeManip.h"
#include "CPyCppyyHelpers.h"
#include "Utility.h"

// Standard
#include <cstring>
#include <utility>
#include <sstream>


//- data ______________________________________________________________________
namespace CPyCppyy {

   typedef TExecutor* (*ExecutorFactory_t) ();
   typedef std::map< std::string, ExecutorFactory_t > ExecFactories_t;
   ExecFactories_t gExecFactories;

   extern PyObject* gNullPtrObject;
}


//- helpers -------------------------------------------------------------------
namespace {

   class GILControl {
   public:
      GILControl( CPyCppyy::TCallContext* ctxt ) :
         fSave( nullptr ), fRelease( ReleasesGIL( ctxt ) ) {
#ifdef WITH_THREAD
         if ( fRelease ) fSave = PyEval_SaveThread();
#endif
      }
      ~GILControl() {
#ifdef WITH_THREAD
         if ( fRelease ) PyEval_RestoreThread( fSave );
#endif
      }
   private:
      PyThreadState* fSave;
      Bool_t fRelease;
   };

} // unnamed namespace

#define CPYCPPYY_IMPL_GILCALL( rtype, tcode )                                 \
static inline rtype GILCall##tcode(                                           \
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, CPyCppyy::TCallContext* ctxt ) {\
   GILControl gc( ctxt );                                                     \
   return Cppyy::Call##tcode( method, self, &ctxt->fArgs );                   \
}

CPYCPPYY_IMPL_GILCALL( void,         V )
CPYCPPYY_IMPL_GILCALL( UChar_t,      B )
CPYCPPYY_IMPL_GILCALL( Char_t,       C )
CPYCPPYY_IMPL_GILCALL( Short_t,      H )
CPYCPPYY_IMPL_GILCALL( Int_t,        I )
CPYCPPYY_IMPL_GILCALL( Long_t,       L )
CPYCPPYY_IMPL_GILCALL( Long64_t,     LL )
CPYCPPYY_IMPL_GILCALL( Float_t,      F )
CPYCPPYY_IMPL_GILCALL( Double_t,     D )
CPYCPPYY_IMPL_GILCALL( LongDouble_t, LD )
CPYCPPYY_IMPL_GILCALL( void*,        R )

// TODO: CallS may not have a use here; CallO is used instead for std::string
static inline Char_t* GILCallS(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, CPyCppyy::TCallContext* ctxt ) {
   GILControl gc( ctxt );
// TODO: make use of getting the string length returned ...
   size_t len;
   return Cppyy::CallS( method, self, &ctxt->fArgs, &len );
}

static inline Cppyy::TCppObject_t GILCallO( Cppyy::TCppMethod_t method,
      Cppyy::TCppObject_t self, CPyCppyy::TCallContext* ctxt, Cppyy::TCppType_t klass ) {
   GILControl gc( ctxt );
   return Cppyy::CallO( method, self, &ctxt->fArgs, klass );
}

static inline Cppyy::TCppObject_t GILCallConstructor(
      Cppyy::TCppMethod_t method, Cppyy::TCppType_t klass, CPyCppyy::TCallContext* ctxt ) {
   GILControl gc( ctxt );
   return Cppyy::CallConstructor( method, klass, &ctxt->fArgs );
}

static inline PyObject* CPyCppyy_PyUnicode_FromInt( Int_t c ) {
   // python chars are range(256)
   if ( c < 0 ) return CPyCppyy_PyUnicode_FromFormat( "%c", 256 - std::abs(c));
   return CPyCppyy_PyUnicode_FromFormat( "%c", c );
}

static inline PyObject* CPyCppyy_PyBool_FromInt( Int_t b ) {
   PyObject* result = (Bool_t)b ? Py_True : Py_False;
   Py_INCREF( result );
   return result;
}


//- executors for built-ins ---------------------------------------------------
PyObject* CPyCppyy::TBoolExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python bool return value
   Bool_t retval = GILCallB( method, self, ctxt );
   PyObject* result = retval ? Py_True : Py_False;
   Py_INCREF( result );
   return result;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TBoolConstRefExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python bool return value
   return CPyCppyy_PyBool_FromInt( *((Bool_t*)GILCallR( method, self, ctxt )) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCharExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method with argument <self, ctxt>, construct python string return value
// with the single char
   return CPyCppyy_PyUnicode_FromInt( (Int_t)GILCallC( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCharConstRefExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python string return value
// with the single char
   return CPyCppyy_PyUnicode_FromInt( *((Char_t*)GILCallR( method, self, ctxt )) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TUCharExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, args>, construct python string return value
// with the single char
   return CPyCppyy_PyUnicode_FromInt( (UChar_t)GILCallB( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TUCharConstRefExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python string return value
//  with the single char from the pointer return
   return CPyCppyy_PyUnicode_FromInt( *((UChar_t*)GILCallR( method, self, ctxt )) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TIntExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python int return value
   return PyInt_FromLong( (Int_t)GILCallI( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TShortExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python int return value
   return PyInt_FromLong( (Short_t)GILCallH( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TLongExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python long return value
   return PyLong_FromLong( (Long_t)GILCallL( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TULongExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python unsigned long return value
   return PyLong_FromUnsignedLong( (ULong_t)GILCallLL( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TLongLongExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python long long return value
   Long64_t result = GILCallLL( method, self, ctxt );
   return PyLong_FromLongLong( result );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TULongLongExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python unsigned long long return value
   ULong64_t result = (ULong64_t)GILCallLL( method, self, ctxt );
   return PyLong_FromUnsignedLongLong( result );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TFloatExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python float return value
   return PyFloat_FromDouble( (Double_t)GILCallF( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TDoubleExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python float return value
   return PyFloat_FromDouble( (Double_t)GILCallD( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TLongDoubleExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python float return value
   return PyFloat_FromDouble( (Double_t)GILCallLD( method, self, ctxt ) );
}

//-----------------------------------------------------------------------------
Bool_t CPyCppyy::TRefExecutor::SetAssignable( PyObject* pyobject )
{
// prepare "buffer" for by-ref returns, used with __setitem__
   if ( pyobject != 0 ) {
      Py_INCREF( pyobject );
      fAssignable = pyobject;
      return kTRUE;
   }

   fAssignable = 0;
   return kFALSE;
}

//-----------------------------------------------------------------------------
#define CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( name, type, stype, F1, F2 )      \
PyObject* CPyCppyy::T##name##RefExecutor::Execute(                             \
       Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )\
{                                                                            \
   type* ref = (type*)GILCallR( method, self, ctxt );                        \
   if ( ! fAssignable )                                                      \
      return F1( (stype)*ref );                                              \
   else {                                                                    \
      *ref = (type)F2( fAssignable );                                        \
      Py_DECREF( fAssignable );                                              \
      fAssignable = 0;                                                       \
      Py_INCREF( Py_None );                                                  \
      return Py_None;                                                        \
   }                                                                         \
}

CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( Bool,   Bool_t,   Long_t,   CPyCppyy_PyBool_FromInt,    PyLong_AsLong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( Char,   Char_t,   Long_t,   CPyCppyy_PyUnicode_FromInt, PyLong_AsLong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( UChar,  UChar_t,  ULong_t,  CPyCppyy_PyUnicode_FromInt, PyLongOrInt_AsULong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( Short,  Short_t,  Long_t,   PyInt_FromLong,     PyLong_AsLong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( UShort, UShort_t, ULong_t,  PyInt_FromLong,     PyLongOrInt_AsULong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( Int,    Int_t,    Long_t,   PyInt_FromLong,     PyLong_AsLong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( UInt,   UInt_t,   ULong_t,  PyLong_FromUnsignedLong, PyLongOrInt_AsULong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( Long,   Long_t,   Long_t,   PyLong_FromLong,    PyLong_AsLong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( ULong,  ULong_t,  ULong_t,  PyLong_FromUnsignedLong, PyLongOrInt_AsULong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR(
   LongLong,  Long64_t,  Long64_t,   PyLong_FromLongLong,         PyLong_AsLongLong )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR(
   ULongLong, ULong64_t, ULong64_t,  PyLong_FromUnsignedLongLong, PyLongOrInt_AsULong64 )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( Float,  Float_t,  Double_t, PyFloat_FromDouble, PyFloat_AsDouble )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR( Double, Double_t, Double_t, PyFloat_FromDouble, PyFloat_AsDouble )
CPYCPPYY_IMPLEMENT_BASIC_REFEXECUTOR(
   LongDouble, LongDouble_t, LongDouble_t, PyFloat_FromDouble, PyFloat_AsDouble )

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TSTLStringRefExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, return python string return value
   if ( ! fAssignable ) {
      std::string* result = (std::string*)GILCallR( method, self, ctxt );
      return CPyCppyy_PyUnicode_FromStringAndSize( result->c_str(), result->size() );
   } else {
      std::string* result = (std::string*)GILCallR( method, self, ctxt );
      *result = std::string(
         CPyCppyy_PyUnicode_AsString( fAssignable ), CPyCppyy_PyUnicode_GET_SIZE( fAssignable ) );

      Py_DECREF( fAssignable );
      fAssignable = 0;

      Py_INCREF( Py_None );
      return Py_None;
   }
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TVoidExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, return None
   GILCallV( method, self, ctxt );
   Py_INCREF( Py_None );
   return Py_None;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCStringExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python string return value
   char* result = (char*)GILCallR( method, self, ctxt );
   if ( ! result ) {
      Py_INCREF( PyStrings::gEmptyString );
      return PyStrings::gEmptyString;
   }

   return CPyCppyy_PyUnicode_FromString( result );
}


//- pointer/array executors ---------------------------------------------------
PyObject* CPyCppyy::TVoidArrayExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python long return value
   Long_t* result = (Long_t*)GILCallR( method, self, ctxt );
   if ( ! result ) {
      Py_INCREF( gNullPtrObject );
      return gNullPtrObject;
   }
   return BufFac_t::Instance()->PyBuffer_FromMemory( result, sizeof(void*) );
}

//-----------------------------------------------------------------------------
#define CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( name, type )                        \
PyObject* CPyCppyy::T##name##ArrayExecutor::Execute(                           \
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )\
{                                                                            \
   return BufFac_t::Instance()->PyBuffer_FromMemory( (type*)GILCallR( method, self, ctxt ) );\
}

CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( Bool,   Bool_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( Short,  Short_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( UShort, UShort_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( Int,    Int_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( UInt,   UInt_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( Long,   Long_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( ULong,  ULong_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( Float,  Float_t )
CPYCPPYY_IMPLEMENT_ARRAY_EXECUTOR( Double, Double_t )


//- special cases ------------------------------------------------------------
PyObject* CPyCppyy::TSTLStringExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python string return value

// TODO: make use of GILLCallS (?!)
   static Cppyy::TCppScope_t sSTLStringScope = Cppyy::GetScope( "std::string" );
   std::string* result = (std::string*)GILCallO( method, self, ctxt, sSTLStringScope );
   if ( ! result ) {
      Py_INCREF( PyStrings::gEmptyString );
      return PyStrings::gEmptyString;
   }

   PyObject* pyresult =
      CPyCppyy_PyUnicode_FromStringAndSize( result->c_str(), result->size() );
   free(result); // GILCallO calls Cppyy::CallO which calls malloc.

   return pyresult;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python proxy object return value
   return BindCppObject( (void*)GILCallR( method, self, ctxt ), fClass );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectByValueExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execution will bring a temporary in existence
   Cppyy::TCppObject_t value = GILCallO( method, self, ctxt, fClass );

   if ( ! value ) {
      if ( ! PyErr_Occurred() )         // callee may have set a python error itself
         PyErr_SetString( PyExc_ValueError, "NULL result where temporary expected" );
      return 0;
   }

// the result can then be bound
   ObjectProxy* pyobj = (ObjectProxy*)BindCppObjectNoCast( value, fClass, kFALSE, kTRUE );
   if ( ! pyobj )
      return 0;

// python ref counting will now control this object's life span
   pyobj->HoldOn();
   return (PyObject*)pyobj;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectRefExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// executor binds the result to the left-hand side, overwriting if an old object
   PyObject* result = BindCppObject( (void*)GILCallR( method, self, ctxt ), fClass );
   if ( ! result || ! fAssignable )
      return result;
   else {
   // this generic code is quite slow compared to its C++ equivalent ...
      PyObject* assign = PyObject_GetAttrString( result, const_cast< char* >( "__assign__" ) );
      if ( ! assign ) {
         PyErr_Clear();
         PyObject* descr = PyObject_Str( result );
         if ( descr && PyBytes_CheckExact( descr ) ) {
            PyErr_Format( PyExc_TypeError, "can not assign to return object (%s)",
                          PyBytes_AS_STRING( descr ) );
         } else {
            PyErr_SetString( PyExc_TypeError, "can not assign to result" );
         }
         Py_XDECREF( descr );
         Py_DECREF( result );
         Py_DECREF( fAssignable ); fAssignable = 0;
         return 0;
      }

      PyObject* res2 = PyObject_CallFunction( assign, const_cast< char* >( "O" ), fAssignable );

      Py_DECREF( assign );
      Py_DECREF( result );
      Py_DECREF( fAssignable ); fAssignable = 0;

      if ( res2 ) {
         Py_DECREF( res2 );             // typically, *this from operator=()
         Py_INCREF( Py_None );
         return Py_None;
      }

      return 0;
   }
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectPtrPtrExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python ROOT object
// return ptr value
   return BindCppObject( (void*)GILCallR( method, self, ctxt ), fClass, kTRUE );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectPtrRefExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python ROOT object
// ignoring ref) return ptr value
   return BindCppObject( *(void**)GILCallR( method, self, ctxt ), fClass, kFALSE );
}


//- smart pointers ------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectBySmartPtrExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// smart pointer executor
   Cppyy::TCppObject_t value = GILCallO( method, self, ctxt, fClass );

   if ( ! value ) {
      if ( ! PyErr_Occurred() )         // callee may have set a python error itself
         PyErr_SetString( PyExc_ValueError, "NULL result where temporary expected" );
      return 0;
   }

// fixme? - why doesn't this do the same as `self._get_smart_ptr().get()'
   ObjectProxy* pyobj = (ObjectProxy*) BindCppObject(
      (void*)GILCallR( (Cppyy::TCppMethod_t)fDereferencer, value, ctxt ), fRawPtrType );

   if ( pyobj ) {
      pyobj->SetSmartPtr( (void*)value, fClass );
      pyobj->HoldOn();  // life-time control by python ref-counting
   }

   return (PyObject*)pyobj;
}

PyObject* CPyCppyy::TCppObjectBySmartPtrPtrExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
   Cppyy::TCppObject_t value = GILCallR( method, self, ctxt );
   if ( ! value )
      return nullptr;

// todo: why doesn't this do the same as `self._get_smart_ptr().get()'
   ObjectProxy* pyobj = (ObjectProxy*) BindCppObject(
      (void*)GILCallR( (Cppyy::TCppMethod_t)fDereferencer, value, ctxt ), fRawPtrType );

   if ( pyobj )
      pyobj->SetSmartPtr( (void*)value, fClass );

   return (PyObject*)pyobj;
}

PyObject* CPyCppyy::TCppObjectBySmartPtrRefExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
   Cppyy::TCppObject_t value = GILCallR( method, self, ctxt );
   if ( ! value )
      return nullptr;

   //if ( ! fAssignable ) {

     // fixme? - why doesn't this do the same as `self._get_smart_ptr().get()'
     ObjectProxy* pyobj = (ObjectProxy*) BindCppObject(
        (void*)GILCallR( (Cppyy::TCppMethod_t)fDereferencer, value, ctxt ), fRawPtrType );

     if ( pyobj )
        pyobj->SetSmartPtr( (void*)value, fClass );

     return (PyObject*)pyobj;

   // todo: assignment not done yet
   //
   /*} else {

     PyObject* result = BindCppObject( (void*)value, fClass );

   // this generic code is quite slow compared to its C++ equivalent ...
      PyObject* assign = PyObject_GetAttrString( result, const_cast< char* >( "__assign__" ) );
      if ( ! assign ) {
         PyErr_Clear();
         PyObject* descr = PyObject_Str( result );
         if ( descr && PyBytes_CheckExact( descr ) ) {
            PyErr_Format( PyExc_TypeError, "can not assign to return object (%s)",
                          PyBytes_AS_STRING( descr ) );
         } else {
            PyErr_SetString( PyExc_TypeError, "can not assign to result" );
         }
         Py_XDECREF( descr );
         Py_DECREF( result );
         Py_DECREF( fAssignable ); fAssignable = 0;
         return 0;
      }

      PyObject* res2 = PyObject_CallFunction( assign, const_cast< char* >( "O" ), fAssignable );


      Py_DECREF( assign );
      Py_DECREF( result );
      Py_DECREF( fAssignable ); fAssignable = 0;

      if ( res2 ) {
         Py_DECREF( res2 );             // typically, *this from operator=()
         Py_INCREF( Py_None );
         return Py_None;
      }

      return 0;
   }
   */
}


//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectArrayExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct TTupleOfInstances from
// return value
   return BindCppObjectArray( (void*)GILCallR( method, self, ctxt ), fClass, fArraySize );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TConstructorExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t klass, TCallContext* ctxt )
{
// package return address in PyObject* for caller to handle appropriately (see
// TConstructorHolder for the actual build of the PyObject)
   return (PyObject*)GILCallConstructor( method, (Cppyy::TCppType_t)klass, ctxt );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TPyObjectExecutor::Execute(
      Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, return python object
   return (PyObject*)GILCallR( method, self, ctxt );
}


//- factories -----------------------------------------------------------------
CPyCppyy::TExecutor* CPyCppyy::CreateExecutor(
      const std::string& fullType, Bool_t manage_smart_ptr )
{
// The matching of the fulltype to an executor factory goes through up to 4 levels:
//   1) full, qualified match
//   2) drop '&' as by ref/full type is often pretty much the same python-wise
//   3) ROOT classes, either by ref/ptr or by value
//   4) additional special case for enums
//
// If all fails, void is used, which will cause the return type to be ignored on use

// an exactly matching executor is best
   ExecFactories_t::iterator h = gExecFactories.find( fullType );
   if ( h != gExecFactories.end() )
      return (h->second)();

// resolve typedefs etc., and collect qualifiers
   std::string resolvedType = Cppyy::ResolveName( fullType );

// a full, qualified matching executor is preferred
   h = gExecFactories.find( resolvedType );
   if ( h != gExecFactories.end() )
      return (h->second)();

//-- nothing? ok, collect information about the type and possible qualifiers/decorators
   const std::string& cpd = Utility::Compound( resolvedType );
   std::string realType = TypeManip::clean_type( resolvedType );

// const-ness (dropped by TypeManip::clean_type) is in general irrelevant
   h = gExecFactories.find( realType + cpd );
   if ( h != gExecFactories.end() )
      return (h->second)();

//-- still nothing? try pointer instead of array (for builtins)
/* TODO: check/fix/remove
   if ( cpd == "[]" ) {
   // CLING WORKAROUND -- if the type is a fixed-size array, it will have a funky
   // resolved type like MyClass(&)[N], which TClass::GetClass() fails on. So, strip
   // it down:
      realType = TClassEdit::CleanType( realType.substr( 0, realType.rfind("(") ).c_str(), 1 );
   // -- CLING WORKAROUND
      h = gExecFactories.find( realType + "*" );
      if ( h != gExecFactories.end() )
         return (h->second)();         // TODO: use array size
   }
*/

// ROOT classes and special cases (enum)
   TExecutor* result = 0;
   if ( Cppyy::TCppType_t klass = Cppyy::GetScope( realType ) ) {
      if ( manage_smart_ptr && Cppyy::IsSmartPtr( realType ) ) {
         const std::vector< Cppyy::TCppMethod_t > methods = Cppyy::GetMethodsFromName( klass, "operator->" );
         if ( ! methods.empty() ) {
            Cppyy::TCppType_t rawPtrType = Cppyy::GetScope(
               TypeManip::clean_type( Cppyy::GetMethodResultType( methods[0] ) ) );
            if ( rawPtrType ) {
               if ( cpd == "" ) {
                  result = new TCppObjectBySmartPtrExecutor( klass, rawPtrType, methods[0] );
               } else if ( cpd == "*" ) {
                  result = new TCppObjectBySmartPtrPtrExecutor( klass, rawPtrType, methods[0] );
               } else if ( cpd == "&" ) {
                  result = new TCppObjectBySmartPtrRefExecutor( klass, rawPtrType, methods[0] );
               } // else if ( cpd == "**" ) {
               //  } else if ( cpd == "*&" || cpd == "&*" ) {
               //  } else if ( cpd == "[]" ) {
               //  } else {
            }
         }
      }

      if ( ! result ) {
         if ( cpd == "" )
            result = new TCppObjectByValueExecutor( klass );
         else if ( cpd == "&" )
            result = new TCppObjectRefExecutor( klass );
         else if ( cpd == "**" )
            result = new TCppObjectPtrPtrExecutor( klass );
         else if ( cpd == "*&" || cpd == "&*" )
            result = new TCppObjectPtrRefExecutor( klass );
         else if ( cpd == "[]" ) {
            Py_ssize_t asize = Utility::ArraySize( resolvedType );
            if ( 0 < asize )
              result = new TCppObjectArrayExecutor( klass, asize );
            else
              result = new TCppObjectPtrRefExecutor( klass );
         } else
            result = new TCppObjectExecutor( klass );
      }
   } else if ( Cppyy::IsEnum( realType ) ) {
   // enums don't resolve to unsigned ints, but that's what they are ...
      h = gExecFactories.find( "UInt_t" + cpd );
   } else {
   // handle (with warning) unknown types
      std::stringstream s;
      s << "creating executor for unknown type \"" << fullType << "\"" << std::ends;
      PyErr_Warn( PyExc_RuntimeWarning, (char*)s.str().c_str() );
   // void* may work ("user knows best"), void will fail on use of return value
      h = (cpd == "") ? gExecFactories.find( "void" ) : gExecFactories.find( "void*" );
   }

   if ( ! result && h != gExecFactories.end() )
   // executor factory available, use it to create executor
      result = (h->second)();

   return result;                  // may still be null
}

////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
#define CPYCPPYY_EXECUTOR_FACTORY( name )                \
TExecutor* Create##name##Executor()                    \
{                                                      \
   return new T##name##Executor;                       \
}

namespace {

   using namespace CPyCppyy;

// use macro rather than template for portability ...
   CPYCPPYY_EXECUTOR_FACTORY( Bool )
   CPYCPPYY_EXECUTOR_FACTORY( BoolRef )
   CPYCPPYY_EXECUTOR_FACTORY( BoolConstRef )
   CPYCPPYY_EXECUTOR_FACTORY( Char )
   CPYCPPYY_EXECUTOR_FACTORY( CharRef )
   CPYCPPYY_EXECUTOR_FACTORY( CharConstRef )
   CPYCPPYY_EXECUTOR_FACTORY( UChar )
   CPYCPPYY_EXECUTOR_FACTORY( UCharRef )
   CPYCPPYY_EXECUTOR_FACTORY( UCharConstRef )
   CPYCPPYY_EXECUTOR_FACTORY( Short )
   CPYCPPYY_EXECUTOR_FACTORY( ShortRef )
   CPYCPPYY_EXECUTOR_FACTORY( UShortRef )
   CPYCPPYY_EXECUTOR_FACTORY( Int )
   CPYCPPYY_EXECUTOR_FACTORY( IntRef )
   CPYCPPYY_EXECUTOR_FACTORY( UIntRef )
   CPYCPPYY_EXECUTOR_FACTORY( ULong )
   CPYCPPYY_EXECUTOR_FACTORY( ULongRef )
   CPYCPPYY_EXECUTOR_FACTORY( Long )
   CPYCPPYY_EXECUTOR_FACTORY( LongRef )
   CPYCPPYY_EXECUTOR_FACTORY( Float )
   CPYCPPYY_EXECUTOR_FACTORY( FloatRef )
   CPYCPPYY_EXECUTOR_FACTORY( Double )
   CPYCPPYY_EXECUTOR_FACTORY( DoubleRef )
   CPYCPPYY_EXECUTOR_FACTORY( LongDouble )
   CPYCPPYY_EXECUTOR_FACTORY( LongDoubleRef )
   CPYCPPYY_EXECUTOR_FACTORY( Void )
   CPYCPPYY_EXECUTOR_FACTORY( LongLong )
   CPYCPPYY_EXECUTOR_FACTORY( LongLongRef )
   CPYCPPYY_EXECUTOR_FACTORY( ULongLong )
   CPYCPPYY_EXECUTOR_FACTORY( ULongLongRef )
   CPYCPPYY_EXECUTOR_FACTORY( CString )
   CPYCPPYY_EXECUTOR_FACTORY( VoidArray )
   CPYCPPYY_EXECUTOR_FACTORY( BoolArray )
   CPYCPPYY_EXECUTOR_FACTORY( ShortArray )
   CPYCPPYY_EXECUTOR_FACTORY( UShortArray )
   CPYCPPYY_EXECUTOR_FACTORY( IntArray )
   CPYCPPYY_EXECUTOR_FACTORY( UIntArray )
   CPYCPPYY_EXECUTOR_FACTORY( LongArray )
   CPYCPPYY_EXECUTOR_FACTORY( ULongArray )
   CPYCPPYY_EXECUTOR_FACTORY( FloatArray )
   CPYCPPYY_EXECUTOR_FACTORY( DoubleArray )
   CPYCPPYY_EXECUTOR_FACTORY( STLString )
   CPYCPPYY_EXECUTOR_FACTORY( STLStringRef )
   CPYCPPYY_EXECUTOR_FACTORY( Constructor )
   CPYCPPYY_EXECUTOR_FACTORY( PyObject )

// executor factories for ROOT types
   typedef std::pair< const char*, ExecutorFactory_t > NFp_t;

   NFp_t factories_[] = {
   // factories for built-ins
      NFp_t( "bool",               &CreateBoolExecutor                ),
      NFp_t( "bool&",              &CreateBoolRefExecutor             ),
      NFp_t( "const bool&",        &CreateBoolConstRefExecutor        ),
      NFp_t( "char",               &CreateCharExecutor                ),
      NFp_t( "signed char",        &CreateCharExecutor                ),
      NFp_t( "unsigned char",      &CreateUCharExecutor               ),
      NFp_t( "char&",              &CreateCharRefExecutor             ),
      NFp_t( "signed char&",       &CreateCharRefExecutor             ),
      NFp_t( "unsigned char&",     &CreateUCharRefExecutor            ),
      NFp_t( "const char&",        &CreateCharConstRefExecutor        ),
      NFp_t( "const signed char&", &CreateCharConstRefExecutor        ),
      NFp_t( "const unsigned char&", &CreateUCharConstRefExecutor     ),
      NFp_t( "short",              &CreateShortExecutor               ),
      NFp_t( "short&",             &CreateShortRefExecutor            ),
      NFp_t( "unsigned short",     &CreateIntExecutor                 ),
      NFp_t( "unsigned short&",    &CreateUShortRefExecutor           ),
      NFp_t( "int",                &CreateIntExecutor                 ),
      NFp_t( "int&",               &CreateIntRefExecutor              ),
      NFp_t( "unsigned int",       &CreateULongExecutor               ),
      NFp_t( "unsigned int&",      &CreateUIntRefExecutor             ),
      NFp_t( "UInt_t",  /* enum */ &CreateULongExecutor               ),
      NFp_t( "UInt_t&", /* enum */ &CreateUIntRefExecutor             ),
      NFp_t( "long",               &CreateLongExecutor                ),
      NFp_t( "long&",              &CreateLongRefExecutor             ),
      NFp_t( "unsigned long",      &CreateULongExecutor               ),
      NFp_t( "unsigned long&",     &CreateULongRefExecutor            ),
      NFp_t( "long long",          &CreateLongLongExecutor            ),
      NFp_t( "Long64_t",           &CreateLongLongExecutor            ),
      NFp_t( "long long&",         &CreateLongLongRefExecutor         ),
      NFp_t( "Long64_t&",          &CreateLongLongRefExecutor         ),
      NFp_t( "unsigned long long", &CreateULongLongExecutor           ),
      NFp_t( "ULong64_t",          &CreateULongLongExecutor           ),
      NFp_t( "unsigned long long&", &CreateULongLongRefExecutor       ),
      NFp_t( "ULong64_t&",         &CreateULongLongRefExecutor        ),

      NFp_t( "float",              &CreateFloatExecutor               ),
      NFp_t( "float&",             &CreateFloatRefExecutor            ),
      NFp_t( "Float16_t",          &CreateFloatExecutor               ),
      NFp_t( "Float16_t&",         &CreateFloatRefExecutor            ),
      NFp_t( "double",             &CreateDoubleExecutor              ),
      NFp_t( "double&",            &CreateDoubleRefExecutor           ),
      NFp_t( "Double32_t",         &CreateDoubleExecutor              ),
      NFp_t( "Double32_t&",        &CreateDoubleRefExecutor           ),
      NFp_t( "long double",        &CreateLongDoubleExecutor          ),   // TODO: lost precision
      NFp_t( "long double&",       &CreateLongDoubleRefExecutor       ),
      NFp_t( "void",               &CreateVoidExecutor                ),

   // pointer/array factories
      NFp_t( "void*",              &CreateVoidArrayExecutor           ),
      NFp_t( "bool*",              &CreateBoolArrayExecutor           ),
      NFp_t( "short*",             &CreateShortArrayExecutor          ),
      NFp_t( "unsigned short*",    &CreateUShortArrayExecutor         ),
      NFp_t( "int*",               &CreateIntArrayExecutor            ),
      NFp_t( "unsigned int*",      &CreateUIntArrayExecutor           ),
      NFp_t( "UInt_t*", /* enum */ &CreateUIntArrayExecutor           ),
      NFp_t( "long*",              &CreateLongArrayExecutor           ),
      NFp_t( "unsigned long*",     &CreateULongArrayExecutor          ),
      NFp_t( "float*",             &CreateFloatArrayExecutor          ),
      NFp_t( "double*",            &CreateDoubleArrayExecutor         ),

   // factories for special cases
      NFp_t( "const char*",        &CreateCStringExecutor             ),
      NFp_t( "char*",              &CreateCStringExecutor             ),
      NFp_t( "std::string",        &CreateSTLStringExecutor           ),
      NFp_t( "string",             &CreateSTLStringExecutor           ),
      NFp_t( "std::string&",       &CreateSTLStringRefExecutor        ),
      NFp_t( "string&",            &CreateSTLStringRefExecutor        ),
      NFp_t( "__init__",           &CreateConstructorExecutor         ),
      NFp_t( "PyObject*",          &CreatePyObjectExecutor            ),
      NFp_t( "_object*",           &CreatePyObjectExecutor            ),
      NFp_t( "FILE*",              &CreateVoidArrayExecutor           )
   };

   struct InitExecFactories_t {
   public:
      InitExecFactories_t()
      {
      // load all executor factories in the global map 'gExecFactories'
         int nf = sizeof( factories_ ) / sizeof( factories_[ 0 ] );
         for ( int i = 0; i < nf; ++i ) {
            gExecFactories[ factories_[ i ].first ] = factories_[ i ].second;
         }
      }
   } initExecvFactories_;

} // unnamed namespace
