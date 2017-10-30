#ifndef CPYCPPYY_TSETITEMHOLDER_H
#define CPYCPPYY_TSETITEMHOLDER_H

// Bindings
#include "CPPMethod.h"


namespace CPyCppyy {

class TSetItemHolder : public CPPMethod {
public:
    using CPPMethod::CPPMethod;

public:
    virtual PyCallable* Clone() { return new TSetItemHolder(*this); }
    virtual PyObject* PreProcessArgs(CPPInstance*& self, PyObject* args, PyObject* kwds);

protected:
    virtual bool InitExecutor_(Executor*&, TCallContext* ctxt = nullptr);
};

} // namespace CPyCppyy

#endif // !CPYCPPYY_TSETITEMHOLDER_H
