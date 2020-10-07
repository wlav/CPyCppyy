// Bindings
#include "CPyCppyy.h"
#include "Pythonize.h"
#include "Converters.h"
#include "CPPInstance.h"
#include "CPPFunction.h"
#include "CPPOverload.h"
#include "CustomPyTypes.h"
#include "LowLevelViews.h"
#include "ProxyWrappers.h"
#include "PyCallable.h"
#include "PyStrings.h"
#include "TypeManip.h"
#include "Utility.h"

// Standard
#include <algorithm>
#include <complex>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>


//- data and local helpers ---------------------------------------------------
namespace CPyCppyy {
    extern PyObject* gThisModule;
    extern std::map<std::string, std::vector<PyObject*>> gPythonizations;
}

namespace {

// for convenience
using namespace CPyCppyy;

//-----------------------------------------------------------------------------
bool HasAttrDirect(PyObject* pyclass, PyObject* pyname, bool mustBeCPyCppyy = false) {
// prevents calls to Py_TYPE(pyclass)->tp_getattr, which is unnecessary for our
// purposes here and could tickle problems w/ spurious lookups into ROOT meta
    PyObject* dct = PyObject_GetAttr(pyclass, PyStrings::gDict);
    if (dct) {
        PyObject* attr = PyObject_GetItem(dct, pyname);
        Py_DECREF(dct);
        if (attr) {
            bool ret = !mustBeCPyCppyy || CPPOverload_Check(attr);
            Py_DECREF(attr);
            return ret;
        }
    }
    PyErr_Clear();
    return false;
}

PyObject* GetAttrDirect(PyObject* pyclass, PyObject* pyname) {
// get an attribute without causing getattr lookups
    PyObject* dct = PyObject_GetAttr(pyclass, PyStrings::gDict);
    if (dct) {
        PyObject* attr = PyObject_GetItem(dct, pyname);
        Py_DECREF(dct);
        return attr;
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
inline bool IsTemplatedSTLClass(const std::string& name, const std::string& klass) {
// Scan the name of the class and determine whether it is a template instantiation.
    auto pos = name.find(klass);
    return pos == 5 && name.rfind("std::", 0, 5) == 0  && name.find("::", name.rfind(">")) == std::string::npos;
}

// to prevent compiler warnings about const char* -> char*
inline PyObject* CallPyObjMethod(PyObject* obj, const char* meth)
{
// Helper; call method with signature: obj->meth().
    Py_INCREF(obj);
    PyObject* result = PyObject_CallMethod(obj, const_cast<char*>(meth), const_cast<char*>(""));
    Py_DECREF(obj);
    return result;
}

//-----------------------------------------------------------------------------
inline PyObject* CallPyObjMethod(PyObject* obj, const char* meth, PyObject* arg1)
{
// Helper; call method with signature: obj->meth(arg1).
    Py_INCREF(obj);
    PyObject* result = PyObject_CallMethod(
        obj, const_cast<char*>(meth), const_cast<char*>("O"), arg1);
    Py_DECREF(obj);
    return result;
}

//-----------------------------------------------------------------------------
PyObject* PyStyleIndex(PyObject* self, PyObject* index)
{
// Helper; converts python index into straight C index.
    Py_ssize_t idx = PyInt_AsSsize_t(index);
    if (idx == (Py_ssize_t)-1 && PyErr_Occurred())
        return nullptr;

    Py_ssize_t size = PySequence_Size(self);
    if (idx >= size || (idx < 0 && idx < -size)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return nullptr;
    }

    PyObject* pyindex = nullptr;
    if (idx >= 0) {
        Py_INCREF(index);
        pyindex = index;
    } else
        pyindex = PyLong_FromSsize_t(size+idx);

    return pyindex;
}

//-----------------------------------------------------------------------------
inline bool AdjustSlice(const Py_ssize_t nlen, Py_ssize_t& start, Py_ssize_t& stop, Py_ssize_t& step)
{
// Helper; modify slice range to match the container.
    if ((step > 0 && stop <= start) || (step < 0 && start <= stop))
        return false;

    if (start < 0) start = 0;
    if (start >= nlen) start = nlen-1;
    if (step >= nlen) step = nlen;

    stop = step > 0 ? std::min(nlen, stop) : (stop >= 0 ? stop : -1);
    return true;
}

//-----------------------------------------------------------------------------
inline PyObject* CallSelfIndex(CPPInstance* self, PyObject* idx, PyObject* pymeth)
{
// Helper; call method with signature: meth(pyindex).
    Py_INCREF((PyObject*)self);
    PyObject* pyindex = PyStyleIndex((PyObject*)self, idx);
    if (!pyindex) {
        Py_DECREF((PyObject*)self);
        return nullptr;
    }

    PyObject* result = PyObject_CallMethodObjArgs((PyObject*)self, pymeth, pyindex, nullptr);
    Py_DECREF(pyindex);
    Py_DECREF((PyObject*)self);
    return result;
}

//- "smart pointer" behavior ---------------------------------------------------
PyObject* DeRefGetAttr(PyObject* self, PyObject* name)
{
// Follow operator*() if present (available in python as __deref__), so that
// smart pointers behave as expected.
    if (name == PyStrings::gTypeCode || name == PyStrings::gCTypesType) {
    // TODO: these calls come from TemplateProxy and are unlikely to be needed in practice,
    // whereas as-is, they can accidentally dereference the result of end() on some STL
    // containers. Obviously, this is a dumb hack that should be resolved more fundamentally.
        PyErr_SetString(PyExc_AttributeError, CPyCppyy_PyText_AsString(name));
        return nullptr;
    }

    if (!CPyCppyy_PyText_Check(name))
        PyErr_SetString(PyExc_TypeError, "getattr(): attribute name must be string");

    PyObject* pyptr = PyObject_CallMethodObjArgs(self, PyStrings::gDeref, nullptr);
    if (!pyptr)
        return nullptr;

// prevent a potential infinite loop
    if (Py_TYPE(pyptr) == Py_TYPE(self)) {
        PyObject* val1 = PyObject_Str(self);
        PyObject* val2 = PyObject_Str(name);
        PyErr_Format(PyExc_AttributeError, "%s has no attribute \'%s\'",
            CPyCppyy_PyText_AsString(val1), CPyCppyy_PyText_AsString(val2));
        Py_DECREF(val2);
        Py_DECREF(val1);

        Py_DECREF(pyptr);
        return nullptr;
    }

    PyObject* result = PyObject_GetAttr(pyptr, name);
    Py_DECREF(pyptr);
    return result;
}

//-----------------------------------------------------------------------------
PyObject* FollowGetAttr(PyObject* self, PyObject* name)
{
// Follow operator->() if present (available in python as __follow__), so that
// smart pointers behave as expected.
    if (!CPyCppyy_PyText_Check(name))
        PyErr_SetString(PyExc_TypeError, "getattr(): attribute name must be string");

    PyObject* pyptr = PyObject_CallMethodObjArgs(self, PyStrings::gFollow, nullptr);
    if (!pyptr)
         return nullptr;

    PyObject* result = PyObject_GetAttr(pyptr, name);
    Py_DECREF(pyptr);
    return result;
}


//- vector behavior as primitives ----------------------------------------------
#if PY_VERSION_HEX < 0x03040000
#define PyObject_LengthHint _PyObject_LengthHint
#endif

// TODO: can probably use the below getters in the InitializerListConverter
struct ItemGetter {
    ItemGetter(PyObject* pyobj) : fPyObject(pyobj) { Py_INCREF(fPyObject); }
    virtual ~ItemGetter() { Py_DECREF(fPyObject); }
    virtual Py_ssize_t size() = 0;
    virtual PyObject* get() = 0;
    PyObject* fPyObject;
};

struct CountedItemGetter : public ItemGetter {
    CountedItemGetter(PyObject* pyobj) : ItemGetter(pyobj), fCur(0) {}
    Py_ssize_t fCur;
};

struct TupleItemGetter : public CountedItemGetter {
    using CountedItemGetter::CountedItemGetter;
    virtual Py_ssize_t size() { return PyTuple_GET_SIZE(fPyObject); }
    virtual PyObject* get() {
        if (fCur < PyTuple_GET_SIZE(fPyObject)) {
            PyObject* item = PyTuple_GET_ITEM(fPyObject, fCur++);
            Py_INCREF(item);
            return item;
        }
        PyErr_SetString(PyExc_StopIteration, "end of tuple");
        return nullptr;
    }
};

struct ListItemGetter : public CountedItemGetter {
    using CountedItemGetter::CountedItemGetter;
    virtual Py_ssize_t size() { return PyList_GET_SIZE(fPyObject); }
    virtual PyObject* get() {
        if (fCur < PyList_GET_SIZE(fPyObject)) {
            PyObject* item = PyList_GET_ITEM(fPyObject, fCur++);
            Py_INCREF(item);
            return item;
        }
        PyErr_SetString(PyExc_StopIteration, "end of list");
        return nullptr;
    }
};

struct SequenceItemGetter : public CountedItemGetter {
    using CountedItemGetter::CountedItemGetter;
    virtual Py_ssize_t size() {
        Py_ssize_t sz = PySequence_Size(fPyObject);
        if (sz < 0) {
            PyErr_Clear();
            return PyObject_LengthHint(fPyObject, 8);
        }
        return sz;
    }
    virtual PyObject* get() { return PySequence_GetItem(fPyObject, fCur++); }
};

struct IterItemGetter : public ItemGetter {
    using ItemGetter::ItemGetter;
    virtual Py_ssize_t size() { return PyObject_LengthHint(fPyObject, 8); }
    virtual PyObject* get() { return (*(Py_TYPE(fPyObject)->tp_iternext))(fPyObject); }
};

PyObject* VectorInit(PyObject* self, PyObject* args, PyObject* /* kwds */)
{
// Specialized vector constructor to allow construction from containers; allowing
// such construction from initializer_list instead would possible, but can be
// error-prone. This use case is common enough for std::vector to implement it
// directly, except for arrays (which can be passed wholesale) and strings (which
// won't convert properly as they'll be seen as buffers)

    ItemGetter* getter = nullptr;
    if (PyTuple_GET_SIZE(args) == 1) {
        PyObject* fi = PyTuple_GET_ITEM(args, 0);
        if (CPyCppyy_PyText_Check(fi) || PyBytes_Check(fi)) {
            PyErr_SetString(PyExc_TypeError, "can not convert string to vector");
            return nullptr;
        }
    // TODO: this only tests for new-style buffers, which is too strict, but a
    // generic check for Py_TYPE(fi)->tp_as_buffer is too loose (note that the
    // main use case is numpy, which offers the new interface)
        if (!PyObject_CheckBuffer(fi)) {
            if (PyTuple_CheckExact(fi))
                getter = new TupleItemGetter(fi);
            else if (PyList_CheckExact(fi))
                getter = new ListItemGetter(fi);
            else if (PySequence_Check(fi))
                getter = new SequenceItemGetter(fi);
            else {
                PyObject* iter = PyObject_GetIter(fi);
                if (iter) {
                    getter = new IterItemGetter{iter};
                    Py_DECREF(iter);
                }
                else PyErr_Clear();
            }
        }
    }

    if (getter) {
    // construct an empty vector, then back-fill it
        PyObject* result = PyObject_CallMethodObjArgs(self, PyStrings::gRealInit, nullptr);
        if (!result) {
            delete getter;
            return nullptr;
        }

        Py_ssize_t sz = getter->size();
        if (sz < 0) {
            Py_DECREF(result);
            delete getter;
            return nullptr;
        }

    // reserve memory as appliable
        if (0 < sz) {
            PyObject* res = PyObject_CallMethod(self, (char*)"reserve", (char*)"n", sz);
            Py_DECREF(res);
        } else { // empty container
            Py_DECREF(result);
            delete getter;
            return result;
        }

        bool fill_ok = true;

    // two main options: a list of lists (or tuples), or a list of objects; the former
    // are emplace_back'ed, the latter push_back'ed
        PyObject* fi = PySequence_GetItem(PyTuple_GET_ITEM(args, 0), 0);
        if (!fi) PyErr_Clear();
        if (fi && (PyTuple_CheckExact(fi) || PyList_CheckExact(fi))) {
        // use emplace_back to construct the vector entries one by one
            PyObject* eb_call = PyObject_GetAttrString(self, (char*)"emplace_back");
            PyObject* vtype = GetAttrDirect((PyObject*)Py_TYPE(self), PyStrings::gValueType);
            bool value_is_vector = false;
            if (vtype && CPyCppyy_PyText_Check(vtype)) {
            // if the value_type is a vector, then allow for initialization from sequences
                if (std::string(CPyCppyy_PyText_AsString(vtype)).rfind("std::vector", 0) != std::string::npos)
                    value_is_vector = true;
            } else
                PyErr_Clear();
            Py_XDECREF(vtype);

            if (eb_call) {
                PyObject* eb_args;
                for (int i = 0; /* until break */; ++i) {
                    PyObject* item = getter->get();
                    if (item) {
                        if (value_is_vector && PySequence_Check(item)) {
                            eb_args = PyTuple_New(1);
                            PyTuple_SET_ITEM(eb_args, 0, item);
                        } else if (PyTuple_CheckExact(item)) {
                            eb_args = item;
                        } else if (PyList_CheckExact(item)) {
                            Py_ssize_t isz = PyList_GET_SIZE(item);
                            eb_args = PyTuple_New(isz);
                            for (Py_ssize_t j = 0; j < isz; ++j) {
                                PyObject* iarg = PyList_GET_ITEM(item, j);
                                Py_INCREF(iarg);
                                PyTuple_SET_ITEM(eb_args, j, iarg);
                            }
                            Py_DECREF(item);
                        } else {
                            Py_DECREF(item);
                            PyErr_Format(PyExc_TypeError, "argument %d is not a tuple or list", i);
                            fill_ok = false;
                            break;
                        }
                        PyObject* ebres = PyObject_CallObject(eb_call, eb_args);
                        Py_DECREF(eb_args);
                        if (!ebres) {
                            fill_ok = false;
                            break;
                        }
                        Py_DECREF(ebres);
                    } else {
                        if (PyErr_Occurred()) {
                            if (!(PyErr_ExceptionMatches(PyExc_IndexError) ||
                                  PyErr_ExceptionMatches(PyExc_StopIteration)))
                                fill_ok = false;
                            else { PyErr_Clear(); }
                        }
                        break;
                    }
                }
                Py_DECREF(eb_call);
            }
        } else {
        // use push_back to add the vector entries one by one
            PyObject* pb_call = PyObject_GetAttrString(self, (char*)"push_back");
            if (pb_call) {
                for (;;) {
                    PyObject* item = getter->get();
                    if (item) {
                        PyObject* pbres = PyObject_CallFunctionObjArgs(pb_call, item, nullptr);
                        Py_DECREF(item);
                        if (!pbres) {
                            fill_ok = false;
                            break;
                        }
                        Py_DECREF(pbres);
                    } else {
                        if (PyErr_Occurred()) {
                            if (!(PyErr_ExceptionMatches(PyExc_IndexError) ||
                                  PyErr_ExceptionMatches(PyExc_StopIteration)))
                                fill_ok = false;
                            else { PyErr_Clear(); }
                        }
                        break;
                    }
                }
                Py_DECREF(pb_call);
            }
        }
        Py_XDECREF(fi);
        delete getter;

        if (!fill_ok) {
            Py_DECREF(result);
            return nullptr;
        }

        return result;
    }

// The given argument wasn't iterable: simply forward to regular constructor
    PyObject* realInit = PyObject_GetAttr(self, PyStrings::gRealInit);
    if (realInit) {
        PyObject* result = PyObject_Call(realInit, args, nullptr);
        Py_DECREF(realInit);
        return result;
    }

    return nullptr;
}

//---------------------------------------------------------------------------
PyObject* VectorData(PyObject* self, PyObject*)
{
    PyObject* pydata = CallPyObjMethod(self, "__real_data");
    if (!LowLevelView_Check(pydata)) return pydata;

    PyObject* pylen = PyObject_CallMethodObjArgs(self, PyStrings::gSize, nullptr);
    if (!pylen) {
        PyErr_Clear();
        return pydata;
    }

    long clen = PyInt_AsLong(pylen);
    Py_DECREF(pylen);

// TODO: should be a LowLevelView helper
    Py_buffer& bi = ((LowLevelView*)pydata)->fBufInfo;
    bi.len = clen * bi.itemsize;
    if (bi.ndim == 1 && bi.shape)
        bi.shape[0] = clen;

    return pydata;
}


//-----------------------------------------------------------------------------
static PyObject* vector_iter(PyObject* v) {
    vectoriterobject* vi = PyObject_GC_New(vectoriterobject, &VectorIter_Type);
    if (!vi) return nullptr;

    Py_INCREF(v);
    vi->ii_container = v;

// tell the iterator code to set a life line if this container is a temporary
    vi->vi_flags = vectoriterobject::kDefault;
    if (v->ob_refcnt <= 2 || (((CPPInstance*)v)->fFlags & CPPInstance::kIsValue))
        vi->vi_flags = vectoriterobject::kNeedLifeLine;

    PyObject* pyvalue_type = PyObject_GetAttr((PyObject*)Py_TYPE(v), PyStrings::gValueType);
    if (pyvalue_type) {
        PyObject* pyvalue_size = GetAttrDirect((PyObject*)Py_TYPE(v), PyStrings::gValueSize);
        if (pyvalue_size) {
            vi->vi_stride = PyLong_AsLong(pyvalue_size);
            Py_DECREF(pyvalue_size);
        } else {
            PyErr_Clear();
            vi->vi_stride = 0;
        }

        if (CPyCppyy_PyText_Check(pyvalue_type)) {
            std::string value_type = CPyCppyy_PyText_AsString(pyvalue_type);
            vi->vi_klass = Cppyy::GetScope(value_type);
            if (vi->vi_klass) {
                vi->vi_converter = nullptr;
                if (!vi->vi_flags) {
                    value_type = Cppyy::ResolveName(value_type);
                    if (value_type.back() != '*')     // meaning, object stored by-value
                        vi->vi_flags = vectoriterobject::kNeedLifeLine;
                }
            } else
                vi->vi_converter = CPyCppyy::CreateConverter(value_type);
            if (!vi->vi_stride) vi->vi_stride = Cppyy::SizeOf(value_type);

        } else if (CPPScope_Check(pyvalue_type)) {
            vi->vi_klass = ((CPPClass*)pyvalue_type)->fCppType;
            vi->vi_converter = nullptr;
            if (!vi->vi_stride) vi->vi_stride = Cppyy::SizeOf(vi->vi_klass);
            if (!vi->vi_flags)  vi->vi_flags  = vectoriterobject::kNeedLifeLine;
        }

        PyObject* pydata = CallPyObjMethod(v, "__real_data");
        if (!pydata || Utility::GetBuffer(pydata, '*', 1, vi->vi_data, false) == 0)
            vi->vi_data = CPPInstance_Check(pydata) ? ((CPPInstance*)pydata)->GetObjectRaw() : nullptr;
        Py_XDECREF(pydata);

    } else {
        PyErr_Clear();
        vi->vi_data      = nullptr;
        vi->vi_stride    = 0;
        vi->vi_converter = nullptr;
        vi->vi_klass     = 0;
        vi->vi_flags     = 0;
    }

    Py_XDECREF(pyvalue_type);

    vi->ii_pos = 0;
    vi->ii_len = PySequence_Size(v);

    PyObject_GC_Track(vi);
    return (PyObject*)vi;
}

PyObject* VectorGetItem(CPPInstance* self, PySliceObject* index)
{
// Implement python's __getitem__ for std::vector<>s.
    if (PySlice_Check(index)) {
        if (!self->GetObject()) {
            PyErr_SetString(PyExc_TypeError, "unsubscriptable object");
            return nullptr;
        }

        PyObject* pyclass = (PyObject*)Py_TYPE((PyObject*)self);
        PyObject* nseq = PyObject_CallObject(pyclass, nullptr);

        Py_ssize_t start, stop, step;
        PySlice_GetIndices((CPyCppyy_PySliceCast)index, PyObject_Length((PyObject*)self), &start, &stop, &step);

        const Py_ssize_t nlen = PySequence_Size((PyObject*)self);
        if (!AdjustSlice(nlen, start, stop, step))
            return nseq;

        const Py_ssize_t sign = step < 0 ? -1 : 1;
        for (Py_ssize_t i = start; i*sign < stop*sign; i += step) {
            PyObject* pyidx = PyInt_FromSsize_t(i);
            PyObject* item = PyObject_CallMethodObjArgs((PyObject*)self, PyStrings::gGetNoCheck, pyidx, nullptr);
            CallPyObjMethod(nseq, "push_back", item);
            Py_DECREF(item);
            Py_DECREF(pyidx);
        }

        return nseq;
    }

    return CallSelfIndex(self, (PyObject*)index, PyStrings::gGetNoCheck);
}


static Cppyy::TCppType_t sVectorBoolTypeID = (Cppyy::TCppType_t)0;

PyObject* VectorBoolGetItem(CPPInstance* self, PyObject* idx)
{
// std::vector<bool> is a special-case in C++, and its return type depends on
// the compiler: treat it special here as well
    if (!CPPInstance_Check(self) || self->ObjectIsA() != sVectorBoolTypeID) {
        PyErr_Format(PyExc_TypeError,
            "require object of type std::vector<bool>, but %s given",
            Cppyy::GetScopedFinalName(self->ObjectIsA()).c_str());
        return nullptr;
    }

    if (!self->GetObject()) {
        PyErr_SetString(PyExc_TypeError, "unsubscriptable object");
        return nullptr;
    }

    if (PySlice_Check(idx)) {
        PyObject* pyclass = (PyObject*)Py_TYPE((PyObject*)self);
        PyObject* nseq = PyObject_CallObject(pyclass, nullptr);

        Py_ssize_t start, stop, step;
        PySlice_GetIndices((CPyCppyy_PySliceCast)idx, PyObject_Length((PyObject*)self), &start, &stop, &step);
        const Py_ssize_t nlen = PySequence_Size((PyObject*)self);
        if (!AdjustSlice(nlen, start, stop, step))
            return nseq;

        const Py_ssize_t sign = step < 0 ? -1 : 1;
        for (Py_ssize_t i = start; i*sign < stop*sign; i += step) {
            PyObject* pyidx = PyInt_FromSsize_t(i);
            PyObject* item = PyObject_CallMethodObjArgs((PyObject*)self, PyStrings::gGetItem, pyidx, nullptr);
            CallPyObjMethod(nseq, "push_back", item);
            Py_DECREF(item);
            Py_DECREF(pyidx);
        }

        return nseq;
    }

    PyObject* pyindex = PyStyleIndex((PyObject*)self, idx);
    if (!pyindex)
        return nullptr;

    int index = (int)PyLong_AsLong(pyindex);
    Py_DECREF(pyindex);

// get hold of the actual std::vector<bool> (no cast, as vector is never a base)
    std::vector<bool>* vb = (std::vector<bool>*)self->GetObject();

// finally, return the value
    if (bool((*vb)[index]))
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

PyObject* VectorBoolSetItem(CPPInstance* self, PyObject* args)
{
// std::vector<bool> is a special-case in C++, and its return type depends on
// the compiler: treat it special here as well
    if (!CPPInstance_Check(self) || self->ObjectIsA() != sVectorBoolTypeID) {
        PyErr_Format(PyExc_TypeError,
            "require object of type std::vector<bool>, but %s given",
            Cppyy::GetScopedFinalName(self->ObjectIsA()).c_str());
        return nullptr;
    }

    if (!self->GetObject()) {
        PyErr_SetString(PyExc_TypeError, "unsubscriptable object");
        return nullptr;
    }

    int bval = 0; PyObject* idx = nullptr;
    if (!PyArg_ParseTuple(args, const_cast<char*>("Oi:__setitem__"), &idx, &bval))
        return nullptr;

    PyObject* pyindex = PyStyleIndex((PyObject*)self, idx);
    if (!pyindex)
        return nullptr;

    int index = (int)PyLong_AsLong(pyindex);
    Py_DECREF(pyindex);

// get hold of the actual std::vector<bool> (no cast, as vector is never a base)
    std::vector<bool>* vb = (std::vector<bool>*)self->GetObject();

// finally, set the value
    (*vb)[index] = (bool)bval;

    Py_RETURN_NONE;
}


//- array behavior as primitives ----------------------------------------------
PyObject* ArrayInit(PyObject* self, PyObject* args, PyObject* /* kwds */)
{
// std::array is normally only constructed using aggregate initialization, which
// is a concept that does not exist in python, so use this custom constructor to
// to fill the array using setitem

    if (args && PyTuple_GET_SIZE(args) == 1 && PySequence_Check(PyTuple_GET_ITEM(args, 0))) {
    // construct the empty array, then fill it
        PyObject* result = PyObject_CallMethodObjArgs(self, PyStrings::gRealInit, nullptr);
        if (!result)
            return nullptr;

        PyObject* items = PyTuple_GET_ITEM(args, 0);
        Py_ssize_t fillsz = PySequence_Size(items);
        if (PySequence_Size(self) != fillsz) {
            PyErr_Format(PyExc_ValueError, "received sequence of size %zd where %zd expected",
                         fillsz, PySequence_Size(self));
            Py_DECREF(result);
            return nullptr;
        }
 
        PyObject* si_call = PyObject_GetAttr(self, PyStrings::gSetItem);
        for (Py_ssize_t i = 0; i < fillsz; ++i) {
            PyObject* item = PySequence_GetItem(items, i);
            PyObject* index = PyInt_FromSsize_t(i);
            PyObject* sires = PyObject_CallFunctionObjArgs(si_call, index, item, nullptr);
            Py_DECREF(index);
            Py_DECREF(item);
            if (!sires) {
                Py_DECREF(si_call);
                Py_DECREF(result);
                return nullptr;
            } else
                Py_DECREF(sires);
        }
        Py_DECREF(si_call);

        return result;
    } else
        PyErr_Clear();

// The given argument wasn't iterable: simply forward to regular constructor
    PyObject* realInit = PyObject_GetAttr(self, PyStrings::gRealInit);
    if (realInit) {
        PyObject* result = PyObject_Call(realInit, args, nullptr);
        Py_DECREF(realInit);
        return result;
    }

    return nullptr;
}


//- map behavior as primitives ------------------------------------------------
static PyObject* MapFromPairs(PyObject* self, PyObject* pairs)
{
// construct an empty map, then fill it with the key, value pairs
    PyObject* result = PyObject_CallMethodObjArgs(self, PyStrings::gRealInit, nullptr);
    if (!result)
        return nullptr;

    PyObject* si_call = PyObject_GetAttr(self, PyStrings::gSetItem);
    for (Py_ssize_t i = 0; i < PySequence_Size(pairs); ++i) {
        PyObject* pair = PySequence_GetItem(pairs, i);
        PyObject* sires = nullptr;
        if (pair && PySequence_Check(pair) && PySequence_Size(pair) == 2) {
            PyObject* key   = PySequence_GetItem(pair, 0);
            PyObject* value = PySequence_GetItem(pair, 1);
            sires = PyObject_CallFunctionObjArgs(si_call, key, value, nullptr);
            Py_DECREF(value);
            Py_DECREF(key);
        }
        Py_DECREF(pair);
        if (!sires) {
            Py_DECREF(si_call);
            Py_DECREF(result);
            if (!PyErr_Occurred())
                PyErr_SetString(PyExc_TypeError, "Failed to fill map (argument not a dict or sequence of pairs)");
            return nullptr;
        } else
            Py_DECREF(sires);
    }
    Py_DECREF(si_call);

    return result;
}

PyObject* MapInit(PyObject* self, PyObject* args, PyObject* /* kwds */)
{
// Specialized map constructor to allow construction from mapping containers and
// from tuples of pairs ("intializer_list style").

// PyMapping_Check is not very discriminatory, as it basically only checks for the
// existence of  __getitem__, hence the most common cases of tuple and list are
// dropped straight-of-the-bat (the PyMapping_Items call will fail on them).
    if (PyTuple_GET_SIZE(args) == 1 && PyMapping_Check(PyTuple_GET_ITEM(args, 0)) && \
           !(PyTuple_Check(PyTuple_GET_ITEM(args, 0)) || PyList_Check(PyTuple_GET_ITEM(args, 0)))) {
        PyObject* assoc = PyTuple_GET_ITEM(args, 0);
#if PY_VERSION_HEX < 0x03000000
    // to prevent warning about literal string, expand macro
        PyObject* items = PyObject_CallMethod(assoc, (char*)"items", nullptr);
#else
    // in p3, PyMapping_Items isn't a macro, but a function that short-circuits dict
        PyObject* items = PyMapping_Items(assoc);
#endif
        if (items && PySequence_Check(items)) {
            PyObject* result = MapFromPairs(self, items);
            Py_DECREF(items);
            return result;
        }

        Py_XDECREF(items);
        PyErr_Clear();

    // okay to fall through as long as 'self' has not been created (is done in MapFromPairs)
    }

// tuple of pairs case (some mapping types are sequences)
    if (PyTuple_GET_SIZE(args) == 1 && PySequence_Check(PyTuple_GET_ITEM(args, 0)))
        return MapFromPairs(self, PyTuple_GET_ITEM(args, 0));

// The given argument wasn't a mapping or tuple of pairs: forward to regular constructor
    PyObject* realInit = PyObject_GetAttr(self, PyStrings::gRealInit);
    if (realInit) {
        PyObject* result = PyObject_Call(realInit, args, nullptr);
        Py_DECREF(realInit);
        return result;
    }

    return nullptr;
}

PyObject* STLContainsWithFind(PyObject* self, PyObject* obj)
{
// Implement python's __contains__ for std::map/std::set
    PyObject* result = nullptr;

    PyObject* iter = CallPyObjMethod(self, "find", obj);
    if (CPPInstance_Check(iter)) {
        PyObject* end = PyObject_CallMethodObjArgs(self, PyStrings::gEnd, nullptr);
        if (CPPInstance_Check(end)) {
            if (!PyObject_RichCompareBool(iter, end, Py_EQ)) {
                Py_INCREF(Py_True);
                result = Py_True;
            }
        }
        Py_XDECREF(end);
    }
    Py_XDECREF(iter);

    if (!result) {
        PyErr_Clear();            // e.g. wrong argument type, which should always lead to False
        Py_INCREF(Py_False);
        result = Py_False;
    }

    return result;
}


//- set behavior as primitives ------------------------------------------------
PyObject* SetInit(PyObject* self, PyObject* args, PyObject* /* kwds */)
{
// Specialized set constructor to allow construction from Python sets.
    if (PyTuple_GET_SIZE(args) == 1 && PySet_Check(PyTuple_GET_ITEM(args, 0))) {
        PyObject* pyset = PyTuple_GET_ITEM(args, 0);

    // construct an empty set, then fill it
        PyObject* result = PyObject_CallMethodObjArgs(self, PyStrings::gRealInit, nullptr);
        if (!result)
            return nullptr;

        PyObject* iter = PyObject_GetIter(pyset);
        if (iter) {
            PyObject* ins_call = PyObject_GetAttrString(self, (char*)"insert");

            IterItemGetter getter{iter};
            Py_DECREF(iter);

            PyObject* item = getter.get();
            while (item) {
                PyObject* insres = PyObject_CallFunctionObjArgs(ins_call, item, nullptr);
                Py_DECREF(item);
                if (!insres) {
                    Py_DECREF(ins_call);
                    Py_DECREF(result);
                    return nullptr;
                } else
                    Py_DECREF(insres);
                item = getter.get();
            }
            Py_DECREF(ins_call);
        }

        return result;
    }

// The given argument wasn't iterable: simply forward to regular constructor
    PyObject* realInit = PyObject_GetAttr(self, PyStrings::gRealInit);
    if (realInit) {
        PyObject* result = PyObject_Call(realInit, args, nullptr);
        Py_DECREF(realInit);
        return result;
    }

    return nullptr;
}


//- STL container iterator support --------------------------------------------
static const ptrdiff_t PS_END_ADDR  =  7;   // non-aligned address, so no clash
static const ptrdiff_t PS_FLAG_ADDR = 11;   // id.
static const ptrdiff_t PS_COLL_ADDR = 13;   // id.

PyObject* StlSequenceIter(PyObject* self)
{
// Implement python's __iter__ for std::iterator<>s
    PyObject* iter = PyObject_CallMethodObjArgs(self, PyStrings::gBegin, nullptr);
    if (iter) {
        PyObject* end = PyObject_CallMethodObjArgs(self, PyStrings::gEnd, nullptr);
        if (end) {
            if (CPPInstance_Check(iter)) {
            // use the data member cache to store extra state on the iterator object,
            // without it being visible on the Python side
                auto& dmc = ((CPPInstance*)iter)->GetDatamemberCache();
                dmc.push_back(std::make_pair(PS_END_ADDR, end));

            // set a flag, indicating first iteration (reset in __next__)
                Py_INCREF(Py_False);
                dmc.push_back(std::make_pair(PS_FLAG_ADDR, Py_False));

            // make sure the iterated over collection remains alive for the duration
                Py_INCREF(self);
                dmc.push_back(std::make_pair(PS_COLL_ADDR, self));
            } else {
            // could store "end" on the object's dictionary anyway, but if end() returns
            // a user-customized object, then its __next__ is probably custom, too
                Py_DECREF(end);
            }
        }
    }
    return iter;
}

//- generic iterator support over a sequence with operator[] and size ---------
//-----------------------------------------------------------------------------
static PyObject* index_iter(PyObject* c) {
    indexiterobject* ii = PyObject_GC_New(indexiterobject, &IndexIter_Type);
    if (!ii) return nullptr;

    Py_INCREF(c);
    ii->ii_container = c;
    ii->ii_pos = 0;
    ii->ii_len = PySequence_Size(c);

    PyObject_GC_Track(ii);
    return (PyObject*)ii;
}


//- safe indexing for STL-like vector w/o iterator dictionaries ---------------
/* replaced by indexiterobject iteration, but may still have some future use ...
PyObject* CheckedGetItem(PyObject* self, PyObject* obj)
{
// Implement a generic python __getitem__ for STL-like classes that are missing the
// reflection info for their iterators. This is then used for iteration by means of
// consecutive indeces, it such index is of integer type.
    Py_ssize_t size = PySequence_Size(self);
    Py_ssize_t idx  = PyInt_AsSsize_t(obj);
    if ((size == (Py_ssize_t)-1 || idx == (Py_ssize_t)-1) && PyErr_Occurred()) {
    // argument conversion problem: let method itself resolve anew and report
        PyErr_Clear();
        return PyObject_CallMethodObjArgs(self, PyStrings::gGetNoCheck, obj, nullptr);
    }

    bool inbounds = false;
    if (idx < 0) idx += size;
    if (0 <= idx && 0 <= size && idx < size)
        inbounds = true;

    if (inbounds)
        return PyObject_CallMethodObjArgs(self, PyStrings::gGetNoCheck, obj, nullptr);
    else
        PyErr_SetString( PyExc_IndexError, "index out of range" );

    return nullptr;
}*/


//- pair as sequence to allow tuple unpacking --------------------------------
PyObject* PairUnpack(PyObject* self, PyObject* pyindex)
{
// For std::map<> iteration, unpack std::pair<>s into tuples for the loop.
    long idx = PyLong_AsLong(pyindex);
    if (idx == -1 && PyErr_Occurred())
        return nullptr;

    if (!CPPInstance_Check(self) || !((CPPInstance*)self)->GetObject()) {
        PyErr_SetString(PyExc_TypeError, "unsubscriptable object");
        return nullptr;
    }

    if ((int)idx == 0)
        return PyObject_GetAttr(self, PyStrings::gFirst);
    else if ((int)idx == 1)
        return PyObject_GetAttr(self, PyStrings::gSecond);

// still here? Trigger stop iteration
    PyErr_SetString(PyExc_IndexError, "out of bounds");
    return nullptr;
}

//- simplistic len() functions -----------------------------------------------
PyObject* ReturnTwo(CPPInstance*, PyObject*) {
    return PyInt_FromLong(2);
}


//- shared/unique_ptr behavior -----------------------------------------------
PyObject* SmartPtrInit(PyObject* self, PyObject* args, PyObject* /* kwds */)
{
// since the shared/unique pointer will take ownership, we need to relinquish it
    PyObject* realInit = PyObject_GetAttr(self, PyStrings::gRealInit);
    if (realInit) {
        PyObject* result = PyObject_Call(realInit, args, nullptr);
        Py_DECREF(realInit);
        if (result && PyTuple_GET_SIZE(args) == 1 && CPPInstance_Check(PyTuple_GET_ITEM(args, 0))) {
            CPPInstance* cppinst = (CPPInstance*)PyTuple_GET_ITEM(args, 0);
            if (!(cppinst->fFlags & CPPInstance::kIsSmartPtr)) cppinst->CppOwns();
        }
        return result;
    }
    return nullptr;
}


//- string behavior as primitives --------------------------------------------
#if PY_VERSION_HEX >= 0x03000000
// TODO: this is wrong, b/c it doesn't order
static int PyObject_Compare(PyObject* one, PyObject* other) {
    return !PyObject_RichCompareBool(one, other, Py_EQ);
}
#endif
static inline
PyObject* CPyCppyy_PyString_FromCppString(std::string* s, bool native=true) {
    if (native)
        return PyBytes_FromStringAndSize(s->data(), s->size());
    return CPyCppyy_PyText_FromStringAndSize(s->data(), s->size());
}

static inline
PyObject* CPyCppyy_PyString_FromCppString(std::wstring* s, bool native=true) {
    PyObject* pyobj = PyUnicode_FromWideChar(s->data(), s->size());
    if (pyobj && native) {
        PyObject* pybytes = PyUnicode_AsEncodedString(pyobj, "UTF-8", "strict");
        Py_DECREF(pyobj);
        pyobj = pybytes;
    }
    return pyobj;
}

#define CPPYY_IMPL_STRING_PYTHONIZATION(type, name)                          \
static inline                                                                \
PyObject* name##StringGetData(PyObject* self, bool native=true)              \
{                                                                            \
    if (CPyCppyy::CPPInstance_Check(self)) {                                 \
        type* obj = ((type*)((CPPInstance*)self)->GetObject());              \
        if (obj) return CPyCppyy_PyString_FromCppString(obj, native);        \
    }                                                                        \
    PyErr_Format(PyExc_TypeError, "object mismatch (%s expected)", #type);   \
    return nullptr;                                                          \
}                                                                            \
                                                                             \
PyObject* name##StringStr(PyObject* self)                                    \
{                                                                            \
    PyObject* pyobj = name##StringGetData(self, false);                      \
    if (!pyobj) {                                                            \
      /* do a native conversion to make printing possible (debatable) */     \
        PyErr_Clear();                                                       \
        PyObject* pybytes = name##StringGetData(self, true);                 \
        if (pybytes) { /* should not fail */                                 \
            pyobj = PyObject_Str(pybytes);                                   \
            Py_DECREF(pybytes);                                              \
        }                                                                    \
    }                                                                        \
    return pyobj;                                                            \
}                                                                            \
                                                                             \
PyObject* name##StringBytes(PyObject* self)                                  \
{                                                                            \
    return name##StringGetData(self, true);                                  \
}                                                                            \
                                                                             \
PyObject* name##StringRepr(PyObject* self)                                   \
{                                                                            \
    PyObject* data = name##StringGetData(self, true);                        \
    if (data) {                                                              \
        PyObject* repr = PyObject_Repr(data);                                \
        Py_DECREF(data);                                                     \
        return repr;                                                         \
    }                                                                        \
    return nullptr;                                                          \
}                                                                            \
                                                                             \
PyObject* name##StringIsEqual(PyObject* self, PyObject* obj)                 \
{                                                                            \
    PyObject* data = name##StringGetData(self, PyBytes_Check(obj));          \
    if (data) {                                                              \
        PyObject* result = PyObject_RichCompare(data, obj, Py_EQ);           \
        Py_DECREF(data);                                                     \
        return result;                                                       \
    }                                                                        \
    return nullptr;                                                          \
}                                                                            \
                                                                             \
PyObject* name##StringIsNotEqual(PyObject* self, PyObject* obj)              \
{                                                                            \
    PyObject* data = name##StringGetData(self, PyBytes_Check(obj));          \
    if (data) {                                                              \
        PyObject* result = PyObject_RichCompare(data, obj, Py_NE);           \
        Py_DECREF(data);                                                     \
        return result;                                                       \
    }                                                                        \
    return nullptr;                                                          \
}

// Only define StlStringCompare:
#define CPPYY_IMPL_STRING_PYTHONIZATION_CMP(type, name)                      \
CPPYY_IMPL_STRING_PYTHONIZATION(type, name)                                  \
PyObject* name##StringCompare(PyObject* self, PyObject* obj)                 \
{                                                                            \
    PyObject* data = name##StringGetData(self, PyBytes_Check(obj));          \
    int result = 0;                                                          \
    if (data) {                                                              \
        result = PyObject_Compare(data, obj);                                \
        Py_DECREF(data);                                                     \
    }                                                                        \
    if (PyErr_Occurred())                                                    \
        return nullptr;                                                      \
    return PyInt_FromLong(result);                                           \
}

CPPYY_IMPL_STRING_PYTHONIZATION_CMP(std::string, Stl)
CPPYY_IMPL_STRING_PYTHONIZATION_CMP(std::wstring, StlW)

PyObject* StlStringDecode(PyObject* self, PyObject* args, PyObject* kwds)
{
    char* keywords[] = {(char*)"encoding", (char*)"errors", (char*)nullptr};
    const char* encoding; const char* errors;
    if (!PyArg_ParseTupleAndKeywords(args, kwds,
            const_cast<char*>("s|s"), keywords, &encoding, &errors))
        return nullptr;
    std::string* obj = ((std::string*)((CPPInstance*)self)->GetObject());
    if (!obj) {
        PyErr_SetString(PyExc_ReferenceError, "attempt to access a null-pointer");
        return nullptr;
    }
    return PyUnicode_Decode(obj->data(), obj->size(), encoding, errors);
}

PyObject* StlStringReplace(CPPInstance* self, PyObject* args, PyObject* kwds)
{
    std::string* obj = (std::string*)self->GetObject();
    if (!obj) {
        PyErr_SetString(PyExc_ReferenceError, "attempt to access a null-pointer");
        return nullptr;
    }

    char* keywords[] = {(char*)"old", (char*)"new", (char*)"count", (char*)nullptr};
    char *oldbuf = nullptr, *newbuf = nullptr; Py_ssize_t nold = 0, nnew = 0, count = -1;
    if (PyArg_ParseTupleAndKeywords(args, kwds, const_cast<char*>("et#et#|n"), keywords,
            "utf-8", &oldbuf, &nold, "utf-8", &newbuf, &nnew, &count)) {

        std::string olds{oldbuf, (std::string::size_type)nold};
        std::string news{newbuf, (std::string::size_type)nnew};
        PyMem_Free(oldbuf); PyMem_Free(newbuf);

        std::string* ret = new std::string(*obj);
        if (count && olds != news) {    // does replacement have effect?
            int step = nnew + (nold ? 0 : 1);

            Py_ssize_t converted = 0;
            std::string::size_type pos = 0;
            while ((pos = ret->find(olds, pos)) != std::string::npos) {
                ret->replace(pos, nold, news);
                if (++converted == count)
                    break;
                pos += step;
            }
        }

        return BindCppObjectNoCast(ret, self->ObjectIsA(), CPPInstance::kIsOwner);
    }

    PyErr_Clear();
    PyObject* cppreplace = PyObject_GetAttrString((PyObject*)self, (char*)"__cpp_replace");
    if (cppreplace) {
        PyObject* result = PyObject_Call(cppreplace, args, nullptr);
        Py_DECREF(cppreplace);
        return result;
    }

    return nullptr;
}

Py_hash_t StlStringHash(PyObject* self)
{
// std::string objects hash to the same values as Python strings to allow
// matches in dictionaries etc.
    PyObject* data = StlStringGetData(self, false);
    Py_hash_t h = CPyCppyy_PyText_Type.tp_hash(data);
    Py_DECREF(data);
    return h;
}


//- STL iterator behavior ----------------------------------------------------
PyObject* StlIterNext(PyObject* self)
{
// Python iterator protocol __next__ for STL forward iterators.
    bool mustIncrement = true;
    PyObject* last = nullptr;
    if (CPPInstance_Check(self)) {
        auto& dmc = ((CPPInstance*)self)->GetDatamemberCache();
        for (auto& p: dmc) {
            if (p.first == PS_END_ADDR) {
                last = p.second;
                Py_INCREF(last);
            } else if (p.first == PS_FLAG_ADDR) {
                mustIncrement = p.second == Py_True;
                if (!mustIncrement) {
                    Py_DECREF(p.second);
                    Py_INCREF(Py_True);
                    p.second = Py_True;
                }
            }
        }
    }

    PyObject* next = nullptr;
    if (last) {
    // handle special case of empty container (i.e. self is end)
        if (!PyObject_RichCompareBool(last, self, Py_EQ)) {
            bool iter_valid = true;
            if (mustIncrement) {
            // prefer preinc, but allow post-inc; in both cases, it is "self" that has
            // the updated state to dereference
                PyObject* iter = PyObject_CallMethodObjArgs(self, PyStrings::gPreInc, nullptr);
                if (!iter) {
                    PyErr_Clear();
                    static PyObject* dummy = PyInt_FromLong(1l);
                    iter = PyObject_CallMethodObjArgs(self, PyStrings::gPostInc, dummy, nullptr);
                }
                iter_valid = iter && PyObject_RichCompareBool(last, self, Py_NE);
                Py_XDECREF(iter);
            }

            if (iter_valid) {
                next = PyObject_CallMethodObjArgs(self, PyStrings::gDeref, nullptr);
                if (!next) PyErr_Clear();
            }
        }
        Py_DECREF(last);
    }

    if (!next) PyErr_SetString(PyExc_StopIteration, "");
    return next;
}


//- STL complex<T> behavior --------------------------------------------------
#define COMPLEX_METH_GETSET(name, cppname)                                   \
static PyObject* name##ComplexGet(PyObject* self, void*) {                   \
    return PyObject_CallMethodObjArgs(self, cppname, nullptr);               \
}                                                                            \
static int name##ComplexSet(PyObject* self, PyObject* value, void*) {        \
    PyObject* result = PyObject_CallMethodObjArgs(self, cppname, value, nullptr);\
    if (result) {                                                            \
        Py_DECREF(result);                                                   \
        return 0;                                                            \
    }                                                                        \
    return -1;                                                               \
}                                                                            \
PyGetSetDef name##Complex{(char*)#name, (getter)name##ComplexGet, (setter)name##ComplexSet, nullptr, nullptr};

COMPLEX_METH_GETSET(real, PyStrings::gCppReal)
COMPLEX_METH_GETSET(imag, PyStrings::gCppImag)

static PyObject* ComplexComplex(PyObject* self) {
    PyObject* real = PyObject_CallMethodObjArgs(self, PyStrings::gCppReal, nullptr);
    if (!real) return nullptr;
    double r = PyFloat_AsDouble(real);
    Py_DECREF(real);
    if (r == -1. && PyErr_Occurred())
        return nullptr;

    PyObject* imag = PyObject_CallMethodObjArgs(self, PyStrings::gCppImag, nullptr);
    if (!imag) return nullptr;
    double i = PyFloat_AsDouble(imag);
    Py_DECREF(imag);
    if (i == -1. && PyErr_Occurred())
        return nullptr;

    return PyComplex_FromDoubles(r, i);
}

static PyObject* ComplexRepr(PyObject* self) {
    PyObject* real = PyObject_CallMethodObjArgs(self, PyStrings::gCppReal, nullptr);
    if (!real) return nullptr;
    double r = PyFloat_AsDouble(real);
    Py_DECREF(real);
    if (r == -1. && PyErr_Occurred())
        return nullptr;

    PyObject* imag = PyObject_CallMethodObjArgs(self, PyStrings::gCppImag, nullptr);
    if (!imag) return nullptr;
    double i = PyFloat_AsDouble(imag);
    Py_DECREF(imag);
    if (i == -1. && PyErr_Occurred())
        return nullptr;

    std::ostringstream s;
    s << '(' << r << '+' << i << "j)";
    return CPyCppyy_PyText_FromString(s.str().c_str());
}

static PyObject* ComplexDRealGet(CPPInstance* self, void*)
{
    return PyFloat_FromDouble(((std::complex<double>*)self->GetObject())->real());
}

static int ComplexDRealSet(CPPInstance* self, PyObject* value, void*)
{
    double d = PyFloat_AsDouble(value);
    if (d == -1.0 && PyErr_Occurred())
        return -1;
    ((std::complex<double>*)self->GetObject())->real(d);
    return 0;
}

PyGetSetDef ComplexDReal{(char*)"real", (getter)ComplexDRealGet, (setter)ComplexDRealSet, nullptr, nullptr};


static PyObject* ComplexDImagGet(CPPInstance* self, void*)
{
    return PyFloat_FromDouble(((std::complex<double>*)self->GetObject())->imag());
}

static int ComplexDImagSet(CPPInstance* self, PyObject* value, void*)
{
    double d = PyFloat_AsDouble(value);
    if (d == -1.0 && PyErr_Occurred())
        return -1;
    ((std::complex<double>*)self->GetObject())->imag(d);
    return 0;
}

PyGetSetDef ComplexDImag{(char*)"imag", (getter)ComplexDImagGet, (setter)ComplexDImagSet, nullptr, nullptr};

static PyObject* ComplexDComplex(CPPInstance* self)
{
    double r = ((std::complex<double>*)self->GetObject())->real();
    double i = ((std::complex<double>*)self->GetObject())->imag();
    return PyComplex_FromDoubles(r, i);
}


} // unnamed namespace


//- public functions ---------------------------------------------------------
namespace CPyCppyy {
    std::set<std::string> gIteratorTypes;
}

static inline
bool run_pythonizors(PyObject* pyclass, PyObject* pyname, const std::vector<PyObject*>& v)
{
    PyObject* args = PyTuple_New(2);
    Py_INCREF(pyclass); PyTuple_SET_ITEM(args, 0, pyclass);
    Py_INCREF(pyname);  PyTuple_SET_ITEM(args, 1, pyname);

    bool pstatus = true;
    for (auto pythonizor : v) {
        PyObject* result = PyObject_CallObject(pythonizor, args);
        if (!result) {
            pstatus = false; // TODO: detail the error handling
            break;
        }
        Py_DECREF(result);
    }
    Py_DECREF(args);

    return pstatus;
}

bool CPyCppyy::Pythonize(PyObject* pyclass, const std::string& name)
{
// Add pre-defined pythonizations (for STL and ROOT) to classes based on their
// signature and/or class name.
    if (!pyclass)
        return false;

   CPPScope* klass = (CPPScope*)pyclass;

//- method name based pythonization ------------------------------------------

// for smart pointer style classes that are otherwise not known as such; would
// prefer operator-> as that returns a pointer (which is simpler since it never
// has to deal with ref-assignment), but operator* plays better with STL iters
// and algorithms
    if (HasAttrDirect(pyclass, PyStrings::gDeref) && !Cppyy::IsSmartPtr(klass->fCppType))
        Utility::AddToClass(pyclass, "__getattr__", (PyCFunction)DeRefGetAttr, METH_O);
    else if (HasAttrDirect(pyclass, PyStrings::gFollow) && !Cppyy::IsSmartPtr(klass->fCppType))
        Utility::AddToClass(pyclass, "__getattr__", (PyCFunction)FollowGetAttr, METH_O);

// for STL containers, and user classes modeled after them
    if (HasAttrDirect(pyclass, PyStrings::gSize))
        Utility::AddToClass(pyclass, "__len__", "size");

    if (!IsTemplatedSTLClass(name, "vector")  &&      // vector is dealt with below
           !((PyTypeObject*)pyclass)->tp_iter) {
        if (HasAttrDirect(pyclass, PyStrings::gBegin) && HasAttrDirect(pyclass, PyStrings::gEnd)) {
        // obtain the name of the return type
            const auto& v = Cppyy::GetMethodIndicesFromName(klass->fCppType, "begin");
            if (!v.empty()) {
            // check return type; if not explicitly an iterator, add it to the "known" return
            // types to add the "next" method on use
                Cppyy::TCppMethod_t meth = Cppyy::GetMethod(klass->fCppType, v[0]);
                const std::string& resname = Cppyy::GetMethodResultType(meth);
                if (Cppyy::GetScope(resname)) {
                    if (resname.find("iterator") == std::string::npos)
                        gIteratorTypes.insert(resname);

                // install iterator protocol a la STL
                    ((PyTypeObject*)pyclass)->tp_iter = (getiterfunc)StlSequenceIter;
                    Utility::AddToClass(pyclass, "__iter__", (PyCFunction)StlSequenceIter, METH_NOARGS);
                }
            }
        }
        if (!((PyTypeObject*)pyclass)->tp_iter &&     // no iterator resolved
                HasAttrDirect(pyclass, PyStrings::gGetItem) && HasAttrDirect(pyclass, PyStrings::gLen)) {
        // Python will iterate over __getitem__ using integers, but C++ operator[] will never raise
        // a StopIteration. A checked getitem (raising IndexError if beyond size()) works in some
        // cases but would mess up if operator[] is meant to implement an associative container. So,
        // this has to be implemented as an interator protocol.
            ((PyTypeObject*)pyclass)->tp_iter = (getiterfunc)index_iter;
            Utility::AddToClass(pyclass, "__iter__", (PyCFunction)index_iter, METH_NOARGS);
        }
    }

// operator==/!= are used in op_richcompare of CPPInstance, which subsequently allows
// comparisons to None; if no operator is available, a hook is installed for lazy
// lookups in the global and/or class namespace
    if (HasAttrDirect(pyclass, PyStrings::gEq, true)) {
        PyObject* cppol = PyObject_GetAttr(pyclass, PyStrings::gEq);
        if (!klass->fOperators) klass->fOperators = new Utility::PyOperators();
        klass->fOperators->fEq = cppol;
    // re-insert the forwarding __eq__ from the CPPInstance in case there was a Python-side
    // override in the base class
        static PyObject* top_eq = nullptr;
        if (!top_eq) {
            PyObject* top_cls = PyObject_GetAttrString(gThisModule, "CPPInstance");
            top_eq = PyObject_GetAttr(top_cls, PyStrings::gEq);
            Py_DECREF(top_eq);    // make it borrowed
            Py_DECREF(top_cls);
        }
        PyObject_SetAttr(pyclass, PyStrings::gEq, top_eq);
    }

    if (HasAttrDirect(pyclass, PyStrings::gNe, true)) {
        PyObject* cppol = PyObject_GetAttr(pyclass, PyStrings::gNe);
        if (!klass->fOperators) klass->fOperators = new Utility::PyOperators();
        klass->fOperators->fNe = cppol;
    // re-insert the forwarding __ne__ (same reason as above for __eq__)
        static PyObject* top_ne = nullptr;
        if (!top_ne) {
            PyObject* top_cls = PyObject_GetAttrString(gThisModule, "CPPInstance");
            top_ne = PyObject_GetAttr(top_cls, PyStrings::gNe);
            Py_DECREF(top_ne);    // make it borrowed
            Py_DECREF(top_cls);
        }
        PyObject_SetAttr(pyclass, PyStrings::gNe, top_ne);
    }

    if (Cppyy::IsAggregate(((CPPClass*)pyclass)->fCppType) && name.compare(0, 5, "std::", 5) != 0) {
    // create a pseudo-constructor to allow initializer-style object creation
        Cppyy::TCppType_t kls = ((CPPClass*)pyclass)->fCppType;
        Cppyy::TCppIndex_t ndata = Cppyy::GetNumDatamembers(kls);
        if (ndata) {
            std::string rname = name;
            TypeManip::cppscope_to_legalname(rname);

            std::ostringstream initdef;
            initdef << "namespace __cppyy_internal {\n"
                    << "void init_" << rname << "(" << name << "*& self";
            bool codegen_ok = true;
            std::vector<std::string> arg_types, arg_names, arg_defaults;
            arg_types.reserve(ndata); arg_names.reserve(ndata); arg_defaults.reserve(ndata);
            for (Cppyy::TCppIndex_t i = 0; i < ndata; ++i) {
                if (Cppyy::IsStaticData(kls, i) || !Cppyy::IsPublicData(kls, i))
                    continue;

                const std::string& tt = Cppyy::ResolveName(Cppyy::GetDatamemberType(kls, i));
                const std::string& cpd = Utility::Compound(tt);
                const std::string& tt_clean = TypeManip::clean_type(tt, false, true);

                if (Cppyy::IsEnum(tt_clean)) {
                // TODO: this is stupid as it breaks both on legit enum data members as well as on
                // enum constants that are exposed to the enclosing scope, but for now it is safer
                // to simply not support ithis case
                    codegen_ok = false;
                    break;
                }

                if (tt.rfind(']') == std::string::npos && tt.rfind(')') == std::string::npos) {
                    if (!cpd.empty()) arg_types.push_back(tt_clean+cpd);
                    else arg_types.push_back("const "+tt_clean+"&");
                    arg_names.push_back(Cppyy::GetDatamemberName(kls, i));
                    if ((!cpd.empty() && cpd.back() == '*') || Cppyy::IsBuiltin(tt_clean))
                        arg_defaults.push_back("0");
                    else {
                        Cppyy::TCppScope_t ttid = Cppyy::GetScope(tt_clean);
                        if (Cppyy::IsDefaultConstructable(ttid)) arg_defaults.push_back(tt_clean+"{}");
                    }
                } else {
                    codegen_ok = false;     // TODO: how to support arrays, anonymous enums, etc?
                    break;
                }
            }

            if (codegen_ok && !arg_types.empty()) {
                bool defaults_ok = arg_defaults.size() == arg_types.size();
                for (std::vector<std::string>::size_type i = 0; i < arg_types.size(); ++i) {
                    initdef << ", " << arg_types[i] << " " << arg_names[i];
                    if (defaults_ok) initdef << " = " << arg_defaults[i];
                }
                initdef << ") {\n  self = new " << name << "{";
                for (std::vector<std::string>::size_type i = 0; i < arg_names.size(); ++i) {
                    if (i != 0) initdef << ", ";
                    initdef << arg_names[i];
                }
                initdef << "};\n} }";

                if (Cppyy::Compile(initdef.str())) {
                    Cppyy::TCppScope_t cis = Cppyy::GetScope("__cppyy_internal");
                    const auto& mix = Cppyy::GetMethodIndicesFromName(cis, "init_"+rname);
                    if (mix.size()) {
                        if (!Utility::AddToClass(pyclass, "__init__",
                                new CPPFunction(cis, Cppyy::GetMethod(cis, mix[0]))))
                            PyErr_Clear();
                    }
                }
            }
        }
    }


//- class name based pythonization -------------------------------------------

    if (IsTemplatedSTLClass(name, "vector")) {

    // std::vector<bool> is a special case in C++
        if (!sVectorBoolTypeID) sVectorBoolTypeID = (Cppyy::TCppType_t)Cppyy::GetScope("std::vector<bool>");
        if (klass->fCppType == sVectorBoolTypeID) {
            Utility::AddToClass(pyclass, "__getitem__", (PyCFunction)VectorBoolGetItem, METH_O);
            Utility::AddToClass(pyclass, "__setitem__", (PyCFunction)VectorBoolSetItem);
        } else {
        // constructor that takes python collections
            Utility::AddToClass(pyclass, "__real_init", "__init__");
            Utility::AddToClass(pyclass, "__init__", (PyCFunction)VectorInit, METH_VARARGS | METH_KEYWORDS);

        // data with size
            Utility::AddToClass(pyclass, "__real_data", "data");
            Utility::AddToClass(pyclass, "data", (PyCFunction)VectorData);

        // checked getitem
            if (HasAttrDirect(pyclass, PyStrings::gLen)) {
                Utility::AddToClass(pyclass, "_getitem__unchecked", "__getitem__");
                Utility::AddToClass(pyclass, "__getitem__", (PyCFunction)VectorGetItem, METH_O);
            }

        // vector-optimized iterator protocol
            ((PyTypeObject*)pyclass)->tp_iter = (getiterfunc)vector_iter;

        // helpers for iteration
            const std::string& vtype = Cppyy::ResolveName(name+"::value_type");
            if (vtype.rfind("value_type") == std::string::npos) {    // actually resolved?
                PyObject* pyvalue_type = CPyCppyy_PyText_FromString(vtype.c_str());
                PyObject_SetAttr(pyclass, PyStrings::gValueType, pyvalue_type);
                Py_DECREF(pyvalue_type);
            }

            size_t typesz = Cppyy::SizeOf(name+"::value_type");
            if (typesz) {
                PyObject* pyvalue_size = PyLong_FromSsize_t(typesz);
                PyObject_SetAttr(pyclass, PyStrings::gValueSize, pyvalue_size);
                Py_DECREF(pyvalue_size);
            }
        }
    }

    else if (IsTemplatedSTLClass(name, "array")) {
    // constructor that takes python associative collections
        Utility::AddToClass(pyclass, "__real_init", "__init__");
        Utility::AddToClass(pyclass, "__init__", (PyCFunction)ArrayInit, METH_VARARGS | METH_KEYWORDS);
    }

    else if (IsTemplatedSTLClass(name, "map")) {
    // constructor that takes python associative collections
        Utility::AddToClass(pyclass, "__real_init", "__init__");
        Utility::AddToClass(pyclass, "__init__", (PyCFunction)MapInit, METH_VARARGS | METH_KEYWORDS);

        Utility::AddToClass(pyclass, "__contains__", (PyCFunction)STLContainsWithFind, METH_O);
    }

    else if (IsTemplatedSTLClass(name, "set")) {
    // constructor that takes python associative collections
        Utility::AddToClass(pyclass, "__real_init", "__init__");
        Utility::AddToClass(pyclass, "__init__", (PyCFunction)SetInit, METH_VARARGS | METH_KEYWORDS);

        Utility::AddToClass(pyclass, "__contains__", (PyCFunction)STLContainsWithFind, METH_O);
    }

    else if (IsTemplatedSTLClass(name, "pair")) {
        Utility::AddToClass(pyclass, "__getitem__", (PyCFunction)PairUnpack, METH_O);
        Utility::AddToClass(pyclass, "__len__", (PyCFunction)ReturnTwo, METH_NOARGS);
    }

    if (IsTemplatedSTLClass(name, "shared_ptr") || IsTemplatedSTLClass(name, "unique_ptr")) {
        Utility::AddToClass(pyclass, "__real_init", "__init__");
        Utility::AddToClass(pyclass, "__init__", (PyCFunction)SmartPtrInit, METH_VARARGS | METH_KEYWORDS);
    }

    else if (name.find("iterator") != std::string::npos || gIteratorTypes.find(name) != gIteratorTypes.end()) {
        ((PyTypeObject*)pyclass)->tp_iternext = (iternextfunc)StlIterNext;
        Utility::AddToClass(pyclass, CPPYY__next__, (PyCFunction)StlIterNext, METH_NOARGS);
        ((PyTypeObject*)pyclass)->tp_iter = (getiterfunc)PyObject_SelfIter;
        Utility::AddToClass(pyclass, "__iter__", (PyCFunction)PyObject_SelfIter, METH_NOARGS);
    }

    else if (name == "std::string") { // TODO: ask backend as well
        Utility::AddToClass(pyclass, "__repr__",  (PyCFunction)StlStringRepr,       METH_NOARGS);
        Utility::AddToClass(pyclass, "__str__",   (PyCFunction)StlStringStr,        METH_NOARGS);
        Utility::AddToClass(pyclass, "__bytes__", (PyCFunction)StlStringBytes,      METH_NOARGS);
        Utility::AddToClass(pyclass, "__cmp__",   (PyCFunction)StlStringCompare,    METH_O);
        Utility::AddToClass(pyclass, "__eq__",    (PyCFunction)StlStringIsEqual,    METH_O);
        Utility::AddToClass(pyclass, "__ne__",    (PyCFunction)StlStringIsNotEqual, METH_O);
        Utility::AddToClass(pyclass, "decode",    (PyCFunction)StlStringDecode,     METH_VARARGS | METH_KEYWORDS);
        Utility::AddToClass(pyclass, "__cpp_replace", "replace");
        Utility::AddToClass(pyclass, "replace",   (PyCFunction)StlStringReplace,    METH_VARARGS | METH_KEYWORDS);
        ((PyTypeObject*)pyclass)->tp_hash = (hashfunc)StlStringHash;
    }

    else if (name == "std::basic_string<wchar_t,std::char_traits<wchar_t>,std::allocator<wchar_t> >") {
        Utility::AddToClass(pyclass, "__repr__",  (PyCFunction)StlWStringRepr,       METH_NOARGS);
        Utility::AddToClass(pyclass, "__str__",   (PyCFunction)StlWStringStr,        METH_NOARGS);
        Utility::AddToClass(pyclass, "__bytes__", (PyCFunction)StlWStringBytes,      METH_NOARGS);
        Utility::AddToClass(pyclass, "__cmp__",   (PyCFunction)StlWStringCompare,    METH_O);
        Utility::AddToClass(pyclass, "__eq__",    (PyCFunction)StlWStringIsEqual,    METH_O);
        Utility::AddToClass(pyclass, "__ne__",    (PyCFunction)StlWStringIsNotEqual, METH_O);
    }

    else if (name == "complex<double>" || name == "std::complex<double>") {
        Utility::AddToClass(pyclass, "__cpp_real", "real");
        PyObject_SetAttrString(pyclass, "real",  PyDescr_NewGetSet((PyTypeObject*)pyclass, &ComplexDReal));
        Utility::AddToClass(pyclass, "__cpp_imag", "imag");
        PyObject_SetAttrString(pyclass, "imag",  PyDescr_NewGetSet((PyTypeObject*)pyclass, &ComplexDImag));
        Utility::AddToClass(pyclass, "__complex__", (PyCFunction)ComplexDComplex, METH_NOARGS);
        Utility::AddToClass(pyclass, "__repr__", (PyCFunction)ComplexRepr, METH_NOARGS);
    }

    else if (IsTemplatedSTLClass(name, "complex")) {
        Utility::AddToClass(pyclass, "__cpp_real", "real");
        PyObject_SetAttrString(pyclass, "real", PyDescr_NewGetSet((PyTypeObject*)pyclass, &realComplex));
        Utility::AddToClass(pyclass, "__cpp_imag", "imag");
        PyObject_SetAttrString(pyclass, "imag", PyDescr_NewGetSet((PyTypeObject*)pyclass, &imagComplex)); 
        Utility::AddToClass(pyclass, "__complex__", (PyCFunction)ComplexComplex, METH_NOARGS);
        Utility::AddToClass(pyclass, "__repr__", (PyCFunction)ComplexRepr, METH_NOARGS);
    }

// direct user access; there are two calls here:
//   - explicit pythonization: won't fall through to the base classes and is preferred if present
//   - normal pythonization: only called if explicit isn't present, falls through to base classes
    bool bUserOk = true; PyObject* res = nullptr;
    PyObject* pyname = CPyCppyy_PyText_FromString(name.c_str());
    if (HasAttrDirect(pyclass, PyStrings::gExPythonize)) {
        res = PyObject_CallMethodObjArgs(pyclass, PyStrings::gExPythonize, pyclass, pyname, nullptr);
        bUserOk = (bool)res;
    } else {
        PyObject* func = PyObject_GetAttr(pyclass, PyStrings::gPythonize);
        if (func) {
            res = PyObject_CallFunctionObjArgs(func, pyclass, pyname, nullptr);
            Py_DECREF(func);
            bUserOk = (bool)res;
        } else
            PyErr_Clear();
    }
    if (!bUserOk) {
        Py_DECREF(pyname);
        return false;
    } else {
        Py_XDECREF(res);
        // pyname handed to args tuple below
    }

// call registered pythonizors, if any: first run the namespace-specific pythonizors, then
// the global ones (the idea is to allow writing a pythonizor that see all classes)
    bool pstatus = true;
    std::string outer_scope = TypeManip::extract_namespace(name);
    if (!outer_scope.empty()) {
        auto p = gPythonizations.find(outer_scope);
        if (p != gPythonizations.end()) {
            PyObject* subname = CPyCppyy_PyText_FromString(
                name.substr(outer_scope.size()+2, std::string::npos).c_str());
            pstatus = run_pythonizors(pyclass, subname, p->second);
            Py_DECREF(subname);
        }
    }

    if (pstatus) {
        auto p = gPythonizations.find("");
        if (p != gPythonizations.end())
            pstatus = run_pythonizors(pyclass, pyname, p->second);
    }

    Py_DECREF(pyname);

// phew! all done ...
    return pstatus;
}
