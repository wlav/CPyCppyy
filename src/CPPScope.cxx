// Bindings
#include "CPyCppyy.h"
#include "CPPScope.h"
#include "CPPDataMember.h"
#include "CPPFunction.h"
#include "CPPOverload.h"
#include "ProxyWrappers.h"
#include "PyStrings.h"
#include "TemplateProxy.h"
#include "TypeManip.h"
#include "Utility.h"

// Standard
#include <string.h>
#include <string>
#include <vector>


namespace CPyCppyy {

extern PyTypeObject CPPInstance_Type;

//= CPyCppyy type proxy construction/destruction =============================
static PyObject* meta_alloc(PyTypeObject* metatype, Py_ssize_t nitems)
{
    return PyType_Type.tp_alloc(metatype, nitems);
}

//----------------------------------------------------------------------------
static void meta_dealloc(CPPScope* metatype)
{
    delete metatype->fCppObjects; metatype->fCppObjects = nullptr;
    free(metatype->fModuleName);
    return PyType_Type.tp_dealloc((PyObject*)metatype);
}

//-----------------------------------------------------------------------------
static PyObject* meta_getcppname(CPPScope* meta, void*)
{
    if ((void*)meta == (void*)&CPPInstance_Type)
        return CPyCppyy_PyUnicode_FromString("CPPInstance_Type");
    return CPyCppyy_PyUnicode_FromString(Cppyy::GetScopedFinalName(meta->fCppType).c_str());
}

//-----------------------------------------------------------------------------
static PyObject* meta_getmodule(CPPScope* meta, void*)
{
    if ((void*)meta == (void*)&CPPInstance_Type)
        return CPyCppyy_PyUnicode_FromString("cppyy.gbl");

    if (meta->fModuleName)
        return CPyCppyy_PyUnicode_FromString(meta->fModuleName);

    std::string modname = Cppyy::GetScopedFinalName(meta->fCppType);
    std::string::size_type pos1 = modname.rfind("::");
    if (modname.empty() || pos1 == std::string::npos)
        return CPyCppyy_PyUnicode_FromString(const_cast<char*>("cppyy.gbl"));

    PyObject* pymodule = nullptr;
    std::string::size_type pos2 = modname.rfind("::", pos1-1);
    pos2 = (pos2 == std::string::npos) ? 0 : pos2 + 2;
    PyObject* pyscope = CPyCppyy::GetScopeProxy(Cppyy::GetScope(modname.substr(0, pos1)));
    if (pyscope) {
        pymodule = PyObject_GetAttr(pyscope, PyStrings::gModule);
        CPyCppyy_PyUnicode_AppendAndDel(&pymodule,
            CPyCppyy_PyUnicode_FromString(('.'+modname.substr(pos2, pos1-pos2)).c_str()));

        Py_DECREF(pyscope);
    }

    if (pymodule)
        return pymodule;
    PyErr_Clear();

    TypeManip::cppscope_to_pyscope(modname);
    return CPyCppyy_PyUnicode_FromString(("cppyy.gbl."+modname.substr(0, pos1)).c_str());
}

//-----------------------------------------------------------------------------
static int meta_setmodule(CPPScope* meta, PyObject* value, void*)
{
    if ((void*)meta == (void*)&CPPInstance_Type) {
        PyErr_SetString(PyExc_AttributeError,
            "attribute \'__module__\' of 'cppyy.CPPScope\' objects is not writable");
        return -1;
    }

    const char* newname = CPyCppyy_PyUnicode_AsStringChecked(value);
    if (!value)
        return -1;

    free(meta->fModuleName);
    Py_ssize_t sz = CPyCppyy_PyUnicode_GET_SIZE(value);
    meta->fModuleName = (char*)malloc(sz+1);
    memcpy(meta->fModuleName, newname, sz+1);

    return 0;
}

//----------------------------------------------------------------------------
static PyObject* meta_repr(CPPScope* metatype)
{
// Specialized b/c type_repr expects __module__ to live in the dictionary,
// whereas it is a property (to save memory).
    if ((void*)metatype == (void*)&CPPInstance_Type)
        return CPyCppyy_PyUnicode_FromFormat(
            const_cast<char*>("<class cppyy.CPPInstance at %p>"), metatype);

    PyObject* modname = meta_getmodule(metatype, nullptr);
    std::string clName = Cppyy::GetFinalName(metatype->fCppType);
    const char* kind = Cppyy::IsNamespace(metatype->fCppType) ? "namespace" : "class";

    PyObject* repr = CPyCppyy_PyUnicode_FromFormat("<%s %s.%s at %p>",
        kind, CPyCppyy_PyUnicode_AsString(modname), clName.c_str(), metatype);

    Py_DECREF(modname);
    return repr;
}


//= CPyCppyy type metaclass behavior =========================================
static bool gbl_done = false;
static PyObject* pt_new(PyTypeObject* subtype, PyObject* args, PyObject* kwds)
{
// Called when CPPScope acts as a metaclass; since type_new always resets
// tp_alloc, and since it does not call tp_init on types, the metaclass is
// being fixed up here, and the class is initialized here as well.

// fixup of metaclass (left permanent, and in principle only called once b/c
// cppyy caches python classes)
    subtype->tp_alloc   = (allocfunc)meta_alloc;
    subtype->tp_dealloc = (destructor)meta_dealloc;

    if (!gbl_done &&
            strncmp(CPyCppyy_PyUnicode_AsString(PyTuple_GET_ITEM(args, 0)), "gbl", 3) == 0) {
    // happens on startup, since we need to have some name for gbl for the
    // class declaration to be valid
        gbl_done = true;
        PyObject* newargs = PyTuple_New(PyTuple_GET_SIZE(args));
        PyTuple_SET_ITEM(newargs, 0, CPyCppyy_PyUnicode_FromString(""));
        for (int i = 1; i < PyTuple_GET_SIZE(args); ++i) {
            PyObject* item = PyTuple_GET_ITEM(args, i);
            Py_INCREF(item);
            PyTuple_SET_ITEM(newargs, i, item);
        }
        Py_DECREF(args);
        args = newargs;
    }

// creation of the python-side class
    CPPScope* result = (CPPScope*)PyType_Type.tp_new(subtype, args, kwds);
    if (!result)
        return nullptr;

    result->fCppObjects = new CppToPyMap_t;
    result->fModuleName = nullptr;

// initialization of class (based on metatype)
    const char* mp = strstr(subtype->tp_name, "_meta");
    if (!mp || !CPPScope_CheckExact(subtype)) {
    // there has been a user meta class override in a derived class, so do
    // the consistent thing, thus allowing user control over naming
        result->fCppType = Cppyy::GetScope(
            CPyCppyy_PyUnicode_AsString(PyTuple_GET_ITEM(args, 0)));
    } else {
    // coming here from cppyy or from sub-classing in python; take the
    // C++ type from the meta class to make sure that the latter category
    // has fCppType properly set (it inherits the meta class, but has an
    // otherwise unknown (or wrong) C++ type)
        result->fCppType = ((CPPScope*)subtype)->fCppType;
    }

    return (PyObject*)result;
}

//----------------------------------------------------------------------------
static PyObject* pt_getattro(PyObject* pyclass, PyObject* pyname)
{
// normal type-based lookup
    PyObject* attr = PyType_Type.tp_getattro(pyclass, pyname);
    if (attr || pyclass == (PyObject*)&CPPInstance_Type)
        return attr;

// more elaborate search in case of failure (eg. for inner classes on demand)
    if (CPyCppyy_PyUnicode_CheckExact(pyname)) {
        PyObject *etype, *value, *trace;
        PyErr_Fetch(&etype, &value, &trace);       // clears current exception

    // filter for python specials and lookup qualified class or function
        std::string name = CPyCppyy_PyUnicode_AsString(pyname);
        if (name.size() <= 2 || name.substr(0, 2) != "__") {
            attr = CreateScopeProxy(name, pyclass);

        // namespaces may have seen updates in their list of global functions, which
        // are available as "methods" even though they're not really that
            if (!attr && CPPScope_Check(pyclass)) {
                PyErr_Clear();
                Cppyy::TCppScope_t scope = ((CPPScope*)pyclass)->fCppType;

                if (Cppyy::IsNamespace(scope)) {
                // tickle lazy lookup of functions
                    if (!attr) {
                        const std::vector<Cppyy::TCppIndex_t> methods =
                            Cppyy::GetMethodIndicesFromName(scope, name);
                        if (!methods.empty()) {
                        // function exists, now collect overloads
                            std::vector< PyCallable* > overloads;
                            const size_t nmeth = Cppyy::GetNumMethods(scope);
                            for (size_t imeth = 0; imeth < nmeth; ++imeth) {
                                Cppyy::TCppMethod_t method = Cppyy::GetMethod(scope, imeth);
                                if (Cppyy::GetMethodName(method) == name)
                                    overloads.push_back(new CPPFunction(scope, method));
                            }

                        // Note: can't re-use Utility::AddClass here, as there's the risk of
                        // a recursive call. Simply add method directly, as we're guaranteed
                        // that it doesn't exist yet.
                            attr = (PyObject*)CPPOverload_New(name, overloads);
                        }
                    }

                // tickle lazy lookup of data members
                    if (!attr) {
                        Cppyy::TCppIndex_t dmi = Cppyy::GetDatamemberIndex(scope, name);
                        if (0 <= dmi) attr = (PyObject*)CPPDataMember_New(scope, dmi);
                    }
                }

            // function templates that have not been instantiated
                if (!attr && Cppyy::ExistsMethodTemplate(scope, name)) {
                    attr = (PyObject*)TemplateProxy_New(name, name, pyclass);
                }

            // enums types requested as type (rather than the constants)
            // TODO: IsEnum should deal with the scope, using klass->GetListOfEnums()->FindObject()
                if (!attr && Cppyy::IsEnum(Cppyy::GetScopedFinalName(scope)+"::"+name)) {
                // special case; enum types; for now, pretend int
                // TODO: although fine for C++98, this isn't correct in C++11
                    Py_INCREF(&PyInt_Type);
                    attr = (PyObject*)&PyInt_Type;
                }

                if (attr) {
                    PyObject_SetAttr(pyclass, pyname, attr);
                    Py_DECREF(attr);
                    attr = PyType_Type.tp_getattro(pyclass, pyname);
                }
            }

            if (!attr && !CPPScope_Check(pyclass) /* at global or module-level only */) {
                PyErr_Clear();
            // get the attribute as a global
                attr = GetCppGlobal(name /*, tag */);
                if (CPPDataMember_Check(attr)) {
                    PyObject_SetAttr((PyObject*)Py_TYPE(pyclass), pyname, attr);
                    Py_DECREF(attr);
                    attr = PyType_Type.tp_getattro(pyclass, pyname);
                } else if (attr)
                    PyObject_SetAttr(pyclass, pyname, attr);
            }

        }

    // if failed, then the original error is likely to be more instructive
        if (!attr && etype)
            PyErr_Restore(etype, value, trace);
        else if (!attr) {
            PyObject* sklass = PyObject_Str(pyclass);
            PyErr_Format(PyExc_AttributeError, "%s has no attribute \'%s\'",
                CPyCppyy_PyUnicode_AsString(sklass), CPyCppyy_PyUnicode_AsString(pyname));
            Py_DECREF(sklass);
        }

    // attribute is cached, if found
    }

    return attr;
}


//----------------------------------------------------------------------------
// p2.7 does not have a __dir__ in object, and object.__dir__ in p3 does not
// quite what I'd expected of it, so the following pulls in the internal code
#include "PyObjectDir27.inc"

static PyObject* meta_dir(CPPScope* klass)
{
// Collect a list of everything (currently) available in the namespace.
// The backend can filter by returning empty strings. Special care is
// taken for functions, which need not be unique (overloading).
    using namespace Cppyy;

    if ((void*)klass == (void*)&CPPInstance_Type)
        return PyList_New(0);

    PyObject* defdir = _generic_dir((PyObject*)klass);
    if (!IsNamespace(klass->fCppType))
        return defdir;

    PyObject* alldir = PySet_New(defdir);
    Py_DECREF(defdir);

// add (sub)scopes
    for (TCppIndex_t i = 0; i < GetNumScopes(klass->fCppType); ++i) {
        PyObject* entry = CPyCppyy_PyUnicode_FromString(
                              GetScopeName(klass->fCppType, i).c_str());
        PySet_Add(alldir, entry);
        Py_DECREF(entry);
    }

// add methods
    for (TCppIndex_t i = 0; i < GetNumMethods(klass->fCppType); ++i) {
        TCppMethod_t meth = GetMethod(klass->fCppType, i);
        if (IsPublicMethod(meth)) {
            const std::string& mname = GetMethodName(meth);
            if (!mname.empty()) {
                PyObject* entry = CPyCppyy_PyUnicode_FromString(
                    CPyCppyy::Utility::MapOperatorName(mname, GetMethodNumArgs(meth)).c_str());
                PySet_Add(alldir, entry);
                Py_DECREF(entry);
            }
        }
    }

// add (global) data
    for (TCppIndex_t i = 0; i < GetNumDatamembers(klass->fCppType); ++i) {
        if (IsPublicData(klass->fCppType, i)) {
            const std::string& dname = GetDatamemberName(klass->fCppType, i);
            if (!dname.empty()) {
                PyObject* entry = CPyCppyy_PyUnicode_FromString(dname.c_str());
                PySet_Add(alldir, entry);
                Py_DECREF(entry);
            }
        }
    }

#if PY_VERSION_HEX < 0x03000000
// copy into python list
    Py_ssize_t nentries = PySet_GET_SIZE(alldir);
    PyObject* ll_alldir = PyList_New(nentries);
    for (Py_ssize_t i = 0; i < nentries; ++i)
        PyList_SET_ITEM(ll_alldir, i, PySet_Pop(alldir));
    Py_DECREF(alldir);
    return ll_alldir;
#else
    return alldir;
#endif
}

//-----------------------------------------------------------------------------
static PyMethodDef meta_methods[] = {
    {(char*)"__dir__", (PyCFunction)meta_dir, METH_NOARGS, nullptr},
    {(char*)nullptr, nullptr, 0, nullptr}
};


//-----------------------------------------------------------------------------
static PyGetSetDef meta_getset[] = {
    {(char*)"__cppname__", (getter)meta_getcppname, nullptr, nullptr, nullptr},
    {(char*)"__module__",  (getter)meta_getmodule,  (setter)meta_setmodule, nullptr, nullptr},
    {(char*)nullptr, nullptr, nullptr, nullptr, nullptr}
};


//= CPyCppyy object proxy type type ==========================================
PyTypeObject CPPScope_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    (char*)"cppyy.CPPScope",       // tp_name
    sizeof(CPyCppyy::CPPScope),    // tp_basicsize
    0,                             // tp_itemsize
    0,                             // tp_dealloc
    0,                             // tp_print
    0,                             // tp_getattr
    0,                             // tp_setattr
    0,                             // tp_compare
    (reprfunc)meta_repr,           // tp_repr
    0,                             // tp_as_number
    0,                             // tp_as_sequence
    0,                             // tp_as_mapping
    0,                             // tp_hash
    0,                             // tp_call
    0,                             // tp_str
    (getattrofunc)pt_getattro,     // tp_getattro
    0,                             // tp_setattro
    0,                             // tp_as_buffer
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,     // tp_flags
    (char*)"CPyCppyy metatype (internal)",        // tp_doc
    0,                             // tp_traverse
    0,                             // tp_clear
    0,                             // tp_richcompare
    0,                             // tp_weaklistoffset
    0,                             // tp_iter
    0,                             // tp_iternext
    meta_methods,                  // tp_methods
    0,                             // tp_members
    meta_getset,                   // tp_getset
    &PyType_Type,                  // tp_base
    0,                             // tp_dict
    0,                             // tp_descr_get
    0,                             // tp_descr_set
    0,                             // tp_dictoffset
    0,                             // tp_init
    0,                             // tp_alloc
    (newfunc)pt_new,               // tp_new
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
