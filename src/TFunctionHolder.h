#ifndef CPYCPPYY_TFUNCTIONHOLDER_H
#define CPYCPPYY_TFUNCTIONHOLDER_H

// Bindings
#include "TMethodHolder.h"


namespace CPyCppyy {

    class TFunctionHolder : public TMethodHolder {
    public:
        using TMethodHolder::TMethodHolder;

        virtual PyCallable* Clone() { return new TFunctionHolder(*this); }

        virtual PyObject* PreProcessArgs(CPPInstance*& self, PyObject* args, PyObject* kwds);
        virtual PyObject* Call(
            CPPInstance*&, PyObject* args, PyObject* kwds, TCallContext* ctx = nullptr);
    };

} // namespace CPyCppyy

#endif // !CPYCPPYY_TFUNCTIONHOLDER_H
