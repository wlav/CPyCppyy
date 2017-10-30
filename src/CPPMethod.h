#ifndef CPYCPPYY_CPPMETHOD_H
#define CPYCPPYY_CPPMETHOD_H

// Bindings
#include "PyCallable.h"

// Standard
#include <string>
#include <vector>


namespace CPyCppyy {

class TExecutor;
class Converter;

class CPPMethod : public PyCallable {
public:
    CPPMethod(Cppyy::TCppScope_t scope, Cppyy::TCppMethod_t method);
    CPPMethod(const CPPMethod&);
    CPPMethod& operator=(const CPPMethod&);
    virtual ~CPPMethod();

public:
    virtual PyObject* GetSignature(bool show_formalargs = true);
    virtual PyObject* GetPrototype(bool show_formalargs = true);
    virtual int       GetPriority();

    virtual int       GetMaxArgs();
    virtual PyObject* GetCoVarNames();
    virtual PyObject* GetArgDefault(int iarg);
    virtual PyObject* GetScopeProxy();

    virtual PyCallable* Clone() { return new CPPMethod(*this); }

public:
    virtual PyObject* Call(
        CPPInstance*& self, PyObject* args, PyObject* kwds, TCallContext* ctxt = nullptr);

    virtual bool      Initialize(TCallContext* ctxt = nullptr);
    virtual PyObject* PreProcessArgs(CPPInstance*& self, PyObject* args, PyObject* kwds);
    virtual bool      ConvertAndSetArgs(PyObject* args, TCallContext* ctxt = nullptr);
    virtual PyObject* Execute(void* self, ptrdiff_t offset, TCallContext* ctxt = nullptr);

protected:
    Cppyy::TCppMethod_t GetMethod()   { return fMethod; }
    Cppyy::TCppScope_t  GetScope()    { return fScope; }
    TExecutor*          GetExecutor() { return fExecutor; }
    std::string         GetSignatureString(bool show_formalargs = true);
    std::string         GetReturnTypeName();

    virtual bool InitExecutor_(TExecutor*&, TCallContext* ctxt = nullptr);

private:
    void Copy_(const CPPMethod&);
    void Destroy_() const;

    PyObject* CallFast(void*, ptrdiff_t, TCallContext*);
    PyObject* CallSafe(void*, ptrdiff_t, TCallContext*);

    bool InitConverters_();

    void SetPyError_( PyObject* msg );

private:
// representation
    Cppyy::TCppMethod_t fMethod;
    Cppyy::TCppScope_t  fScope;
    TExecutor*          fExecutor;

// call dispatch buffers
    std::vector<Converter*> fConverters;

// cached values
    int  fArgsRequired;

// admin
    bool fIsInitialized;
};

} // namespace CPyCppyy

#endif // !CPYCPPYY_CPPMETHOD_H
