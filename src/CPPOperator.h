#ifndef CPYCPPYY_CPPOPERATOR_H
#define CPYCPPYY_CPPOPERATOR_H

// Bindings
#include "CPPMethod.h"

// Standard
#include <string>


namespace CPyCppyy {

class CPPOperator : public CPPMethod {
public:
    CPPOperator(Cppyy::TCppScope_t scope, Cppyy::TCppMethod_t method, const std::string& name);

public:
    virtual PyCallable* Clone() { return new CPPOperator(*this); }

    virtual PyObject* Call(
        CPPInstance*&, PyObject* args, PyObject* kwds, CallContext* ctxt = nullptr);

private:
    binaryfunc fStub;
};

} // namespace CPyCppyy

#endif // !CPYCPPYY_CPPOPERATOR_H
