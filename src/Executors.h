#ifndef CPYCPPYY_EXECUTORS_H
#define CPYCPPYY_EXECUTORS_H

// Standard
#include <string>


namespace CPyCppyy {

struct CallContext;

typedef Py_ssize_t dim_t;
typedef dim_t* dims_t;

class Executor {
public:
    virtual ~Executor() {}
    virtual PyObject* Execute(
        Cppyy::TCppMethod_t, Cppyy::TCppObject_t, CallContext*) = 0;
};

// special case needed for CPPSetItem
class RefExecutor : public Executor {
public:
    RefExecutor() : fAssignable(nullptr) {}
    virtual bool SetAssignable(PyObject*);

protected:
    PyObject* fAssignable;
};

// create executor from fully qualified type
Executor* CreateExecutor(const std::string& fullType);

} // namespace CPyCppyy

#endif // !CPYCPPYY_EXECUTORS_H
