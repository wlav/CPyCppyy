#ifndef CPYCPPYY_EXECUTORS_H
#define CPYCPPYY_EXECUTORS_H

// Standard
#include <string>


namespace CPyCppyy {

    class TCallContext;

    class TExecutor {
    public:
        virtual ~TExecutor() {}
        virtual PyObject* Execute(
            Cppyy::TCppMethod_t, Cppyy::TCppObject_t, TCallContext*) = 0;
    };

// special case needed for TSetItemHolder
    class TRefExecutor : public TExecutor {
    public:
        TRefExecutor() : fAssignable(nullptr) {}
        virtual bool SetAssignable(PyObject*);

    protected:
        PyObject* fAssignable;
    };

// create executor from fully qualified type
    TExecutor* CreateExecutor(const std::string& fullType, bool manage_smart_ptr = true);

} // namespace CPyCppyy

#endif // !CPYCPPYY_EXECUTORS_H
