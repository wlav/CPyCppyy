// Bindings
#include "CPyCppyy.h"
#include "CPPExcInstance.h"
#include "PyStrings.h"


//______________________________________________________________________________
//                     Python-side exception proxy objects
//                     ===================================
//
// Python exceptions need to derive from PyException as it expects a set of
// specific data members. This class exists so that general CPPInstances need
// not carry that overhead. In use, it forwards to the embedded CPPInstance.


namespace CPyCppyy {

//= CPyCppyy exception object proxy construction/destruction =================
static PyObject* ep_new(PyTypeObject* subtype, PyObject* args, PyObject* kwds)
{
// Create a new exception object proxy (holder only).
    PyObject* pyobj = ((PyTypeObject*)PyExc_Exception)->tp_new(subtype, nullptr, nullptr);
    if (!pyobj)
        return nullptr;

    CPPExcInstance* excobj = (CPPExcInstance*)pyobj;
    if (args) {
        PyObject* ulc = PyObject_GetAttr((PyObject*)subtype, PyStrings::gUnderlying);
        excobj->fCppInstance = PyType_Type.tp_call(ulc, args, kwds);
        if (!excobj->fCppInstance) {
        // if this fails, then the contruction may have been from PyErr_Format, in
        // which case the argument is simply the message, so drop the proxy (which
        // is only used for type matching at that point) and use only the message
            PyErr_Clear();
            if (PyTuple_GET_SIZE(args) == 1) {
                PyObject* msg = PyTuple_GET_ITEM(args, 0);
                if (CPyCppyy_PyText_Check(msg)) {
                    Py_INCREF(msg);
                    excobj->fAltMessage = msg;
                }
            }
        } else
            excobj->fAltMessage = nullptr;
        Py_DECREF(ulc);
    } else {
        excobj->fCppInstance = nullptr;
        excobj->fAltMessage  = nullptr;
    }

    return pyobj;
}

//----------------------------------------------------------------------------
static int ep_traverse(CPPExcInstance* pyobj, visitproc visit, void* args)
{
// Garbage collector traverse of held python member objects.
    if (pyobj->fCppInstance)
        visit(pyobj->fCppInstance, args);

    if (pyobj->fAltMessage)
        visit(pyobj->fAltMessage, args);

    return 0;
}

//----------------------------------------------------------------------------
static PyObject* ep_str(CPPExcInstance* self)
{
    if (self->fCppInstance) {
        PyObject* what = PyObject_CallMethod((PyObject*)self, "what", nullptr);
        if (what)
            return what;
        PyErr_Clear();
        return PyObject_Str(self->fCppInstance);
    }

    if (self->fAltMessage) {
        Py_INCREF(self->fAltMessage);
        return self->fAltMessage;
    }

    return PyType_Type.tp_str((PyObject*)self);
}

//----------------------------------------------------------------------------
static PyObject* ep_repr(CPPExcInstance* self)
{
    if (self->fCppInstance)
        return PyObject_Repr(self->fCppInstance);
    return PyType_Type.tp_repr((PyObject*)self);
}

//----------------------------------------------------------------------------
static void ep_dealloc(CPPExcInstance* pyobj)
{
    PyObject_GC_UnTrack((PyObject*)pyobj);
    Py_CLEAR(pyobj->fCppInstance);
    Py_CLEAR(pyobj->fAltMessage);
}

//----------------------------------------------------------------------------
static int ep_clear(CPPExcInstance* pyobj)
{
// Garbage collector clear of held python member objects.
    Py_CLEAR(pyobj->fCppInstance);
    Py_CLEAR(pyobj->fAltMessage);

    return 0;
}

//= forwarding methods =======================================================
static PyObject* ep_getattro(CPPExcInstance* self, PyObject* attr)
{
    if (self->fCppInstance) {
        PyObject* res = PyObject_GetAttr(self->fCppInstance, attr);
        if (res) return res;
        PyErr_Clear();
    }

    return ((PyTypeObject*)PyExc_Exception)->tp_getattro((PyObject*)self, attr);
}

//----------------------------------------------------------------------------
static int ep_setattro(CPPExcInstance* self, PyObject* attr, PyObject* value)
{
    if (self->fCppInstance) {
        int res = PyObject_SetAttr(self->fCppInstance, attr, value);
        if (!res) return res;
        PyErr_Clear();
    }

    return ((PyTypeObject*)PyExc_Exception)->tp_setattro((PyObject*)self, attr, value);
}

//----------------------------------------------------------------------------
static PyObject* ep_richcompare(CPPExcInstance* self, PyObject* other, int op)
{
    return PyObject_RichCompare(self->fCppInstance, other, op);
}


//= CPyCppyy exception object proxy type ======================================
PyTypeObject CPPExcInstance_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    (char*)"cppyy.CPPExcInstance", // tp_name
    sizeof(CPPExcInstance),        // tp_basicsize
    0,                             // tp_itemsize
    (destructor)ep_dealloc,        // tp_dealloc
    0,                             // tp_print
    0,                             // tp_getattr
    0,                             // tp_setattr
    0,                             // tp_compare
    (reprfunc)ep_repr,             // tp_repr
    0,                             // tp_as_number
    0,                             // tp_as_sequence
    0,                             // tp_as_mapping
    0,                             // tp_hash
    0,                             // tp_call
    (reprfunc)ep_str,              // tp_str
    (getattrofunc)ep_getattro,     // tp_getattro
    (setattrofunc)ep_setattro,     // tp_setattro
    0,                             // tp_as_buffer
    Py_TPFLAGS_DEFAULT |
        Py_TPFLAGS_BASETYPE |
        Py_TPFLAGS_BASE_EXC_SUBCLASS |
        Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_CHECKTYPES,     // tp_flags
    (char*)"cppyy exception object proxy (internal)", // tp_doc
    (traverseproc)ep_traverse,     // tp_traverse
    (inquiry)ep_clear,             // tp_clear
    (richcmpfunc)ep_richcompare,   // tp_richcompare
    0,                             // tp_weaklistoffset
    0,                             // tp_iter
    0,                             // tp_iternext
    0,                             // tp_methods
    0,                             // tp_members
    0,                             // tp_getset
    (PyTypeObject*)PyExc_Exception,    // tp_base
    0,                             // tp_dict
    0,                             // tp_descr_get
    0,                             // tp_descr_set
    0,                             // tp_dictoffset
    0,                             // tp_init
    0,                             // tp_alloc
    (newfunc)ep_new,               // tp_new
    0,                             // tp_free
    0,                             // tp_is_gc
    0,                             // tp_bases
    0,                             // tp_mro
    0,                             // tp_cache
    0,                             // tp_subclasses
    0                              // tp_weaklist
#if PY_VERSION_HEX >= 0x02030000
    , 0                            // tp_del
#endif
#if PY_VERSION_HEX >= 0x02060000
    , 0                            // tp_version_tag
#endif
#if PY_VERSION_HEX >= 0x03040000
    , 0                            // tp_finalize
#endif
};

} // namespace CPyCppyy
