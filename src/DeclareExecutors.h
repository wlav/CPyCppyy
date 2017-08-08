#ifndef CPYCPPYY_DECLAREEXECUTORS_H
#define CPYCPPYY_DECLAREEXECUTORS_H

// Bindings
#include "Executors.h"
#include "TCallContext.h"


namespace CPyCppyy {

namespace {

#define CPYCPPYY_DECLARE_BASIC_EXECUTOR(name)                                 \
    class T##name##Executor : public TExecutor {                              \
    public:                                                                   \
        virtual PyObject* Execute(                                            \
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);         \
    }

// executors for built-ins
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Bool);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(BoolConstRef);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Char);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(CharConstRef);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(UChar);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(UCharConstRef);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Short);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Int);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Long);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(ULong);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(LongLong);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(ULongLong);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Float);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Double);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(LongDouble);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Void);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(CString);

// pointer/array executors
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(VoidArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(BoolArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(ShortArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(UShortArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(IntArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(UIntArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(LongArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(ULongArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(FloatArray);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(DoubleArray);

// special cases
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(STLString);

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

    CPYCPPYY_DECLARE_BASIC_EXECUTOR(Constructor);
    CPYCPPYY_DECLARE_BASIC_EXECUTOR(PyObject);

#define CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(name)                              \
    class T##name##RefExecutor : public TRefExecutor {                        \
    public:                                                                   \
        virtual PyObject* Execute(                                            \
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*);         \
    }

    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(Bool);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(Char);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(UChar);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(Short);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(UShort);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(Int);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(UInt);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(Long);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(ULong);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(LongLong);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(ULongLong);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(Float);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(Double);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(LongDouble);
    CPYCPPYY_DECLARE_BASIC_REFEXECUTOR(STLString);

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
