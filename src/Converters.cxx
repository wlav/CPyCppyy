// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "DeclareConverters.h"
#include "TCallContext.h"
#include "ObjectProxy.h"
#include "TPyBufferFactory.h"
#include "TCustomPyTypes.h"
#include "TTupleOfInstances.h"
#include "TypeManip.h"
#include "Utility.h"
#include "CPyCppyyHelpers.h"

// Standard
#include <limits.h>
#include <stddef.h>      // for ptrdiff_t
#include <string.h>
#include <utility>
#include <sstream>
#if __cplusplus > 201402L
#include <string_view>
#endif

// FIXME: Should refer to CPyCppyy::TParameter in the code.
#ifdef R__CXXMODULES
  #define TParameter CPyCppyy::TParameter
#endif


//- data ______________________________________________________________________
namespace CPyCppyy {

// factories
   typedef TConverter* (*ConverterFactory_t) ( Long_t size );
   typedef std::map< std::string, ConverterFactory_t > ConvFactories_t;
   static ConvFactories_t gConvFactories;
   extern PyObject* gNullPtrObject;

}

//- pretend-ctypes helpers ----------------------------------------------------
#if PY_VERSION_HEX >= 0x02050000

struct CPyCppyy_tagCDataObject { // non-public (but so far very stable)
    PyObject_HEAD
    char* b_ptr;
};

static inline PyTypeObject* GetCTypesType( const char* name ) {
   PyObject* ct = PyImport_ImportModule( "ctypes" );
   if ( ! ct ) return nullptr;
   PyTypeObject* ct_t = (PyTypeObject*)PyObject_GetAttrString( ct, name );
   Py_DECREF( ct );
   return ct_t;
}

#endif

//- custom helpers to check ranges --------------------------------------------
static inline bool CPyCppyy_PyLong_AsBool( PyObject* pyobject )
{
// range-checking python integer to C++ bool conversion
   Long_t l = PyLong_AsLong( pyobject );
// fail to pass float -> bool; the problem is rounding (0.1 -> 0 -> False)
   if ( ! ( l == 0 || l == 1 ) || PyFloat_Check( pyobject ) ) {
      PyErr_SetString( PyExc_ValueError, "boolean value should be bool, or integer 1 or 0" );
      return (bool)-1;
   }
   return (bool)l;
}

static inline Char_t CPyCppyy_PyUnicode_AsChar( PyObject* pyobject ) {
// python string to C++ char conversion
   return (Char_t)CPyCppyy_PyUnicode_AsString( pyobject )[0];
}

static inline UShort_t CPyCppyy_PyLong_AsUShort( PyObject* pyobject )
{
// range-checking python integer to C++ unsigend short int conversion

// prevent p2.7 silent conversions and do a range check
   if ( ! (PyLong_Check( pyobject ) || PyInt_Check( pyobject )) ) {
      PyErr_SetString( PyExc_TypeError, "unsigned short conversion expects an integer object" );
      return (UShort_t)-1;
   }
   Long_t l = PyLong_AsLong( pyobject );
   if ( l < 0 || USHRT_MAX < l ) {
      PyErr_Format( PyExc_ValueError, "integer %ld out of range for unsigned short", l );
      return (UShort_t)-1;

   }
   return (UShort_t)l;
}

static inline Short_t CPyCppyy_PyLong_AsShort( PyObject* pyobject )
{
// range-checking python integer to C++ short int conversion
// prevent p2.7 silent conversions and do a range check
   if ( ! (PyLong_Check( pyobject ) || PyInt_Check( pyobject )) ) {
      PyErr_SetString( PyExc_TypeError, "short int conversion expects an integer object" );
      return (Short_t)-1;
   }
   Long_t l = PyLong_AsLong( pyobject );
   if ( l < SHRT_MIN || SHRT_MAX < l ) {
      PyErr_Format( PyExc_ValueError, "integer %ld out of range for short int", l );
      return (Short_t)-1;

   }
   return (Short_t)l;
}

static inline Long_t CPyCppyy_PyLong_AsStrictLong( PyObject* pyobject )
{
// strict python integer to C++ integer conversion

// p2.7 and later silently converts floats to long, therefore require this
// check; earlier pythons may raise a SystemError which should be avoided as
// it is confusing
   if ( ! (PyLong_Check( pyobject ) || PyInt_Check( pyobject )) ) {
      PyErr_SetString( PyExc_TypeError, "int/long conversion expects an integer object" );
      return (Long_t)-1;
   }
   return (Long_t)PyLong_AsLong( pyobject );
}


//- base converter implementation ---------------------------------------------
PyObject* CPyCppyy::TConverter::FromMemory( void* )
{
// could happen if no derived class override
   PyErr_SetString( PyExc_TypeError, "C++ type can not be converted from memory" );
   return 0;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TConverter::ToMemory( PyObject*, void* )
{
// could happen if no derived class override
   PyErr_SetString( PyExc_TypeError, "C++ type can not be converted to memory" );
   return false;
}


//- helper macro's ------------------------------------------------------------
#define CPYCPPYY_IMPLEMENT_BASIC_CONVERTER( name, type, stype, F1, F2, tc )   \
bool CPyCppyy::T##name##Converter::SetArg(                                  \
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )        \
{                                                                             \
/* convert <pyobject> to C++ 'type', set arg for call */                      \
   type val = (type)F2( pyobject );                                           \
   if ( val == (type)-1 && PyErr_Occurred() )                                 \
      return false;                                                          \
   para.fValue.f##name = val;                                                 \
   para.fTypeCode = tc;                                                       \
   return true;                                                              \
}                                                                             \
                                                                              \
PyObject* CPyCppyy::T##name##Converter::FromMemory( void* address )           \
{                                                                             \
   return F1( (stype)*((type*)address) );                                     \
}                                                                             \
                                                                              \
bool CPyCppyy::T##name##Converter::ToMemory( PyObject* value, void* address )\
{                                                                             \
   type s = (type)F2( value );                                                \
   if ( s == (type)-1 && PyErr_Occurred() )                                   \
      return false;                                                          \
   *((type*)address) = (type)s;                                               \
   return true;                                                              \
}

//-----------------------------------------------------------------------------
static inline Int_t ExtractChar( PyObject* pyobject, const char* tname, Int_t low, Int_t high )
{
   Int_t lchar = -1;
   if ( CPyCppyy_PyUnicode_Check( pyobject ) ) {
      if ( CPyCppyy_PyUnicode_GET_SIZE( pyobject ) == 1 )
         lchar = (Int_t)CPyCppyy_PyUnicode_AsChar( pyobject );
      else
         PyErr_Format( PyExc_ValueError, "%s expected, got string of size " PY_SSIZE_T_FORMAT,
             tname, CPyCppyy_PyUnicode_GET_SIZE( pyobject ) );
   } else if ( ! PyFloat_Check( pyobject ) ) {    // don't allow truncating conversion
      lchar = PyLong_AsLong( pyobject );
      if ( lchar == -1 && PyErr_Occurred() )
         ; // empty, as error already set
      else if ( ! ( low <= lchar && lchar <= high ) ) {
         PyErr_Format( PyExc_ValueError,
            "integer to character: value %d not in range [%d,%d]", lchar, low, high );
         lchar = -1;
      }
   } else
      PyErr_SetString( PyExc_TypeError, "char or small int type expected" );

   return lchar;
}

//-----------------------------------------------------------------------------
#define CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( name, type, F1 )        \
bool CPyCppyy::TConst##name##RefConverter::SetArg(                          \
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )        \
{                                                                             \
   type val = (type)F1( pyobject );                                           \
   if ( val == (type)-1 && PyErr_Occurred() )                                 \
      return false;                                                          \
   para.fValue.f##name = val;                                                 \
   para.fRef = &para.fValue.f##name;                                          \
   para.fTypeCode = 'r';                                                      \
   return true;                                                              \
}

//-----------------------------------------------------------------------------
#define CPYCPPYY_IMPLEMENT_BASIC_CONST_CHAR_REF_CONVERTER( name, type, low, high )\
bool CPyCppyy::TConst##name##RefConverter::SetArg(                          \
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )        \
{                                                                             \
/* convert <pyobject> to C++ <<type>>, set arg for call, allow int -> char */ \
   type val = (type)ExtractChar( pyobject, #type, low, high );                \
   if ( val == (type)-1 && PyErr_Occurred() )                                 \
      return false;                                                          \
   para.fValue.fLong = val;                                                   \
   para.fTypeCode = 'l';                                                      \
   return true;                                                              \
}

//-----------------------------------------------------------------------------
#define CPYCPPYY_IMPLEMENT_BASIC_CHAR_CONVERTER( name, type, low, high ) \
bool CPyCppyy::T##name##Converter::SetArg(                                  \
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )        \
{                                                                             \
/* convert <pyobject> to C++ <<type>>, set arg for call, allow int -> char */ \
   Long_t val = ExtractChar( pyobject, #type, low, high );                    \
   if ( val == -1 && PyErr_Occurred() )                                       \
      return false;                                                          \
   para.fValue.fLong = val;                                                   \
   para.fTypeCode = 'l';                                                      \
   return true;                                                              \
}                                                                             \
                                                                              \
PyObject* CPyCppyy::T##name##Converter::FromMemory( void* address )           \
{                                                                             \
   return CPyCppyy_PyUnicode_FromFormat( "%c", *((type*)address) );           \
}                                                                             \
                                                                              \
bool CPyCppyy::T##name##Converter::ToMemory( PyObject* value, void* address )\
{                                                                             \
   if ( CPyCppyy_PyUnicode_Check( value ) ) {                                 \
      const char* buf = CPyCppyy_PyUnicode_AsString( value );                 \
      if ( PyErr_Occurred() )                                                 \
         return false;                                                       \
      int len = CPyCppyy_PyUnicode_GET_SIZE( value );                         \
      if ( len != 1 ) {                                                       \
         PyErr_Format( PyExc_TypeError, #type" expected, got string of size %d", len );\
         return false;                                                       \
      }                                                                       \
      *((type*)address) = (type)buf[0];                                       \
   } else {                                                                   \
      Long_t l = PyLong_AsLong( value );                                      \
      if ( l == -1 && PyErr_Occurred() )                                      \
         return false;                                                       \
      if ( ! ( low <= l && l <= high ) ) {                                    \
         PyErr_Format( PyExc_ValueError, \
            "integer to character: value %ld not in range [%d,%d]", l, low, high );\
         return false;                                                       \
      }                                                                       \
      *((type*)address) = (type)l;                                            \
   }                                                                          \
   return true;                                                              \
}


//- converters for built-ins --------------------------------------------------
CPYCPPYY_IMPLEMENT_BASIC_CONVERTER( Long, Long_t, Long_t, PyLong_FromLong, CPyCppyy_PyLong_AsStrictLong, 'l' )

//-----------------------------------------------------------------------------
bool CPyCppyy::TLongRefConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ long&, set arg for call
#if PY_VERSION_HEX < 0x03000000
   if ( TCustomInt_CheckExact( pyobject ) ) {
      para.fValue.fVoidp = (void*)&((PyIntObject*)pyobject)->ob_ival;
      para.fTypeCode = 'V';
      return true;
   }
#endif

#if PY_VERSION_HEX < 0x02050000
   PyErr_SetString( PyExc_TypeError, "use cppyy.Long for pass-by-ref of longs" );
   return false;
#endif

// TODO: this keeps a refcount to the type .. it should be okay to drop that
   static PyTypeObject* c_long_type = GetCTypesType( "c_long" );
   if ( Py_TYPE( pyobject ) == c_long_type ) {
      para.fValue.fVoidp = (void*)((CPyCppyy_tagCDataObject*)pyobject)->b_ptr;
      para.fTypeCode = 'V';
      return true;
   }

   PyErr_SetString( PyExc_TypeError, "use ctypes.c_long for pass-by-ref of longs" );
   return false;
}

//-----------------------------------------------------------------------------
CPYCPPYY_IMPLEMENT_BASIC_CONST_CHAR_REF_CONVERTER( Char,  Char_t,  CHAR_MIN,  CHAR_MAX )
CPYCPPYY_IMPLEMENT_BASIC_CONST_CHAR_REF_CONVERTER( UChar, UChar_t,        0, UCHAR_MAX )

CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( Bool,      bool,    CPyCppyy_PyLong_AsBool )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( Short,     Short_t,   CPyCppyy_PyLong_AsShort )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( UShort,    UShort_t,  CPyCppyy_PyLong_AsUShort )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( Int,       Int_t,     CPyCppyy_PyLong_AsStrictLong )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( UInt,      UInt_t,    PyLongOrInt_AsULong )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( Long,      Long_t,    CPyCppyy_PyLong_AsStrictLong )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( ULong,     ULong_t,   PyLongOrInt_AsULong )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( LongLong,  Long64_t,  PyLong_AsLongLong )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( ULongLong, ULong64_t, PyLongOrInt_AsULong64 )

//-----------------------------------------------------------------------------
bool CPyCppyy::TIntRefConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ (pseudo)int&, set arg for call
#if PY_VERSION_HEX < 0x03000000
   if ( TCustomInt_CheckExact( pyobject ) ) {
      para.fValue.fVoidp = (void*)&((PyIntObject*)pyobject)->ob_ival;
      para.fTypeCode = 'V';
      return true;
   }
#endif

#if PY_VERSION_HEX >= 0x02050000
// TODO: this keeps a refcount to the type .. it should be okay to drop that
   static PyTypeObject* c_int_type = GetCTypesType( "c_int" );
   if ( Py_TYPE( pyobject ) == c_int_type ) {
      para.fValue.fVoidp = (void*)((CPyCppyy_tagCDataObject*)pyobject)->b_ptr;
      para.fTypeCode = 'V';
      return true;
   }
#endif

// alternate, pass pointer from buffer
   int buflen = Utility::GetBuffer( pyobject, 'i', sizeof(int), para.fValue.fVoidp );
   if ( para.fValue.fVoidp && buflen ) {
      para.fTypeCode = 'V';
      return true;
   };

#if PY_VERSION_HEX < 0x02050000
   PyErr_SetString( PyExc_TypeError, "use cppyy.Long for pass-by-ref of ints" );
#else
   PyErr_SetString( PyExc_TypeError, "use ctypes.c_int for pass-by-ref of ints" );
#endif
   return false;
}

//-----------------------------------------------------------------------------
// convert <pyobject> to C++ bool, allow int/long -> bool, set arg for call
CPYCPPYY_IMPLEMENT_BASIC_CONVERTER(
   Bool, bool, Long_t, PyInt_FromLong, CPyCppyy_PyLong_AsBool, 'l' )

//-----------------------------------------------------------------------------
CPYCPPYY_IMPLEMENT_BASIC_CHAR_CONVERTER( Char,  Char_t,  CHAR_MIN, CHAR_MAX  )
CPYCPPYY_IMPLEMENT_BASIC_CHAR_CONVERTER( UChar, UChar_t,        0, UCHAR_MAX )

//-----------------------------------------------------------------------------
CPYCPPYY_IMPLEMENT_BASIC_CONVERTER(
   Short,  Short_t,  Long_t, PyInt_FromLong,  CPyCppyy_PyLong_AsShort, 'l' )
CPYCPPYY_IMPLEMENT_BASIC_CONVERTER(
   UShort, UShort_t, Long_t, PyInt_FromLong,  CPyCppyy_PyLong_AsUShort, 'l' )
CPYCPPYY_IMPLEMENT_BASIC_CONVERTER(
   Int,    Int_t,    Long_t, PyInt_FromLong,  CPyCppyy_PyLong_AsStrictLong, 'l' )

//-----------------------------------------------------------------------------
bool CPyCppyy::TULongConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ unsigned long, set arg for call
   para.fValue.fULong = PyLongOrInt_AsULong( pyobject );
   if ( PyErr_Occurred() )
      return false;
   para.fTypeCode = 'L';
   return true;
}

PyObject* CPyCppyy::TULongConverter::FromMemory( void* address )
{
// construct python object from C++ unsigned long read at <address>
   return PyLong_FromUnsignedLong( *((ULong_t*)address) );
}

bool CPyCppyy::TULongConverter::ToMemory( PyObject* value, void* address )
{
// convert <value> to C++ unsigned long, write it at <address>
   ULong_t u = PyLongOrInt_AsULong( value );
   if ( PyErr_Occurred() )
      return false;
   *((ULong_t*)address) = u;
   return true;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TUIntConverter::FromMemory( void* address )
{
// construct python object from C++ unsigned int read at <address>
   return PyLong_FromUnsignedLong( *((UInt_t*)address) );
}

bool CPyCppyy::TUIntConverter::ToMemory( PyObject* value, void* address )
{
// convert <value> to C++ unsigned int, write it at <address>
   ULong_t u = PyLongOrInt_AsULong( value );
   if ( PyErr_Occurred() )
      return false;

   if ( u > (ULong_t)UINT_MAX ) {
      PyErr_SetString( PyExc_OverflowError, "value too large for unsigned int" );
      return false;
   }

   *((UInt_t*)address) = (UInt_t)u;
   return true;
}

//- floating point converters -------------------------------------------------
CPYCPPYY_IMPLEMENT_BASIC_CONVERTER(
   Float,  Float_t,  Double_t, PyFloat_FromDouble, PyFloat_AsDouble, 'f' )
CPYCPPYY_IMPLEMENT_BASIC_CONVERTER(
   Double, Double_t, Double_t, PyFloat_FromDouble, PyFloat_AsDouble, 'd' )

CPYCPPYY_IMPLEMENT_BASIC_CONVERTER(
   LongDouble, LongDouble_t, LongDouble_t, PyFloat_FromDouble, PyFloat_AsDouble, 'g' )

//-----------------------------------------------------------------------------
bool CPyCppyy::TDoubleRefConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ double&, set arg for call
   if ( TCustomFloat_CheckExact( pyobject ) ) {
      para.fValue.fVoidp = (void*)&((PyFloatObject*)pyobject)->ob_fval;
      para.fTypeCode = 'V';
      return true;
   }

// alternate, pass pointer from buffer
   int buflen = Utility::GetBuffer( pyobject, 'd', sizeof(double), para.fValue.fVoidp );
   if ( para.fValue.fVoidp && buflen ) {
      para.fTypeCode = 'V';
      return true;
   }

   PyErr_SetString( PyExc_TypeError, "use cppyy.Double for pass-by-ref of doubles" );
   return false;
}

//-----------------------------------------------------------------------------
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( Float,      Float_t,      PyFloat_AsDouble )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( Double,     Double_t,     PyFloat_AsDouble )
CPYCPPYY_IMPLEMENT_BASIC_CONST_REF_CONVERTER( LongDouble, LongDouble_t, PyFloat_AsDouble )

//-----------------------------------------------------------------------------
bool CPyCppyy::TVoidConverter::SetArg( PyObject*, TParameter&, TCallContext* )
{
// can't happen (unless a type is mapped wrongly), but implemented for completeness
   PyErr_SetString( PyExc_SystemError, "void/unknown arguments can\'t be set" );
   return false;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TLongLongConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ long long, set arg for call
   if ( PyFloat_Check( pyobject ) ) {
   // special case: float implements nb_int, but allowing rounding conversions
   // interferes with overloading
      PyErr_SetString( PyExc_ValueError, "can not convert float to long long" );
      return false;
   }

   para.fValue.fLongLong = PyLong_AsLongLong( pyobject );
   if ( PyErr_Occurred() )
      return false;
   para.fTypeCode = 'q';
   return true;
}

PyObject* CPyCppyy::TLongLongConverter::FromMemory( void* address )
{
// construct python object from C++ long long read at <address>
   return PyLong_FromLongLong( *(Long64_t*)address );
}

bool CPyCppyy::TLongLongConverter::ToMemory( PyObject* value, void* address )
{
// convert <value> to C++ long long, write it at <address>
   Long64_t ll = PyLong_AsLongLong( value );
   if ( ll == -1 && PyErr_Occurred() )
      return false;
   *((Long64_t*)address) = ll;
   return true;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TULongLongConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ unsigned long long, set arg for call
   para.fValue.fULongLong = PyLongOrInt_AsULong64( pyobject );
   if ( PyErr_Occurred() )
      return false;
   para.fTypeCode = 'Q';
   return true;
}

PyObject* CPyCppyy::TULongLongConverter::FromMemory( void* address )
{
// construct python object from C++ unsigned long long read at <address>
   return PyLong_FromUnsignedLongLong( *(ULong64_t*)address );
}

bool CPyCppyy::TULongLongConverter::ToMemory( PyObject* value, void* address )
{
// convert <value> to C++ unsigned long long, write it at <address>
   Long64_t ull = PyLongOrInt_AsULong64( value );
   if ( PyErr_Occurred() )
      return false;
   *((ULong64_t*)address) = ull;
   return true;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TCStringConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// construct a new string and copy it in new memory
   const char* s = CPyCppyy_PyUnicode_AsStringChecked( pyobject );
   if ( PyErr_Occurred() )
      return false;

   fBuffer = std::string( s, CPyCppyy_PyUnicode_GET_SIZE( pyobject ) );

// verify (too long string will cause truncation, no crash)
   if ( fMaxSize < (UInt_t)fBuffer.size() )
      PyErr_Warn( PyExc_RuntimeWarning, (char*)"string too long for char array (truncated)" );
   else if ( fMaxSize != UINT_MAX )
      fBuffer.resize( fMaxSize, '\0' );      // padd remainder of buffer as needed

// set the value and declare success
   para.fValue.fVoidp = (void*)fBuffer.c_str();
   para.fTypeCode = 'p';
   return true;
}

PyObject* CPyCppyy::TCStringConverter::FromMemory( void* address )
{
// construct python object from C++ const char* read at <address>
   if ( address && *(char**)address ) {
      if ( fMaxSize != UINT_MAX ) {          // need to prevent reading beyond boundary
         std::string buf( *(char**)address, fMaxSize );     // cut on fMaxSize
         return CPyCppyy_PyUnicode_FromString( buf.c_str() ); // cut on \0
      }

      return CPyCppyy_PyUnicode_FromString( *(char**)address );
   }

// empty string in case there's no address
   Py_INCREF( PyStrings::gEmptyString );
   return PyStrings::gEmptyString;
}

bool CPyCppyy::TCStringConverter::ToMemory( PyObject* value, void* address )
{
// convert <value> to C++ const char*, write it at <address>
   const char* s = CPyCppyy_PyUnicode_AsStringChecked( value );
   if ( PyErr_Occurred() )
      return false;

// verify (too long string will cause truncation, no crash)
   if ( fMaxSize < (UInt_t)CPyCppyy_PyUnicode_GET_SIZE( value ) )
      PyErr_Warn( PyExc_RuntimeWarning, (char*)"string too long for char array (truncated)" );

   if ( fMaxSize != UINT_MAX )
      strncpy( *(char**)address, s, fMaxSize );   // padds remainder
   else
      // coverity[secure_coding] - can't help it, it's intentional.
      strcpy( *(char**)address, s );

   return true;
}


//- pointer/array conversions -------------------------------------------------
namespace {

   using namespace CPyCppyy;

   inline bool CArraySetArg(
      PyObject* pyobject, TParameter& para, char tc, int size )
   {
   // general case of loading a C array pointer (void* + type code) as function argument
      if ( pyobject == gNullPtrObject ) {
         para.fValue.fVoidp = NULL;
      } else {
         int buflen = Utility::GetBuffer( pyobject, tc, size, para.fValue.fVoidp );
         if ( ! para.fValue.fVoidp || buflen == 0 )
            return false;
      }
      para.fTypeCode = 'p';
      return true;
   }

} // unnamed namespace


//-----------------------------------------------------------------------------
bool CPyCppyy::TNonConstCStringConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )
{
// attempt base class first (i.e. passing a string), but if that fails, try a buffer
   if ( this->TCStringConverter::SetArg( pyobject, para, ctxt ) )
      return true;

// apparently failed, try char buffer
   PyErr_Clear();
   return CArraySetArg( pyobject, para, 'c', sizeof(char) );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TNonConstCStringConverter::FromMemory( void* address )
{
// assume this is a buffer access if the size is known; otherwise assume string
   if ( fMaxSize != UINT_MAX )
      return CPyCppyy_PyUnicode_FromStringAndSize( *(char**)address, fMaxSize );
   return this->TCStringConverter::FromMemory( address );
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TNonConstUCStringConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )
{
// attempt base class first (i.e. passing a string), but if that fails, try a buffer
   if ( this->TCStringConverter::SetArg( pyobject, para, ctxt ) )
      return true;

// apparently failed, try char buffer
   PyErr_Clear();
   return CArraySetArg( pyobject, para, 'B', sizeof(unsigned char) );
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TVoidArrayConverter::GetAddressSpecialCase( PyObject* pyobject, void*& address )
{
// (1): C++11 style "null pointer"
   if ( pyobject == gNullPtrObject ) {
      address = nullptr;
      return true;
   }

// (2): allow integer zero to act as a null pointer (C NULL), no deriveds
   if ( PyInt_CheckExact( pyobject ) || PyLong_CheckExact( pyobject ) ) {
      Long_t val = (Long_t)PyLong_AsLong( pyobject );
      if ( val == 0l ) {
         address = (void*)val;
         return true;
      }

      return false;
   }

// (3): opaque PyCapsule (CObject in older pythons) from somewhere
   if ( CPyCppyy_PyCapsule_CheckExact( pyobject ) ) {
      address = (void*)CPyCppyy_PyCapsule_GetPointer( pyobject, NULL );
      return true;
   }

   return false;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TVoidArrayConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )
{
// just convert pointer if it is a C++ object
   if ( ObjectProxy_Check( pyobject ) ) {
   // depending on memory policy, some objects are no longer owned when passed to C++
      if ( ! fKeepControl && ! UseStrictOwnership( ctxt ) )
         ((ObjectProxy*)pyobject)->Release();

   // set pointer (may be null) and declare success
      para.fValue.fVoidp = ((ObjectProxy*)pyobject)->GetObject();
      para.fTypeCode = 'p';
      return true;
   }

// handle special cases
   if ( GetAddressSpecialCase( pyobject, para.fValue.fVoidp ) ) {
      para.fTypeCode = 'p';
      return true;
   }

// final try: attempt to get buffer
   int buflen = Utility::GetBuffer( pyobject, '*', 1, para.fValue.fVoidp, false );

// ok if buffer exists (can't perform any useful size checks)
   if ( para.fValue.fVoidp && buflen != 0 ) {
      para.fTypeCode = 'p';
      return true;
   }

// give up
   return false;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TVoidArrayConverter::FromMemory( void* address )
{
// nothing sensible can be done, just return <address> as pylong
   if ( ! address || *(ptrdiff_t*)address == 0 ) {
      Py_INCREF( gNullPtrObject );
      return gNullPtrObject;
   }
   return BufFac_t::Instance()->PyBuffer_FromMemory( (Long_t*)*(ptrdiff_t**)address, sizeof(void*) );
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TVoidArrayConverter::ToMemory( PyObject* value, void* address )
{
// just convert pointer if it is a C++ object
   if ( ObjectProxy_Check( value ) ) {
   // depending on memory policy, some objects are no longer owned when passed to C++
      if ( ! fKeepControl && TCallContext::sMemoryPolicy != TCallContext::kUseStrict )
         ((ObjectProxy*)value)->Release();

   // set pointer (may be null) and declare success
      *(void**)address = ((ObjectProxy*)value)->GetObject();
      return true;
   }

// handle special cases
   void* ptr = 0;
   if ( GetAddressSpecialCase( value, ptr ) ) {
      *(void**)address = ptr;
      return true;
   }

// final try: attempt to get buffer
   void* buf = 0;
   int buflen = Utility::GetBuffer( value, '*', 1, buf, false );
   if ( ! buf || buflen == 0 )
      return false;

   *(void**)address = buf;
   return true;
}

//-----------------------------------------------------------------------------
#define CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( name, type, code )               \
bool CPyCppyy::T##name##ArrayConverter::SetArg(                            \
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )       \
{                                                                            \
   return CArraySetArg( pyobject, para, code, sizeof(type) );                \
}                                                                            \
                                                                             \
bool CPyCppyy::T##name##ArrayRefConverter::SetArg(                         \
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )             \
{                                                                            \
   bool result = T##name##ArrayConverter::SetArg( pyobject, para, ctxt );  \
   para.fTypeCode = 'V';                                                     \
   return result;                                                            \
}                                                                            \
                                                                             \
PyObject* CPyCppyy::T##name##ArrayConverter::FromMemory( void* address )     \
{                                                                            \
   return BufFac_t::Instance()->PyBuffer_FromMemory( *(type**)address, fSize * sizeof(type) );\
}                                                                            \
                                                                             \
bool CPyCppyy::T##name##ArrayConverter::ToMemory( PyObject* value, void* address )\
{                                                                            \
   void* buf = 0;                                                            \
   int buflen = Utility::GetBuffer( value, code, sizeof(type), buf );        \
   if ( ! buf || buflen == 0 )                                               \
      return false;                                                         \
   if ( 0 <= fSize ) {                                                       \
      if ( fSize < buflen/(int)sizeof(type) ) {                              \
         PyErr_SetString( PyExc_ValueError, "buffer too large for value" );  \
         return false;                                                      \
      }                                                                      \
      memcpy( *(type**)address, buf, 0 < buflen ? ((size_t) buflen) : sizeof(type) );\
   } else                                                                    \
      *(type**)address = (type*)buf;                                         \
   return true;                                                             \
}

//-----------------------------------------------------------------------------
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( Bool,   bool,   'b' )   // signed char
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( Short,  Short_t,  'h' )
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( UShort, UShort_t, 'H' )
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( Int,    Int_t,    'i' )
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( UInt,   UInt_t,   'I' )
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( Long,   Long_t,   'l' )
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( ULong,  ULong_t,  'L' )
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( Float,  Float_t,  'f' )
CPYCPPYY_IMPLEMENT_ARRAY_CONVERTER( Double, Double_t, 'd' )

//-----------------------------------------------------------------------------
bool CPyCppyy::TLongLongArrayConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )
{
// convert <pyobject> to C++ long long*, set arg for call
   PyObject* pytc = PyObject_GetAttr( pyobject, PyStrings::gTypeCode );
   if ( pytc != 0 ) {              // iow, this array has a known type, but there's no
      Py_DECREF( pytc );           // such thing for long long in module array
      return false;
   }

   return TVoidArrayConverter::SetArg( pyobject, para, ctxt );
}


//- converters for special cases ----------------------------------------------
#define CPYCPPYY_IMPLEMENT_STRING_AS_PRIMITIVE_CONVERTER( name, type, F1, F2 )\
CPyCppyy::T##name##Converter::T##name##Converter( bool keepControl ) :      \
      TCppObjectConverter( Cppyy::GetScope( #type ), keepControl ) {}         \
                                                                              \
bool CPyCppyy::T##name##Converter::SetArg(                                  \
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )              \
{                                                                             \
   if ( CPyCppyy_PyUnicode_Check( pyobject ) ) {                              \
      fBuffer = type( CPyCppyy_PyUnicode_AsString( pyobject ),                \
                      CPyCppyy_PyUnicode_GET_SIZE( pyobject ) );              \
      para.fValue.fVoidp = &fBuffer;                                          \
      para.fTypeCode = 'V';                                                   \
      return true;                                                           \
   }                                                                          \
                                                                              \
   if ( ! ( PyInt_Check( pyobject ) || PyLong_Check( pyobject ) ) ) {         \
      bool result = TCppObjectConverter::SetArg( pyobject, para, ctxt );    \
      para.fTypeCode = 'V';                                                   \
      return result;                                                          \
   }                                                                          \
   return false;                                                             \
}                                                                             \
                                                                              \
PyObject* CPyCppyy::T##name##Converter::FromMemory( void* address )           \
{                                                                             \
   if ( address )                                                             \
      return CPyCppyy_PyUnicode_FromStringAndSize( ((type*)address)->F1(), ((type*)address)->F2() );\
   Py_INCREF( PyStrings::gEmptyString );                                      \
   return PyStrings::gEmptyString;                                            \
}                                                                             \
                                                                              \
bool CPyCppyy::T##name##Converter::ToMemory( PyObject* value, void* address )\
{                                                                             \
   if ( CPyCppyy_PyUnicode_Check( value ) ) {                                 \
      *((type*)address) = CPyCppyy_PyUnicode_AsString( value );               \
      return true;                                                           \
   }                                                                          \
                                                                              \
   return TCppObjectConverter::ToMemory( value, address );                    \
}

CPYCPPYY_IMPLEMENT_STRING_AS_PRIMITIVE_CONVERTER( STLString, std::string, c_str, size )
#if __cplusplus > 201402L
CPYCPPYY_IMPLEMENT_STRING_AS_PRIMITIVE_CONVERTER( STLStringView, std::string_view, data, size )
#endif

//-----------------------------------------------------------------------------
bool CPyCppyy::TCppObjectConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )
{
// convert <pyobject> to C++ instance*, set arg for call
   if ( ! ObjectProxy_Check( pyobject ) ) {
      if ( GetAddressSpecialCase( pyobject, para.fValue.fVoidp ) ) {
         para.fTypeCode = 'p';      // allow special cases such as NULL
         return true;
      }

   // not a cppyy object (TODO: handle SWIG etc.)
      return false;
   }

   ObjectProxy* pyobj = (ObjectProxy*)pyobject;
   if ( pyobj->ObjectIsA() && Cppyy::IsSubtype( pyobj->ObjectIsA(), fClass ) ) {
   // depending on memory policy, some objects need releasing when passed into functions
      if ( ! KeepControl() && ! UseStrictOwnership( ctxt ) )
         ((ObjectProxy*)pyobject)->Release();

   // calculate offset between formal and actual arguments
      para.fValue.fVoidp = pyobj->GetObject();
      if ( pyobj->ObjectIsA() != fClass ) {
         para.fValue.fLong += Cppyy::GetBaseOffset(
            pyobj->ObjectIsA(), fClass, para.fValue.fVoidp, 1 /* up-cast */ );
      }

   // set pointer (may be null) and declare success
      para.fTypeCode = 'p';
      return true;

   }
/* TODO: remove usage of TClass
   else if ( ! TClass::GetClass( Cppyy::GetFinalName( fClass ).c_str() )->GetClassInfo() ) {
   // assume "user knows best" to allow anonymous pointer passing
      para.fValue.fVoidp = pyobj->GetObject();
      para.fTypeCode = 'p';
      return true;
   }
*/

   return false;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectConverter::FromMemory( void* address )
{
// construct python object from C++ instance read at <address>
   return BindCppObject( address, fClass, false );
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TCppObjectConverter::ToMemory( PyObject* value, void* address )
{
// convert <value> to C++ instance, write it at <address>
   if ( ! ObjectProxy_Check( value ) ) {
      void* ptr = 0;
      if ( GetAddressSpecialCase( value, ptr ) ) {
         *(void**)address = ptr;             // allow special cases such as NULL
         return true;
      }

   // not a cppyy object (TODO: handle SWIG etc.)
      return false;
   }

   if ( Cppyy::IsSubtype( ((ObjectProxy*)value)->ObjectIsA(), fClass ) ) {
   // depending on memory policy, some objects need releasing when passed into functions
      if ( ! KeepControl() && TCallContext::sMemoryPolicy != TCallContext::kUseStrict )
         ((ObjectProxy*)value)->Release();

   // call assignment operator through a temporarily wrapped object proxy
      PyObject* pyobj = BindCppObjectNoCast( address, fClass );
      ((ObjectProxy*)pyobj)->Release();     // TODO: might be recycled (?)
      PyObject* result = PyObject_CallMethod( pyobj, (char*)"__assign__", (char*)"O", value );
      Py_DECREF( pyobj );
      if ( result ) {
         Py_DECREF( result );
         return true;
      }
   }

   return false;
}

// TODO: CONSOLIDATE ValueCpp, RefCpp, and CppObject ...

//-----------------------------------------------------------------------------
bool CPyCppyy::TValueCppObjectConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ instance, set arg for call
   if ( ! ObjectProxy_Check( pyobject ) )
      return false;

   ObjectProxy* pyobj = (ObjectProxy*)pyobject;
   if ( pyobj->ObjectIsA() && Cppyy::IsSubtype( pyobj->ObjectIsA(), fClass ) ) {
   // calculate offset between formal and actual arguments
      para.fValue.fVoidp = pyobj->GetObject();
      if ( ! para.fValue.fVoidp )
         return false;

      if ( pyobj->ObjectIsA() != fClass ) {
         para.fValue.fLong += Cppyy::GetBaseOffset(
            pyobj->ObjectIsA(), fClass, para.fValue.fVoidp, 1 /* up-cast */ );
      }

      para.fTypeCode = 'V';
      return true;
   }

   return false;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TRefCppObjectConverter::SetArg(
        PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */)
{
// convert <pyobject> to C++ instance&, set arg for call
    if (!ObjectProxy_Check(pyobject))
        return false;
    ObjectProxy* pyobj = (ObjectProxy*)pyobject;

// reject moves
    if (pyobj->fFlags & ObjectProxy::kIsRValue)
        return false;

    if (pyobj->ObjectIsA() && Cppyy::IsSubtype(pyobj->ObjectIsA(), fClass)) {
    // calculate offset between formal and actual arguments
        para.fValue.fVoidp = pyobj->GetObject();
        if (pyobj->ObjectIsA() != fClass) {
            para.fValue.fLong += Cppyy::GetBaseOffset(
                pyobj->ObjectIsA(), fClass, para.fValue.fVoidp, 1 /* up-cast */);
        }

       para.fTypeCode = 'V';
       return true;
    }
/* TODO: remove usage of TClass
    else if (!TClass::GetClass( Cppyy::GetFinalName( fClass ).c_str() )->GetClassInfo()) {
    // assume "user knows best" to allow anonymous reference passing
        para.fValue.fVoidp = pyobj->GetObject();
        para.fTypeCode = 'V';
        return true;
    }
*/
    return false;
}

//-----------------------------------------------------------------------------
#if PY_VERSION_HEX < 0x03000000
const size_t refcount_cutoff = 1;
#else
// p3 has at least 2 ref-counts, as contrary to p2, it will create a descriptor
// copy for the method holding self in the case of __init__; but there can also
// be a reference held by the frame object, which is indistinguishable from a
// local variable reference, so the cut-off has to remain 2.
const size_t refcount_cutoff = 2;
#endif

bool CPyCppyy::TMoveCppObjectConverter::SetArg(
        PyObject* pyobject, TParameter& para, TCallContext* ctxt)
{
// convert <pyobject> to C++ instance&&, set arg for call
    if (!ObjectProxy_Check(pyobject))
        return false;
    ObjectProxy* pyobj = (ObjectProxy*)pyobject;

// moving is same as by-ref, but have to check that move is allowed
    int moveit_reason = 0;
    if (pyobj->fFlags & ObjectProxy::kIsRValue) {
        pyobj->fFlags &= ~ObjectProxy::kIsRValue;
        moveit_reason = 2;
    } else if (pyobject->ob_refcnt == refcount_cutoff) {
        moveit_reason = 1;
    }

    if (moveit_reason) {
        bool result = this->TRefCppObjectConverter::SetArg(pyobject, para, ctxt);
        if (!result && moveit_reason == 2)       // restore the movability flag?
            ((ObjectProxy*)pyobject)->fFlags |= ObjectProxy::kIsRValue;
        return result;
    }

    PyErr_SetString(PyExc_ValueError, "object is not an rvalue");
    return false;      // not a temporary or movable object
}

//-----------------------------------------------------------------------------
template <bool ISREFERENCE>
bool CPyCppyy::TCppObjectPtrConverter<ISREFERENCE>::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )
{
// convert <pyobject> to C++ instance**, set arg for call
   if ( ! ObjectProxy_Check( pyobject ) )
      return false;              // not a cppyy object (TODO: handle SWIG etc.)

   if ( Cppyy::IsSubtype( ((ObjectProxy*)pyobject)->ObjectIsA(), fClass ) ) {
   // depending on memory policy, some objects need releasing when passed into functions
      if ( ! KeepControl() && ! UseStrictOwnership( ctxt ) )
         ((ObjectProxy*)pyobject)->Release();

   // set pointer (may be null) and declare success
      if( ((ObjectProxy*)pyobject)->fFlags & ObjectProxy::kIsReference)
        // If given object is already a reference (aka pointer) then we should not take the address of it
        para.fValue.fVoidp = ((ObjectProxy*)pyobject)->fObject;
      else
        para.fValue.fVoidp = &((ObjectProxy*)pyobject)->fObject;
      para.fTypeCode = ISREFERENCE ? 'V' : 'p';
      return true;
   }

   return false;
}

//-----------------------------------------------------------------------------
template <bool ISREFERENCE>
PyObject* CPyCppyy::TCppObjectPtrConverter<ISREFERENCE>::FromMemory( void* address )
{
// construct python object from C++ instance* read at <address>
   return BindCppObject( address, fClass, true );
}

//-----------------------------------------------------------------------------
template <bool ISREFERENCE>
bool CPyCppyy::TCppObjectPtrConverter<ISREFERENCE>::ToMemory( PyObject* value, void* address )
{
// convert <value> to C++ instance*, write it at <address>
   if ( ! ObjectProxy_Check( value ) )
      return false;              // not a cppyy object (TODO: handle SWIG etc.)

   if ( Cppyy::IsSubtype( ((ObjectProxy*)value)->ObjectIsA(), fClass ) ) {
   // depending on memory policy, some objects need releasing when passed into functions
      if ( ! KeepControl() && TCallContext::sMemoryPolicy != TCallContext::kUseStrict )
         ((ObjectProxy*)value)->Release();

   // set pointer (may be null) and declare success
      *(void**)address = ((ObjectProxy*)value)->GetObject();
      return true;
   }

   return false;
}


namespace CPyCppyy {
// Instantiate the templates
   template class TCppObjectPtrConverter<true>;
   template class TCppObjectPtrConverter<false>;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TCppObjectArrayConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* txt */ )
{
// convert <pyobject> to C++ instance**, set arg for call
   if ( ! TTupleOfInstances_CheckExact( pyobject ) )
      return false;              // no guarantee that the tuple is okay

// treat the first instance of the tuple as the start of the array, and pass it
// by pointer (TODO: store and check sizes)
   if ( PyTuple_Size( pyobject ) < 1 )
      return false;

   PyObject* first = PyTuple_GetItem( pyobject, 0 );
   if ( ! ObjectProxy_Check( first ) )
      return false;              // should not happen

   if ( Cppyy::IsSubtype( ((ObjectProxy*)first)->ObjectIsA(), fClass ) ) {
   // no memory policies supported; set pointer (may be null) and declare success
      para.fValue.fVoidp = ((ObjectProxy*)first)->fObject;
      para.fTypeCode = 'p';
      return true;
   }

   return false;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectArrayConverter::FromMemory( void* address )
{
// construct python tuple of instances from C++ array read at <address>
   if ( m_size <= 0 )   // if size unknown, just hand out the first object
      return BindCppObjectNoCast( address, fClass );

   return BindCppObjectArray( address, fClass, m_size );
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TCppObjectArrayConverter::ToMemory( PyObject* /* value */, void* /* address */ )
{
// convert <value> to C++ array of instances, write it at <address>

// TODO: need to have size both for the array and from the input
   PyErr_SetString( PyExc_NotImplementedError,
      "access to C-arrays of objects not yet implemented!" );
   return false;
}

//____________________________________________________________________________
// CLING WORKAROUND -- classes for STL iterators are completely undefined in that
// they come in a bazillion different guises, so just do whatever
bool CPyCppyy::TSTLIteratorConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
   if ( ! ObjectProxy_Check( pyobject ) )
      return false;

// just set the pointer value, no check
   ObjectProxy* pyobj = (ObjectProxy*)pyobject;
   para.fValue.fVoidp = pyobj->GetObject();
   para.fTypeCode = 'V';
   return true;
}
// -- END CLING WORKAROUND

//-----------------------------------------------------------------------------
bool CPyCppyy::TVoidPtrRefConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ void*&, set arg for call
   if ( ObjectProxy_Check( pyobject ) ) {
      para.fValue.fVoidp = &((ObjectProxy*)pyobject)->fObject;
      para.fTypeCode = 'V';
      return true;
   }

   return false;
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TVoidPtrPtrConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// convert <pyobject> to C++ void**, set arg for call
   if ( ObjectProxy_Check( pyobject ) ) {
   // this is a C++ object, take and set its address
      para.fValue.fVoidp = &((ObjectProxy*)pyobject)->fObject;
      para.fTypeCode = 'p';
      return true;
   }

// buffer objects are allowed under "user knows best"
   int buflen = Utility::GetBuffer( pyobject, '*', 1, para.fValue.fVoidp, false );

// ok if buffer exists (can't perform any useful size checks)
   if ( para.fValue.fVoidp && buflen != 0 ) {
      para.fTypeCode = 'p';
      return true;
   }

   return false;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TVoidPtrPtrConverter::FromMemory( void* address )
{
// read a void** from address; since this is unknown, long is used (user can cast)
   if ( ! address || *(ptrdiff_t*)address == 0 ) {
      Py_INCREF( gNullPtrObject );
      return gNullPtrObject;
   }
   return BufFac_t::Instance()->PyBuffer_FromMemory( (Long_t*)*(ptrdiff_t**)address, sizeof(void*) );
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TPyObjectConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* /* ctxt */ )
{
// by definition: set and declare success
   para.fValue.fVoidp = pyobject;
   para.fTypeCode = 'p';
   return true;
}

PyObject* CPyCppyy::TPyObjectConverter::FromMemory( void* address )
{
// construct python object from C++ PyObject* read at <address>
   PyObject* pyobject = *((PyObject**)address);

   if ( ! pyobject ) {
      Py_RETURN_NONE;
   }

   Py_INCREF( pyobject );
   return pyobject;
}

bool CPyCppyy::TPyObjectConverter::ToMemory( PyObject* value, void* address )
{
// no conversion needed, write <value> at <address>
   Py_INCREF( value );
   *((PyObject**)address) = value;
   return true;
}


//- smart pointer converters --------------------------------------------------
bool CPyCppyy::TSmartPtrCppObjectConverter::SetArg(
      PyObject* pyobject, TParameter& para, TCallContext* ctxt )
{
   char typeCode = fHandlePtr ? 'p' : 'V';

   if ( ! ObjectProxy_Check( pyobject ) ) {
      if ( fHandlePtr && GetAddressSpecialCase( pyobject, para.fValue.fVoidp ) ) {
         para.fTypeCode = typeCode;      // allow special cases such as NULL
         return true;
      }

      return false;
   }

   ObjectProxy* pyobj = (ObjectProxy*)pyobject;

// for the case where we have a 'hidden' smart pointer:
   if ( pyobj->fFlags & ObjectProxy::kIsSmartPtr && Cppyy::IsSubtype( pyobj->fSmartPtrType, fClass ) ) {
   // depending on memory policy, some objects need releasing when passed into functions
      if ( fKeepControl && ! UseStrictOwnership( ctxt ) )
         ((ObjectProxy*)pyobject)->Release();

   // calculate offset between formal and actual arguments
      para.fValue.fVoidp = pyobj->fSmartPtr;
      if ( pyobj->fSmartPtrType != fClass ) {
         para.fValue.fLong += Cppyy::GetBaseOffset(
            pyobj->fSmartPtrType, fClass, para.fValue.fVoidp, 1 /* up-cast */ );
      }

   // set pointer (may be null) and declare success
      para.fTypeCode = typeCode;
      return true;
   }

// for the case where we have an 'exposed' smart pointer:
   if ( pyobj->ObjectIsA() && Cppyy::IsSubtype( pyobj->ObjectIsA(), fClass ) ) {
   // calculate offset between formal and actual arguments
      para.fValue.fVoidp = pyobj->GetObject();
      if ( pyobj->ObjectIsA() != fClass ) {
         para.fValue.fLong += Cppyy::GetBaseOffset(
            pyobj->ObjectIsA(), fClass, para.fValue.fVoidp, 1 /* up-cast */ );
      }

   // set pointer (may be null) and declare success
      para.fTypeCode = typeCode;
      return true;
   }

   return false;
}

PyObject* CPyCppyy::TSmartPtrCppObjectConverter::FromMemory( void* address )
{
   if ( !address || !fClass )
      return nullptr;

// obtain raw pointer
   std::vector<TParameter> args;
   ObjectProxy* pyobj = (ObjectProxy*) BindCppObject(
      Cppyy::CallR( (Cppyy::TCppMethod_t)fDereferencer, address, &args ), fRawPtrType );
   if ( pyobj )
      pyobj->SetSmartPtr( (void*)address, fClass );

   return (PyObject*)pyobj;
}


//-----------------------------------------------------------------------------
bool CPyCppyy::TNotImplementedConverter::SetArg( PyObject*, TParameter&, TCallContext* )
{
// raise a NotImplemented exception to take a method out of overload resolution
   PyErr_SetString( PyExc_NotImplementedError, "this method can not (yet) be called" );
   return false;
}


//- factories -----------------------------------------------------------------
CPyCppyy::TConverter* CPyCppyy::CreateConverter(const std::string& fullType, long size)
{
// The matching of the fulltype to a converter factory goes through up to five levels:
//   1) full, exact match
//   2) match of decorated, unqualified type
//   3) accept const ref as by value
//   4) accept ref as pointer
//   5) generalized cases (covers basically all C++ classes)
//
// If all fails, void is used, which will generate a run-time warning when used.

// an exactly matching converter is best
    ConvFactories_t::iterator h = gConvFactories.find(fullType);
    if (h != gConvFactories.end())
        return (h->second)(size);

// resolve typedefs etc.
    const std::string& resolvedType = Cppyy::ResolveName(fullType);

// a full, qualified matching converter is preferred
    if (resolvedType != fullType) {
        h = gConvFactories.find(resolvedType);
        if (h != gConvFactories.end())
            return (h->second)(size);
    }

//-- nothing? ok, collect information about the type and possible qualifiers/decorators
    bool isConst = strncmp(resolvedType.c_str(), "const", 5)  == 0;
    const std::string& cpd = Utility::Compound(resolvedType);
    std::string realType   = TypeManip::clean_type(resolvedType, false, true);

// accept unqualified type (as python does not know about qualifiers)
    h = gConvFactories.find(realType + cpd);
    if (h != gConvFactories.end())
        return (h->second)(size);

// drop const, as that is mostly meaningless to python (with the exception
// of c-strings, but those are specialized in the converter map)
    if (isConst) {
        realType = TypeManip::remove_const(realType);
        h = gConvFactories.find(realType + cpd);
        if (h != gConvFactories.end())
            return (h->second)(size);
    }

// CLING WORKAROUND -- if the type is a fixed-size array, it will have a funky
// resolved type like MyClass(&)[N], which TClass::GetClass() fails on. So, strip
// it down:
/* TODO: remove TClassEdit usage
   if ( cpd == "[]" )
      realType = TClassEdit::CleanType( realType.substr( 0, realType.rfind("(") ).c_str(), 1 );
*/
// -- CLING WORKAROUND

//-- still nothing? try pointer instead of array (for builtins)
    if (cpd == "[]") {
        h = gConvFactories.find(realType + "*");
        if (h != gConvFactories.end())
            return (h->second)(size);
    }

//-- still nothing? use a generalized converter
    bool control = cpd == "&" || isConst;

// converters for known C++ classes and default (void*)
    TConverter* result = 0;
    if (Cppyy::TCppScope_t klass = Cppyy::GetScope(realType)) {
        if (Cppyy::IsSmartPtr(realType)) {
            const std::vector<Cppyy::TCppIndex_t> methods =
                Cppyy::GetMethodIndicesFromName(klass, "operator->");
            if (!methods.empty()) {
                Cppyy::TCppMethod_t method = Cppyy::GetMethod(klass, methods[0]);
                Cppyy::TCppType_t rawPtrType = Cppyy::GetScope(
                    TypeManip::clean_type(Cppyy::GetMethodResultType(method)));
                if (rawPtrType) {
                    if (cpd == "") {
                        result = new TSmartPtrCppObjectConverter(klass, rawPtrType, method, control);
                    } else if (cpd == "&") {
                        result = new TSmartPtrCppObjectConverter(klass, rawPtrType, method);
                    } else if (cpd == "*" && size <= 0) {
                        result = new TSmartPtrCppObjectConverter(klass, rawPtrType, method, control, true);
                //  } else if (cpd == "**" || cpd == "*&" || cpd == "&*") {
                //  } else if (cpd == "[]" || size > 0) {
                //  } else {
                    }
                }
            }
        }

        if (!result) {
        // CLING WORKAROUND -- special case for STL iterators
            if (realType.find("__gnu_cxx::__normal_iterator", 0) /* vector */ == 0)
                result = new TSTLIteratorConverter();
            else
       // -- CLING WORKAROUND
            if (cpd == "**" || cpd == "&*")
                result = new TCppObjectPtrConverter<false>(klass, control);
            else if (cpd == "*&" )
                result = new TCppObjectPtrConverter<true>(klass, control);
            else if (cpd == "*" && size <= 0 )
                result = new TCppObjectConverter(klass, control);
            else if (cpd == "&" )
                result = new TRefCppObjectConverter(klass);
            else if (cpd == "&&" )
                result = new TMoveCppObjectConverter(klass);
            else if (cpd == "[]" || size > 0)
                result = new TCppObjectArrayConverter(klass, size, false);
            else if (cpd == "" )               // by value
                result = new TValueCppObjectConverter(klass, true);
        }
    } else if (Cppyy::IsEnum(realType)) {
    // special case (Cling): represent enums as unsigned integers
        if (cpd == "&")
            h = isConst ? gConvFactories.find("const long&") : gConvFactories.find("long&");
        else
            h = gConvFactories.find("UInt_t");
    } else if (realType.find("(*)") != std::string::npos ||
               (realType.find("::*)") != std::string::npos)) {
    // this is a function function pointer
    // TODO: find better way of finding the type
    // TODO: a converter that generates wrappers as appropriate
        h = gConvFactories.find("void*");
    }

    if (!result && cpd == "&&")                   // unhandled moves
        result = new TNotImplementedConverter();

    if (!result && h != gConvFactories.end())
    // converter factory available, use it to create converter
        result = (h->second)(size);
    else if (!result) {
        if (cpd != "") {
            result = new TVoidArrayConverter();       // "user knows best"
        } else {
            result = new TVoidConverter();            // fails on use
        }
    }

    return result;
}

//-----------------------------------------------------------------------------
#define CPYCPPYY_BASIC_CONVERTER_FACTORY( name )                             \
TConverter* Create##name##Converter( Long_t )                                \
{                                                                            \
   return new T##name##Converter();                                          \
}

#define CPYCPPYY_ARRAY_CONVERTER_FACTORY( name )                             \
TConverter* Create##name##Converter( Long_t size )                           \
{                                                                            \
   return new T##name##Converter( size );                                    \
}

//-----------------------------------------------------------------------------
namespace {
   using namespace CPyCppyy;

// use macro rather than template for portability ...
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Bool )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstBoolRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Char )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstCharRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( UChar )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstUCharRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Short )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstShortRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( UShort )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstUShortRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Int )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( IntRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstIntRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( UInt )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstUIntRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Long )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( LongRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstLongRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ULong )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstULongRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Float )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstFloatRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Double )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( DoubleRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstDoubleRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( LongDouble )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstLongDoubleRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( Void )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( LongLong )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstLongLongRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ULongLong )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( ConstULongLongRef )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( CString )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( NonConstCString )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( NonConstUCString )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( BoolArray )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( BoolArrayRef )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( ShortArray )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( ShortArrayRef )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( UShortArray )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( UShortArrayRef )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( IntArray )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( UIntArray )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( UIntArrayRef )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( LongArray )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( ULongArray )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( ULongArrayRef )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( FloatArray )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( FloatArrayRef )
   CPYCPPYY_ARRAY_CONVERTER_FACTORY( DoubleArray )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( VoidArray )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( LongLongArray )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( STLString )
#if __cplusplus > 201402L
   CPYCPPYY_BASIC_CONVERTER_FACTORY( STLStringView )
#endif
   CPYCPPYY_BASIC_CONVERTER_FACTORY( VoidPtrRef )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( VoidPtrPtr )
   CPYCPPYY_BASIC_CONVERTER_FACTORY( PyObject )

// converter factories for C++ types
   typedef std::pair< const char*, ConverterFactory_t > NFp_t;

   // clang-format off
   NFp_t factories_[] = {
   // factories for built-ins
      NFp_t( "bool",                      &CreateBoolConverter               ),
      NFp_t( "const bool&",               &CreateConstBoolRefConverter       ),
      NFp_t( "char",                      &CreateCharConverter               ),
      NFp_t( "const char&",               &CreateConstCharRefConverter       ),
      NFp_t( "signed char",               &CreateCharConverter               ),
      NFp_t( "const signed char&",        &CreateConstCharRefConverter       ),
      NFp_t( "unsigned char",             &CreateUCharConverter              ),
      NFp_t( "const unsigned char&",      &CreateConstUCharRefConverter      ),
      NFp_t( "short",                     &CreateShortConverter              ),
      NFp_t( "const short&",              &CreateConstShortRefConverter      ),
      NFp_t( "unsigned short",            &CreateUShortConverter             ),
      NFp_t( "const unsigned short&",     &CreateConstUShortRefConverter     ),
      NFp_t( "int",                       &CreateIntConverter                ),
      NFp_t( "int&",                      &CreateIntRefConverter             ),
      NFp_t( "const int&",                &CreateConstIntRefConverter        ),
      NFp_t( "unsigned int",              &CreateUIntConverter               ),
      NFp_t( "const unsigned int&",       &CreateConstUIntRefConverter       ),
      NFp_t( "UInt_t", /* enum */         &CreateIntConverter /* yes: Int */ ),
      NFp_t( "long",                      &CreateLongConverter               ),
      NFp_t( "long&",                     &CreateLongRefConverter            ),
      NFp_t( "const long&",               &CreateConstLongRefConverter       ),
      NFp_t( "unsigned long",             &CreateULongConverter              ),
      NFp_t( "const unsigned long&",      &CreateConstULongRefConverter      ),
      NFp_t( "long long",                 &CreateLongLongConverter           ),
      NFp_t( "const long long&",          &CreateConstLongLongRefConverter   ),
      NFp_t( "Long64_t",                  &CreateLongLongConverter           ),
      NFp_t( "const Long64_t&",           &CreateConstLongLongRefConverter   ),
      NFp_t( "unsigned long long",        &CreateULongLongConverter          ),
      NFp_t( "const unsigned long long&", &CreateConstULongLongRefConverter  ),
      NFp_t( "ULong64_t",                 &CreateULongLongConverter          ),
      NFp_t( "const ULong64_t&",          &CreateConstULongLongRefConverter  ),

      NFp_t( "float",                     &CreateFloatConverter              ),
      NFp_t( "const float&",              &CreateConstFloatRefConverter      ),
      NFp_t( "double",                    &CreateDoubleConverter             ),
      NFp_t( "double&",                   &CreateDoubleRefConverter          ),
      NFp_t( "const double&",             &CreateConstDoubleRefConverter     ),
      NFp_t( "long double",               &CreateLongDoubleConverter         ),
      NFp_t( "const long double&",        &CreateConstLongDoubleRefConverter ),
      NFp_t( "void",                      &CreateVoidConverter               ),

   // pointer/array factories
      NFp_t( "bool*",                     &CreateBoolArrayConverter          ),
      NFp_t( "bool&",                     &CreateBoolArrayRefConverter       ),
      NFp_t( "const unsigned char*",      &CreateCStringConverter            ),
      NFp_t( "unsigned char*",            &CreateNonConstUCStringConverter   ),
      NFp_t( "short*",                    &CreateShortArrayConverter         ),
      NFp_t( "short&",                    &CreateShortArrayRefConverter      ),
      NFp_t( "unsigned short*",           &CreateUShortArrayConverter        ),
      NFp_t( "unsigned short&",           &CreateUShortArrayRefConverter     ),
      NFp_t( "int*",                      &CreateIntArrayConverter           ),
      NFp_t( "unsigned int*",             &CreateUIntArrayConverter          ),
      NFp_t( "unsigned int&",             &CreateUIntArrayRefConverter       ),
      NFp_t( "long*",                     &CreateLongArrayConverter          ),
      NFp_t( "unsigned long*",            &CreateULongArrayConverter         ),
      NFp_t( "unsigned long&",            &CreateULongArrayRefConverter      ),
      NFp_t( "float*",                    &CreateFloatArrayConverter         ),
      NFp_t( "float&",                    &CreateFloatArrayRefConverter      ),
      NFp_t( "double*",                   &CreateDoubleArrayConverter        ),
      NFp_t( "long long*",                &CreateLongLongArrayConverter      ),
      NFp_t( "Long64_t*",                 &CreateLongLongArrayConverter      ),
      NFp_t( "unsigned long long*",       &CreateLongLongArrayConverter      ),  // TODO: ULongLong
      NFp_t( "ULong64_t*",                &CreateLongLongArrayConverter      ),  // TODO: ULongLong
      NFp_t( "void*",                     &CreateVoidArrayConverter          ),

   // factories for special cases
      NFp_t( "const char*",               &CreateCStringConverter            ),
      NFp_t( "const char[]",              &CreateCStringConverter            ),
      NFp_t( "char*",                     &CreateNonConstCStringConverter    ),
      NFp_t( "std::string",               &CreateSTLStringConverter          ),
      NFp_t( "string",                    &CreateSTLStringConverter          ),
      NFp_t( "const std::string&",        &CreateSTLStringConverter          ),
      NFp_t( "const string&",             &CreateSTLStringConverter          ),
#if __cplusplus > 201402L
      NFp_t( "std::string_view",          &CreateSTLStringViewConverter      ),
      NFp_t( "string_view",               &CreateSTLStringViewConverter      ),
      NFp_t( "experimental::basic_string_view<char,char_traits<char> >",&CreateSTLStringViewConverter),
#endif
      NFp_t( "void*&",                    &CreateVoidPtrRefConverter         ),
      NFp_t( "void**",                    &CreateVoidPtrPtrConverter         ),
      NFp_t( "PyObject*",                 &CreatePyObjectConverter           ),
      NFp_t( "_object*",                  &CreatePyObjectConverter           ),
      NFp_t( "FILE*",                     &CreateVoidArrayConverter          ),
      NFp_t( "Float16_t",                 &CreateFloatConverter              ),
      NFp_t( "const Float16_t&",          &CreateConstFloatRefConverter      ),
      NFp_t( "Double32_t",                &CreateDoubleConverter             ),
      NFp_t( "Double32_t&",               &CreateDoubleRefConverter          ),
      NFp_t( "const Double32_t&",         &CreateConstDoubleRefConverter     )
   };
   // clang-format on

   static struct InitConvFactories_t {
   public:
      InitConvFactories_t()
      {
      // load all converter factories in the global map 'gConvFactories'
         int nf = sizeof( factories_ ) / sizeof( factories_[ 0 ] );
         for ( int i = 0; i < nf; ++i ) {
            gConvFactories[ factories_[ i ].first ] = factories_[ i ].second;
         }
      }
   } initConvFactories_;

} // unnamed namespace
