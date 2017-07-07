#ifndef CPYCPPYY_PYSTRINGS_H
#define CPYCPPYY_PYSTRINGS_H

namespace CPyCppyy {

// python strings kept for performance reasons

   namespace PyStrings {

       extern PyObject* gBases;
       extern PyObject* gBase;
       extern PyObject* gClass;
       extern PyObject* gCppEq;
       extern PyObject* gCppNe;
       extern PyObject* gDeref;
       extern PyObject* gDict;
       extern PyObject* gEmptyString;
       extern PyObject* gEq;
       extern PyObject* gFollow;
       extern PyObject* gGetItem;
       extern PyObject* gInit;
       extern PyObject* gIter;
       extern PyObject* gLen;
       extern PyObject* gLifeLine;
       extern PyObject* gModule;
       extern PyObject* gMRO;
       extern PyObject* gName;
       extern PyObject* gCppName;
       extern PyObject* gNe;
       extern PyObject* gTypeCode;

       extern PyObject* gAdd;
       extern PyObject* gSub;
       extern PyObject* gMul;
       extern PyObject* gDiv;

       extern PyObject* gAt;
       extern PyObject* gBegin;
       extern PyObject* gEnd;
       extern PyObject* gFirst;
       extern PyObject* gSecond;
       extern PyObject* gSize;
       extern PyObject* gTemplate;
       extern PyObject* gVectorAt;

       extern PyObject* gThisModule;

   } // namespace PyStrings

   Bool_t CreatePyStrings();
   PyObject* DestroyPyStrings();

} // namespace CPyCppyy

#endif // !CPYCPPYY_PYSTRINGS_H