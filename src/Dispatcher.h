#ifndef CPYCPPYY_DISPATCHER_H
#define CPYCPPYY_DISPATCHER_H

// Standard
#include <string>


namespace CPyCppyy {

class CPPScope;

// helper that inserts dispatchers for virtual methods
bool InsertDispatcher(const std::string& name, CPPScope* klass, PyObject* dct);

} // namespace CPyCppyy

#endif // !CPYCPPYY_DISPATCHER_H




