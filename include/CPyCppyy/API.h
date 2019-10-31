#ifndef CPYCPPYY_TPYTHON
#define CPYCPPYY_TPYTHON

//////////////////////////////////////////////////////////////////////////////
//                                                                          //
// TPython                                                                  //
//                                                                          //
// Access to the python interpreter and API onto CPyCppyy.                  //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////

// Bindings
#include "CPyCppyy/PyResult.h"
#include "CPyCppyy/CommonDefs.h"

// Standard
#include <string>
#include <vector>


namespace CPyCppyy {

// import a python module, making its classes available to Cling
CPYCPPYY_EXPORT bool Import(const std::string& name);

// execute a python statement (e.g. "import sys")
CPYCPPYY_EXPORT bool Exec(const std::string& cmd);

// evaluate a python expression (e.g. "1+1")
CPYCPPYY_EXPORT const PyResult Eval(const std::string& expr);

// execute a python stand-alone script, with argv CLI arguments
CPYCPPYY_EXPORT void ExecScript(const std::string& name, const std::vector<std::string>& args);

// enter an interactive python session (exit with ^D)
CPYCPPYY_EXPORT void Prompt();

// C++ Instance (python object proxy) to void* conversion
void* Instance_AsVoidPtr(PyObject* pyobject);

// void* to C++ Instance (python object proxy) conversion, returns a new reference
PyObject* Instance_FromVoidPtr(
    void* addr, const std::string& classname, bool python_owns = false);

// type verifiers for C++ Scope
CPYCPPYY_EXPORT bool Scope_Check(PyObject* pyobject);
CPYCPPYY_EXPORT bool Scope_CheckExact(PyObject* pyobject);

// type verifiers for C++ Instance
CPYCPPYY_EXPORT bool Instance_Check(PyObject* pyobject);
CPYCPPYY_EXPORT bool Instance_CheckExact(PyObject* pyobject);

// type verifiers for C++ Overload
CPYCPPYY_EXPORT bool Overload_Check(PyObject* pyobject);
CPYCPPYY_EXPORT bool Overload_CheckExact(PyObject* pyobject);

} // namespace CPyCppyy

#endif // !CPYCPPYY_API_H
