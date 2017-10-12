#ifndef CPYCPPYY_TSETITEMHOLDER_H
#define CPYCPPYY_TSETITEMHOLDER_H

// Bindings
#include "TMethodHolder.h"


namespace CPyCppyy {

    class TExecutor;
    class TMemberAdapter;
    class TScopeAdapter;

    class TSetItemHolder : public TMethodHolder {
    public:
        using TMethodHolder::TMethodHolder;

    public:
        virtual PyCallable* Clone() { return new TSetItemHolder(*this); }
        virtual PyObject* PreProcessArgs(ObjectProxy*& self, PyObject* args, PyObject* kwds);

    protected:
        virtual bool InitExecutor_(TExecutor*&, TCallContext* ctxt = nullptr);
    };

} // namespace CPyCppyy

#endif // !CPYCPPYY_TSETITEMHOLDER_H
