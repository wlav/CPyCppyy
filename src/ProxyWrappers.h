#ifndef CPYCPPYY_PROXYWRAPPERS_H
#define CPYCPPYY_PROXYWRAPPERS_H

// Standard
#include <string>


namespace CPyCppyy {

// construct a Python shadow class for the named C++ class
PyObject* GetScopeProxy(Cppyy::TCppScope_t);
PyObject* CreateScopeProxy(Cppyy::TCppScope_t);
PyObject* CreateScopeProxy(PyObject*, PyObject* args);
PyObject* CreateScopeProxy(
    const std::string& scope_name, PyObject* parent = nullptr);

// convenience functions to retrieve global variables and enums
PyObject* GetCppGlobal(const std::string& name);
PyObject* GetCppGlobal(PyObject*, PyObject* args);

// bind a C++ object into a Python proxy object
PyObject* BindCppObjectNoCast(Cppyy::TCppObject_t object, Cppyy::TCppType_t klass,
    bool isRef = false, bool isValue = false);
PyObject* BindCppObject(
    Cppyy::TCppObject_t object, Cppyy::TCppType_t klass, bool isRef = false);
inline PyObject* BindCppObject(
    Cppyy::TCppObject_t object, const std::string& clName, bool isRef = false)
{
    return BindCppObject(object, Cppyy::GetScope(clName), isRef);
}

PyObject* BindCppObjectArray(
    Cppyy::TCppObject_t address, Cppyy::TCppType_t klass, Int_t size);

} // namespace CPyCppyy

#endif // !CPYCPPYY_PROXYWRAPPERS_H
