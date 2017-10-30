#ifndef CPYCPPYY_TCLASSMETHODHOLDER_H
#define CPYCPPYY_TCLASSMETHODHOLDER_H

// Bindings
#include "CPPMethod.h"


namespace CPyCppyy {

class TClassMethodHolder : public CPPMethod {
public:
    using CPPMethod::CPPMethod;

    virtual PyCallable* Clone() { return new TClassMethodHolder(*this); }
    virtual PyObject* Call(
        CPPInstance*&, PyObject* args, PyObject* kwds, TCallContext* ctxt = nullptr);
};

} // namespace CPyCppyy

#endif // !CPYCPPYY_TCLASSMETHODHOLDER_H
