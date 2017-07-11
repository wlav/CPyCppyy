// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"


//- data _____________________________________________________________________
PyObject* CPyCppyy::PyStrings::gBases = 0;
PyObject* CPyCppyy::PyStrings::gBase = 0;
PyObject* CPyCppyy::PyStrings::gClass = 0;
PyObject* CPyCppyy::PyStrings::gCppEq = 0;
PyObject* CPyCppyy::PyStrings::gCppNe = 0;
PyObject* CPyCppyy::PyStrings::gDeref = 0;
PyObject* CPyCppyy::PyStrings::gDict = 0;
PyObject* CPyCppyy::PyStrings::gEmptyString = 0;
PyObject* CPyCppyy::PyStrings::gEq = 0;
PyObject* CPyCppyy::PyStrings::gFollow = 0;
PyObject* CPyCppyy::PyStrings::gGetItem = 0;
PyObject* CPyCppyy::PyStrings::gInit = 0;
PyObject* CPyCppyy::PyStrings::gIter = 0;
PyObject* CPyCppyy::PyStrings::gLen = 0;
PyObject* CPyCppyy::PyStrings::gLifeLine = 0;
PyObject* CPyCppyy::PyStrings::gModule = 0;
PyObject* CPyCppyy::PyStrings::gMRO = 0;
PyObject* CPyCppyy::PyStrings::gName = 0;
PyObject* CPyCppyy::PyStrings::gCppName = 0;
PyObject* CPyCppyy::PyStrings::gNe = 0;
PyObject* CPyCppyy::PyStrings::gTypeCode = 0;

PyObject* CPyCppyy::PyStrings::gAdd = 0;
PyObject* CPyCppyy::PyStrings::gSub = 0;
PyObject* CPyCppyy::PyStrings::gMul = 0;
PyObject* CPyCppyy::PyStrings::gDiv = 0;

PyObject* CPyCppyy::PyStrings::gAt = 0;
PyObject* CPyCppyy::PyStrings::gBegin = 0;
PyObject* CPyCppyy::PyStrings::gEnd = 0;
PyObject* CPyCppyy::PyStrings::gFirst = 0;
PyObject* CPyCppyy::PyStrings::gSecond = 0;
PyObject* CPyCppyy::PyStrings::gSize = 0;
PyObject* CPyCppyy::PyStrings::gTemplate = 0;
PyObject* CPyCppyy::PyStrings::gVectorAt = 0;

PyObject* CPyCppyy::PyStrings::gThisModule = 0;


////////////////////////////////////////////////////////////////////////////////

#define CPYCPPYY_INITIALIZE_STRING( var, str )                                 \
   if ( ! ( PyStrings::var = CPyCppyy_PyUnicode_InternFromString( (char*)#str ) ) )    \
      return kFALSE

Bool_t CPyCppyy::CreatePyStrings() {
// Build cache of commonly used python strings (the cache is python intern, so
// all strings are shared python-wide, not just in cppyy).
   CPYCPPYY_INITIALIZE_STRING( gBases, __bases__ );
   CPYCPPYY_INITIALIZE_STRING( gBase, __base__ );
   CPYCPPYY_INITIALIZE_STRING( gClass, __class__ );
   CPYCPPYY_INITIALIZE_STRING( gCppEq, __cpp_eq__ );
   CPYCPPYY_INITIALIZE_STRING( gCppNe, __cpp_ne__ );
   CPYCPPYY_INITIALIZE_STRING( gDeref, __deref__ );
   CPYCPPYY_INITIALIZE_STRING( gDict, __dict__ );
   if ( ! ( PyStrings::gEmptyString = CPyCppyy_PyUnicode_FromString( (char*)"" ) ) )
      return kFALSE;
   CPYCPPYY_INITIALIZE_STRING( gEq, __eq__ );
   CPYCPPYY_INITIALIZE_STRING( gFollow, __follow__ );
   CPYCPPYY_INITIALIZE_STRING( gGetItem, __getitem__ );
   CPYCPPYY_INITIALIZE_STRING( gInit, __init__ );
   CPYCPPYY_INITIALIZE_STRING( gIter, __iter__ );
   CPYCPPYY_INITIALIZE_STRING( gLen, __len__ );
   CPYCPPYY_INITIALIZE_STRING( gLifeLine, __lifeline );
   CPYCPPYY_INITIALIZE_STRING( gModule, __module__ );
   CPYCPPYY_INITIALIZE_STRING( gMRO, __mro__ );
   CPYCPPYY_INITIALIZE_STRING( gName, __name__ );
   CPYCPPYY_INITIALIZE_STRING( gCppName, __cppname__ );
   CPYCPPYY_INITIALIZE_STRING( gNe, __ne__ );
   CPYCPPYY_INITIALIZE_STRING( gTypeCode, typecode );

   CPYCPPYY_INITIALIZE_STRING( gAdd, __add__ );
   CPYCPPYY_INITIALIZE_STRING( gSub, __sub__ );
   CPYCPPYY_INITIALIZE_STRING( gMul, __mul__ );
   CPYCPPYY_INITIALIZE_STRING( gDiv, CPYCPPYY__div__ );

   CPYCPPYY_INITIALIZE_STRING( gAt, at );
   CPYCPPYY_INITIALIZE_STRING( gBegin, begin );
   CPYCPPYY_INITIALIZE_STRING( gEnd, end );
   CPYCPPYY_INITIALIZE_STRING( gFirst, first );
   CPYCPPYY_INITIALIZE_STRING( gSecond, second );
   CPYCPPYY_INITIALIZE_STRING( gSize, size );
   CPYCPPYY_INITIALIZE_STRING( gTemplate, Template );
   CPYCPPYY_INITIALIZE_STRING( gVectorAt, _vector__at );

   CPYCPPYY_INITIALIZE_STRING( gThisModule, cppyy );

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Remove all cached python strings.

PyObject* CPyCppyy::DestroyPyStrings() {
   Py_DECREF( PyStrings::gBases ); PyStrings::gBases = 0;
   Py_DECREF( PyStrings::gBase ); PyStrings::gBase = 0;
   Py_DECREF( PyStrings::gClass ); PyStrings::gClass = 0;
   Py_DECREF( PyStrings::gCppEq ); PyStrings::gCppEq = 0;
   Py_DECREF( PyStrings::gCppNe ); PyStrings::gCppNe = 0;
   Py_DECREF( PyStrings::gDeref ); PyStrings::gDeref = 0;
   Py_DECREF( PyStrings::gDict ); PyStrings::gDict = 0;
   Py_DECREF( PyStrings::gEmptyString ); PyStrings::gEmptyString = 0;
   Py_DECREF( PyStrings::gEq ); PyStrings::gEq = 0;
   Py_DECREF( PyStrings::gFollow ); PyStrings::gFollow = 0;
   Py_DECREF( PyStrings::gGetItem ); PyStrings::gGetItem = 0;
   Py_DECREF( PyStrings::gInit ); PyStrings::gInit = 0;
   Py_DECREF( PyStrings::gIter ); PyStrings::gIter = 0;
   Py_DECREF( PyStrings::gLen ); PyStrings::gLen = 0;
   Py_DECREF( PyStrings::gLifeLine ); PyStrings::gLifeLine = 0;
   Py_DECREF( PyStrings::gModule ); PyStrings::gModule = 0;
   Py_DECREF( PyStrings::gMRO ); PyStrings::gMRO = 0;
   Py_DECREF( PyStrings::gName ); PyStrings::gName = 0;
   Py_DECREF( PyStrings::gCppName ); PyStrings::gCppName = 0;
   Py_DECREF( PyStrings::gNe ); PyStrings::gNe = 0;
   Py_DECREF( PyStrings::gTypeCode ); PyStrings::gTypeCode = 0;

   Py_DECREF( PyStrings::gAdd ); PyStrings::gAdd = 0;
   Py_DECREF( PyStrings::gSub ); PyStrings::gSub = 0;
   Py_DECREF( PyStrings::gMul ); PyStrings::gMul = 0;
   Py_DECREF( PyStrings::gDiv ); PyStrings::gDiv = 0;

   Py_DECREF( PyStrings::gAt ); PyStrings::gAt = 0;
   Py_DECREF( PyStrings::gBegin ); PyStrings::gBegin = 0;
   Py_DECREF( PyStrings::gEnd ); PyStrings::gEnd = 0;
   Py_DECREF( PyStrings::gFirst ); PyStrings::gFirst = 0;
   Py_DECREF( PyStrings::gSecond ); PyStrings::gSecond = 0;
   Py_DECREF( PyStrings::gSize ); PyStrings::gSize = 0;
   Py_DECREF( PyStrings::gTemplate ); PyStrings::gTemplate = 0;
   Py_DECREF( PyStrings::gVectorAt ); PyStrings::gVectorAt = 0;

   Py_DECREF( PyStrings::gThisModule ); PyStrings::gThisModule = 0;

   Py_INCREF( Py_None );
   return Py_None;
}
