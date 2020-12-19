// Bindings
#include "CPyCppyy.h"
#include "CPPOperator.h"
#include "CPPInstance.h"


//- constructor --------------------------------------------------------------
CPyCppyy::CPPOperator::CPPOperator(
    Cppyy::TCppScope_t scope, Cppyy::TCppMethod_t method, const std::string& name)
        : CPPMethod(scope, method)
{
// a bit silly but doing it this way allows decoupling the initialization order
    if (name == "__mul__")
        fStub = CPPInstance_Type.tp_as_number->nb_multiply;
    else if (name == CPPYY__div__)
#if PY_VERSION_HEX < 0x03000000
        fStub = CPPInstance_Type.tp_as_number->nb_divide;
#else
        fStub = CPPInstance_Type.tp_as_number->nb_true_divide;
#endif
    else if (name == "__add__")
        fStub = CPPInstance_Type.tp_as_number->nb_add;
    else if (name == "__sub__")
        fStub = CPPInstance_Type.tp_as_number->nb_subtract;
    else
        fStub = nullptr;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::CPPOperator::Call(
    CPPInstance*& self, PyObject* args, PyObject* kwds, CallContext* ctxt)
{
// some operators can be a mix of global and class overloads; this method will
// first try class overloads (the existence of this method means that such were
// defined) and if failed, fall back on the global stubs
// TODO: the fact that this is a method and not an overload means that the global
// ones are tried for each method that fails during the overload resolution
    PyObject* result = this->CPPMethod::Call(self, args, kwds, ctxt);
    if (result || !fStub || !self || PyTuple_GET_SIZE(args) != 1)
        return result;

    PyObject* pytype = 0, *pyvalue = 0, *pytrace = 0;
    PyErr_Fetch(&pytype, &pyvalue, &pytrace);

    result = fStub((PyObject*)self, PyTuple_GET_ITEM(args, 0));

    if (!result)
        PyErr_Restore(pytype, pyvalue, pytrace);
    else {
        Py_XDECREF(pytype);
        Py_XDECREF(pyvalue);
        Py_XDECREF(pytrace);
    }

    return result;
}
