#ifndef CPYCPPYY_TMETHODHOLDER_H
#define CPYCPPYY_TMETHODHOLDER_H

// Bindings
#include "PyCallable.h"

// Standard
#include <string>
#include <vector>


namespace CPyCppyy {

    class TExecutor;
    class TConverter;

    class TMethodHolder : public PyCallable {
    public:
        TMethodHolder(Cppyy::TCppScope_t scope, Cppyy::TCppMethod_t method);
        TMethodHolder(const TMethodHolder&);
        TMethodHolder& operator=(const TMethodHolder&);
        virtual ~TMethodHolder();

    public:
        virtual PyObject* GetSignature(bool show_formalargs = true);
        virtual PyObject* GetPrototype(bool show_formalargs = true);
        virtual int       GetPriority();

        virtual int       GetMaxArgs();
        virtual PyObject* GetCoVarNames();
        virtual PyObject* GetArgDefault(int iarg);
        virtual PyObject* GetScopeProxy();

        virtual PyCallable* Clone() { return new TMethodHolder(*this); }

    public:
        virtual PyObject* Call(
            ObjectProxy*& self, PyObject* args, PyObject* kwds, TCallContext* ctxt = nullptr);

        virtual bool      Initialize(TCallContext* ctxt = nullptr);
        virtual PyObject* PreProcessArgs(ObjectProxy*& self, PyObject* args, PyObject* kwds);
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
        void Copy_(const TMethodHolder&);
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
        std::vector<TConverter*> fConverters;

    // cached values
        int  fArgsRequired;

    // admin
        bool fIsInitialized;
    };

} // namespace CPyCppyy

#endif // !CPYCPPYY_METHODHOLDER_H
