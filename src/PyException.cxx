// Standard
#include <string.h>

// Bindings
#include "CPyCppyy.h"
#define CPYCPPYY_INTERNAL 1
#include "CPyCppyy/PyException.h"
#undef CPYCPPYY_INTERNAL


//______________________________________________________________________________
//                 C++ exception for throwing python exceptions
//                 ============================================
// Purpose: A C++ exception class for throwing python exceptions
//          through C++ code.
// Created: Apr, 2004, Scott Snyder, from the version in D0's python_util.
//
// Note: Don't be tempted to declare the virtual functions defined here
//       as inline.
//       If you do, you may not be able to properly throw these
//       exceptions across shared libraries.


//- constructors/destructor --------------------------------------------------
CPyCppyy::PyException::PyException()
{
#ifdef WITH_THREAD
    PyGILState_STATE state = PyGILState_Ensure();
#endif

    PyObject* pytype = nullptr, *pyvalue = nullptr, *pytrace = nullptr;
    PyErr_Fetch(&pytype, &pyvalue, &pytrace);
    if (pytype && pyvalue) {
        const char* tname = PyExceptionClass_Name(pytype);
        if (tname) {
            char* dot = strrchr((char*)tname, '.');
            if (dot) tname = dot+1;
            fMsg += tname;
            fMsg += ": ";
        }

        PyObject* msg = PyObject_Str(pyvalue);
        if (msg) {
           fMsg += CPyCppyy_PyText_AsString(msg);
           Py_DECREF(msg);
        }
    }
    PyErr_Restore(pytype, pyvalue, pytrace);

    if (fMsg.empty())
        fMsg = "python exception";

#ifdef WITH_THREAD
    PyGILState_Release(state);
#endif
}

CPyCppyy::PyException::~PyException() noexcept
{
// destructor
}


//- public members -----------------------------------------------------------
const char* CPyCppyy::PyException::what() const noexcept
{
// Return reason for throwing this exception: a python exception was raised.
    return fMsg.c_str();
}
