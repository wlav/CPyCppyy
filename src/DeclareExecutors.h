#ifndef CPYCPPYY_DECLAREEXECUTORS_H
#define CPYCPPYY_DECLAREEXECUTORS_H

// Bindings
#include "Executors.h"
#include "TCallContext.h"


namespace CPyCppyy {

namespace {

#define CPPYY_DECL_EXEC(name)                                                 \
    class T##name##Executor : public TExecutor {                              \
    public:                                                                   \
        virtual PyObject* Execute(                                            \
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);         \
    }

// executors for built-ins
    CPPYY_DECL_EXEC(Bool);
    CPPYY_DECL_EXEC(BoolConstRef);
    CPPYY_DECL_EXEC(Char);
    CPPYY_DECL_EXEC(CharConstRef);
    CPPYY_DECL_EXEC(UChar);
    CPPYY_DECL_EXEC(UCharConstRef);
    CPPYY_DECL_EXEC(Short);
    CPPYY_DECL_EXEC(Int);
    CPPYY_DECL_EXEC(Long);
    CPPYY_DECL_EXEC(ULong);
    CPPYY_DECL_EXEC(LongLong);
    CPPYY_DECL_EXEC(ULongLong);
    CPPYY_DECL_EXEC(Float);
    CPPYY_DECL_EXEC(Double);
    CPPYY_DECL_EXEC(LongDouble);
    CPPYY_DECL_EXEC(Void);
    CPPYY_DECL_EXEC(CString);

// pointer/array executors
    CPPYY_DECL_EXEC(VoidArray);
    CPPYY_DECL_EXEC(BoolArray);
    CPPYY_DECL_EXEC(ShortArray);
    CPPYY_DECL_EXEC(UShortArray);
    CPPYY_DECL_EXEC(IntArray);
    CPPYY_DECL_EXEC(UIntArray);
    CPPYY_DECL_EXEC(LongArray);
    CPPYY_DECL_EXEC(ULongArray);
    CPPYY_DECL_EXEC(FloatArray);
    CPPYY_DECL_EXEC(DoubleArray);

// special cases
    CPPYY_DECL_EXEC(STLString);

    class TCppObjectExecutor : public TExecutor {
    public:
        TCppObjectExecutor(Cppyy::TCppType_t klass) : fClass(klass) {}
        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t,TCallContext*);

    protected:
        Cppyy::TCppType_t fClass;
    };

    class TCppObjectByValueExecutor : public TCppObjectExecutor {
    public:
        using TCppObjectExecutor::TCppObjectExecutor;
        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t,TCallContext*);
    };

    CPPYY_DECL_EXEC(Constructor);
    CPPYY_DECL_EXEC(PyObject);

#define CPPYY_DECL_REFEXEC(name)                                              \
    class T##name##RefExecutor : public TRefExecutor {                        \
    public:                                                                   \
        virtual PyObject* Execute(                                            \
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);         \
    }

    CPPYY_DECL_REFEXEC(Bool);
    CPPYY_DECL_REFEXEC(Char);
    CPPYY_DECL_REFEXEC(UChar);
    CPPYY_DECL_REFEXEC(Short);
    CPPYY_DECL_REFEXEC(UShort);
    CPPYY_DECL_REFEXEC(Int);
    CPPYY_DECL_REFEXEC(UInt);
    CPPYY_DECL_REFEXEC(Long);
    CPPYY_DECL_REFEXEC(ULong);
    CPPYY_DECL_REFEXEC(LongLong);
    CPPYY_DECL_REFEXEC(ULongLong);
    CPPYY_DECL_REFEXEC(Float);
    CPPYY_DECL_REFEXEC(Double);
    CPPYY_DECL_REFEXEC(LongDouble);
    CPPYY_DECL_REFEXEC(STLString);

// special cases
    class TCppObjectRefExecutor : public TRefExecutor {
    public:
        TCppObjectRefExecutor(Cppyy::TCppType_t klass) : fClass(klass) {}
        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);

    protected:
        Cppyy::TCppType_t fClass;
    };

    class TCppObjectPtrPtrExecutor : public TCppObjectExecutor {
    public:
        using TCppObjectExecutor::TCppObjectExecutor;
        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);
    };

    class TCppObjectPtrRefExecutor : public TCppObjectExecutor {
    public:
        using TCppObjectExecutor::TCppObjectExecutor;
        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);
    };

    class TCppObjectArrayExecutor : public TCppObjectExecutor {
    public:
        TCppObjectArrayExecutor( Cppyy::TCppType_t klass, Py_ssize_t array_size )
            : TCppObjectExecutor(klass), fArraySize(array_size) {}
        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);

    protected:
        Py_ssize_t fArraySize;
    };

// smart pointer executors
    class TCppObjectBySmartPtrExecutor : public TExecutor {
    public:
        TCppObjectBySmartPtrExecutor(Cppyy::TCppType_t klass,
                Cppyy::TCppType_t rawPtrType, Cppyy::TCppMethod_t deref)
            : fClass(klass), fRawPtrType(rawPtrType), fDereferencer(deref) {}

        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);

    protected:
        Cppyy::TCppType_t   fClass;
        Cppyy::TCppType_t   fRawPtrType;
        Cppyy::TCppMethod_t fDereferencer;
    };

    class TCppObjectBySmartPtrPtrExecutor : public TCppObjectBySmartPtrExecutor {
    public:
        using TCppObjectBySmartPtrExecutor::TCppObjectBySmartPtrExecutor;

        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);
    };

    class TCppObjectBySmartPtrRefExecutor : public TRefExecutor {
    public:
        TCppObjectBySmartPtrRefExecutor(Cppyy::TCppType_t klass,
                Cppyy::TCppType_t rawPtrType, Cppyy::TCppMethod_t deref )
            : fClass(klass), fRawPtrType(rawPtrType), fDereferencer(deref) {}

        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t,TCallContext* );

    protected:
        Cppyy::TCppType_t fClass;
        Cppyy::TCppType_t fRawPtrType;
        Cppyy::TCppMethod_t fDereferencer;
    };

} // unnamed namespace

} // namespace CPyCppyy

#endif // !CPYCPPYY_DECLAREEXECUTORS_H
