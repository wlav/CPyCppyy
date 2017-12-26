// Bindings
#include "CPyCppyy.h"
#include "TemplateProxy.h"
#include "CPPClassMethod.h"
#include "CPPConstructor.h"
#include "CPPFunction.h"
#include "CPPMethod.h"
#include "CPPOverload.h"
#include "PyCallable.h"
#include "PyStrings.h"
#include "Utility.h"


namespace CPyCppyy {

//----------------------------------------------------------------------------
void TemplateProxy::Set(const std::string& cppname, const std::string& pyname, PyObject* pyclass)
{
// Initialize the proxy for the given 'pyclass.'
    fPyName       = CPyCppyy_PyUnicode_FromString(const_cast<char*>(pyname.c_str()));
    fCppName      = CPyCppyy_PyUnicode_FromString(const_cast<char*>(cppname.c_str()));
    Py_XINCREF(pyclass);
    fPyClass      = pyclass;
    fSelf         = nullptr;
    std::vector<PyCallable*> dummy;
    fNonTemplated = CPPOverload_New(pyname, dummy);
    fTemplated    = CPPOverload_New(pyname, dummy);
}

//----------------------------------------------------------------------------
void TemplateProxy::AddOverload(CPPOverload* mp) {
// Store overloads of this templated method.
    fNonTemplated->AddMethod(mp);
}

void TemplateProxy::AddOverload(PyCallable* pc) {
// Store overload of this templated method.
    fNonTemplated->AddMethod(pc);
}

void TemplateProxy::AddTemplate(PyCallable* pc)
{
// Store know template methods.
    fTemplated->AddMethod(pc);
}


//= CPyCppyy template proxy construction/destruction =========================
static TemplateProxy* tpp_new(PyTypeObject*, PyObject*, PyObject*)
{
// Create a new empty template method proxy.
    TemplateProxy* pytmpl = PyObject_GC_New(TemplateProxy, &TemplateProxy_Type);
    pytmpl->fCppName      = nullptr;
    pytmpl->fPyName       = nullptr;
    pytmpl->fPyClass      = nullptr;
    pytmpl->fSelf         = nullptr;
    pytmpl->fNonTemplated = nullptr;
    pytmpl->fTemplated    = nullptr;

    PyObject_GC_Track(pytmpl);
    return pytmpl;
}

//----------------------------------------------------------------------------
static int tpp_clear(TemplateProxy* pytmpl)
{
// Garbage collector clear of held python member objects.
    Py_CLEAR(pytmpl->fCppName);
    Py_CLEAR(pytmpl->fPyName);
    Py_CLEAR(pytmpl->fPyClass);
    Py_CLEAR(pytmpl->fSelf);
    Py_CLEAR(pytmpl->fNonTemplated);
    Py_CLEAR(pytmpl->fTemplated);

    return 0;
}

//----------------------------------------------------------------------------
static void tpp_dealloc(TemplateProxy* pytmpl)
{
// Destroy the given template method proxy.
    PyObject_GC_UnTrack(pytmpl);
    tpp_clear(pytmpl);
    PyObject_GC_Del(pytmpl);
}

//----------------------------------------------------------------------------
static PyObject* tpp_doc(TemplateProxy* pytmpl, void*)
{
// Forward to method proxies to doc all overloads
    PyObject* doc = nullptr;
    if (pytmpl->fNonTemplated)
        doc = PyObject_GetAttrString((PyObject*)pytmpl->fNonTemplated, "__doc__");
    if (pytmpl->fTemplated) {
        PyObject* doc2 = PyObject_GetAttrString((PyObject*)pytmpl->fTemplated, "__doc__");
        if (doc && doc2) {
            CPyCppyy_PyUnicode_AppendAndDel(&doc, CPyCppyy_PyUnicode_FromString("\n"));
            CPyCppyy_PyUnicode_AppendAndDel(&doc, doc2);
        } else if (!doc && doc2) {
            doc = doc2;
        }
    }

    if (doc)
        return doc;

    return CPyCppyy_PyUnicode_FromString(TemplateProxy_Type.tp_doc);
}

//----------------------------------------------------------------------------
static int tpp_traverse(TemplateProxy* pytmpl, visitproc visit, void* arg)
{
// Garbage collector traverse of held python member objects.
    Py_VISIT(pytmpl->fCppName);
    Py_VISIT(pytmpl->fPyName);
    Py_VISIT(pytmpl->fPyClass);
    Py_VISIT(pytmpl->fSelf);
    Py_VISIT(pytmpl->fNonTemplated);
    Py_VISIT(pytmpl->fTemplated);

    return 0;
}

//= CPyCppyy template proxy callable behavior ================================
static PyObject* tpp_call(TemplateProxy* pytmpl, PyObject* args, PyObject* kwds)
{
// Dispatcher to the actual member method, several uses possible; in order:
//
// case 1: select known non-template overload
//
//    obj.method(a0, a1, ...)
//       => obj->method(a0, a1, ...)        // non-template
//
// case 2: select known template overload
//
//    obj.method(a0, a1, ...)
//       => obj->method(a0, a1, ...)        // all known templates
//
// case 3: auto-instantiation from types of arguments
//
//    obj.method(a0, a1, ...)
//       => obj->method<type(a0), type(a1), ...>(a0, a1, ...)
//
// Note: explicit instantiation needs to use [] syntax:
//
//    obj.method[type<a0>, type<a1>, ...](a0, a1, ...)
//

// case 1: select known non-template overload

// simply forward the call: all non-templated methods are defined on class definition
// and thus already available
    PyObject* pymeth = CPPOverload_Type.tp_descr_get(
        (PyObject*)pytmpl->fNonTemplated, pytmpl->fSelf, (PyObject*)&CPPOverload_Type);
// now call the method with the arguments (loops internally)
    PyObject* result = CPPOverload_Type.tp_call(pymeth, args, kwds);
    Py_DECREF(pymeth); pymeth = 0;
    if (result)
        return result;
// TODO: collect error here, as the failure may be either an overload
// failure after which we should continue; or a real failure, which should
// be reported.
    PyErr_Clear();

// error check on method() which can not be derived if non-templated case fails
    Py_ssize_t nArgs = PyTuple_GET_SIZE(args);
    if (nArgs == 0) {
        PyErr_Format(PyExc_TypeError, "template method \'%s\' with no arguments must be explicit",
            CPyCppyy_PyUnicode_AsString(pytmpl->fPyName));
        return nullptr;
    }

// case 2: non-instantiating obj->method< t0, t1, ... >(a0, a1, ...)

// build "<type, type, ...>" part of method name
    const std::string& name_v1 = Utility::ConstructTemplateArgs(pytmpl->fCppName, args, 0);
    if (name_v1.size()) {
        PyObject* pyname_v1 = CPyCppyy_PyUnicode_FromString(name_v1.c_str());
    // lookup method on self (to make sure it propagates), which is readily callable
        pymeth = PyObject_GetAttr(pytmpl->fSelf ? pytmpl->fSelf : pytmpl->fPyClass, pyname_v1);
        Py_DECREF(pyname_v1); pyname_v1 = 0;
        if (pymeth)     // overloads stop here, as this is an explicit match
            return pymeth;         // callable method, next step is by user
    }
    PyErr_Clear();

// case 3: loop over all previously instantiated templates
    pymeth = CPPOverload_Type.tp_descr_get(
        (PyObject*)pytmpl->fTemplated, pytmpl->fSelf, (PyObject*)&CPPOverload_Type);
// now call the method with the arguments (loops internally)
    result = CPPOverload_Type.tp_call(pymeth, args, kwds);
    Py_DECREF(pymeth); pymeth = 0;
    if (result)
        return result;
// TODO: collect error here, as the failure may be either an overload
// failure after which we should continue; or a real failure, which should
// be reported.
    PyErr_Clear();

// still here? try instantiating methods
    bool isType = false;
    Int_t nStrings = 0;
    PyObject* tpArgs = PyTuple_New(nArgs);
    for (int i = 0; i < nArgs; ++i) {
        PyObject* itemi = PyTuple_GET_ITEM(args, i);
        if (PyType_Check(itemi)) isType = true;
#if PY_VERSION_HEX >= 0x03000000
        else if (!isType && PyUnicode_Check(itemi)) nStrings += 1;
#else
        else if (!isType && PyBytes_Check(itemi)) nStrings += 1;
#endif
    // special case for arrays
        PyObject* pytc = PyObject_GetAttr(itemi, PyStrings::gTypeCode);
        if (!(pytc && CPyCppyy_PyUnicode_Check(pytc))) {
        // normal case (not an array)
            PyErr_Clear();
            PyObject* tp = (PyObject*)Py_TYPE(itemi);
            Py_INCREF(tp);
            PyTuple_SET_ITEM(tpArgs, i, tp);
        } else {
        // array, build up a pointer type
            char tc = ((char*)CPyCppyy_PyUnicode_AsString(pytc))[0];
            const char* ptrname = 0;
            switch (tc) {
                case 'b': ptrname = "char*";           break;
                case 'h': ptrname = "short*";          break;
                case 'H': ptrname = "unsigned short*"; break;
                case 'i': ptrname = "int*";            break;
                case 'I': ptrname = "unsigned int*";   break;
                case 'l': ptrname = "long*";           break;
                case 'L': ptrname = "unsigned long*";  break;
                case 'f': ptrname = "float*";          break;
                case 'd': ptrname = "double*";         break;
                default:  ptrname = "void*";  // TODO: verify if this is right
            }
            if (ptrname) {
                PyObject* pyptrname = PyBytes_FromString(ptrname);
                PyTuple_SET_ITEM(tpArgs, i, pyptrname);
            // string added, but not counted towards nStrings
            } else {
            // this will cleanly fail instantiation
                Py_INCREF(pytc);
                PyTuple_SET_ITEM(tpArgs, i, pytc);
            }
        }
        Py_XDECREF(pytc);
    }

    Cppyy::TCppScope_t scope = ((CPPClass*)pytmpl->fPyClass)->fCppType;
    const std::string& tmplname = CPyCppyy_PyUnicode_AsString(pytmpl->fCppName);

// case 4a: instantiating obj->method<T0, T1, ...>(type(a0), type(a1), ...)(a0, a1, ...)
    if (!isType && nStrings != nArgs) {      // no types among args and not all strings
        const std::string& name_v2 = Utility::ConstructTemplateArgs(nullptr, tpArgs, 0);
        if (name_v2.size()) {
            std::string proto = name_v2.substr(1, name_v2.size()-2);
        // the following causes instantiation as necessary
            Cppyy::TCppMethod_t cppmeth = Cppyy::GetMethodTemplate(scope, tmplname, proto);
            if (cppmeth) {    // overload stops here
                PyCallable* meth = nullptr;
                if (Cppyy::IsNamespace(scope))
                    meth = new CPPFunction(scope, cppmeth);
                else if (Cppyy::IsStaticMethod(cppmeth))
                    meth = new CPPClassMethod(scope, cppmeth);
                else if (Cppyy::IsConstructor(cppmeth))
                    meth = new CPPConstructor(scope, cppmeth);
                else
                    meth = new CPPMethod(scope, cppmeth);

                pytmpl->fTemplated->AddMethod(meth->Clone());
                pymeth = (PyObject*)CPPOverload_New(tmplname, meth);
                PyObject_SetAttrString(pytmpl->fPyClass, (char*)tmplname.c_str(), (PyObject*)pymeth);
                Py_DECREF(pymeth);
                pymeth = PyObject_GetAttrString(
                    pytmpl->fSelf ? pytmpl->fSelf : pytmpl->fPyClass, (char*)tmplname.c_str());

            // now call the method directly
                PyObject* result = CPPOverload_Type.tp_call(pymeth, args, kwds);
                Py_DECREF(pymeth);
                return result;
            }
        }
    }

  /*

   // case 4b/5: instantiating obj->method< t0, t1, ... >(a0, a1, ...)
      if (pyname_v1) {
          std::string mname = CPyCppyy_PyUnicode_AsString(pyname_v1);
       // the following causes instantiation as necessary
          TMethod* cppmeth = klass ? klass->GetMethodAny(mname.c_str()) : 0;
          if (cppmeth) {    // overload stops here
              pymeth = (PyObject*)CPPOverload_New(
                  mname, new CPPMethod(Cppyy::GetScope(klass->GetName(), (Cppyy::TCppMethod_t)cppmeth));
              PyObject_SetAttr(pytmpl->fPyClass, pyname_v1, (PyObject*)pymeth);
              if (mname != cppmeth->GetName()) // happens with typedefs and template default arguments
                  PyObject_SetAttrString(pytmpl->fPyClass, (char*)mname.c_str(), (PyObject*)pymeth);
              Py_DECREF(pymeth);
              pymeth = PyObject_GetAttr(pytmpl->fSelf ? pytmpl->fSelf : pytmpl->fPyClass, pyname_v1);
              Py_DECREF(pyname_v1);
              return pymeth;         // callable method, next step is by user
         }
         Py_DECREF(pyname_v1);
      }
   */

// moderately generic error message, but should be clear enough
    PyErr_Format(PyExc_TypeError, "can not resolve method template call for \'%s\'",
        CPyCppyy_PyUnicode_AsString(pytmpl->fPyName));
    return nullptr;
}

//----------------------------------------------------------------------------
static PyObject* tpp_subscript(TemplateProxy* pytmpl, PyObject* args)
{
// Explicit template member lookup/instantiation.
    PyObject* newArgs;
    if (!PyTuple_Check(args)) {
        newArgs = PyTuple_New(1);
        Py_INCREF(args);
        PyTuple_SET_ITEM(newArgs, 0, args);
    } else {
        Py_INCREF(args);
        newArgs = args;
    }

// build "< type, type, ... >" part of method name
    const std::string& tmpl_args = Utility::ConstructTemplateArgs(nullptr, newArgs, 0);
    std::string meth_name = CPyCppyy_PyUnicode_AsString(pytmpl->fCppName);
    meth_name.append(tmpl_args);
    Py_DECREF(newArgs);
    if (!tmpl_args.empty()) {
    // lookup method on self (to make sure it propagates), which is readily callable
        PyObject* pytmpl_name = CPyCppyy_PyUnicode_FromString(meth_name.c_str());
        PyObject* pymeth = PyObject_GetAttr(
                pytmpl->fSelf ? pytmpl->fSelf : pytmpl->fPyClass, pytmpl_name);
        if (pymeth) {    // overloads stop here, as this is an explicit match
            Py_DECREF(pytmpl_name);
            return pymeth;         // callable method, next step is by user
        }
        else {
        // lookup failed: try instantiating
            PyErr_Clear();
            Cppyy::TCppScope_t scope = ((CPPClass*)pytmpl->fPyClass)->fCppType;

        // the following causes instantiation as necessary
            Cppyy::TCppMethod_t cppmeth = Cppyy::GetMethodTemplate(scope, meth_name, "");
            if (cppmeth) {    // overload stops here
                PyCallable* meth = nullptr;
                if (Cppyy::IsNamespace(scope))
                    meth = new CPPFunction(scope, cppmeth);
                else if (Cppyy::IsStaticMethod(cppmeth))
                    meth = new CPPClassMethod(scope, cppmeth);
                else if (Cppyy::IsConstructor(cppmeth))
                    meth = new CPPConstructor(scope, cppmeth);
                else
                    meth = new CPPMethod(scope, cppmeth);
                PyObject* pymeth = (PyObject*)CPPOverload_New(meth_name, meth);
                PyObject_SetAttr(pytmpl->fPyClass, pytmpl_name, (PyObject*)pymeth);
                pytmpl->fTemplated->AddMethod((CPPOverload*)pymeth); // steals ref
                pymeth = PyObject_GetAttr(pytmpl->fSelf ? pytmpl->fSelf : pytmpl->fPyClass, pytmpl_name);
                Py_DECREF(pytmpl_name);
                return pymeth;          // callable method, next step is by user
            }
        }
        Py_DECREF(pytmpl_name);
    }

    return nullptr;
}


//----------------------------------------------------------------------------
static TemplateProxy* tpp_descrget(TemplateProxy* pytmpl, PyObject* pyobj, PyObject*)
{
// create and use a new template proxy (language requirement)
    TemplateProxy* newPyTmpl = (TemplateProxy*)TemplateProxy_Type.tp_alloc(&TemplateProxy_Type, 0);

// copy name and class pointers
    Py_INCREF(pytmpl->fCppName);
    newPyTmpl->fCppName = pytmpl->fCppName;

    Py_INCREF(pytmpl->fPyName);
    newPyTmpl->fPyName = pytmpl->fPyName;

    Py_XINCREF(pytmpl->fPyClass);
    newPyTmpl->fPyClass = pytmpl->fPyClass;

// copy non-templated method proxy pointer
    Py_INCREF(pytmpl->fNonTemplated);
    newPyTmpl->fNonTemplated = pytmpl->fNonTemplated;

// copy templated method proxy pointer
    Py_INCREF(pytmpl->fTemplated);
    newPyTmpl->fTemplated = pytmpl->fTemplated;

// new method is to be bound to current object (may be nullptr)
    Py_XINCREF(pyobj);
    newPyTmpl->fSelf = pyobj;

    return newPyTmpl;
}

//----------------------------------------------------------------------------
static PyMappingMethods tpp_as_mapping = {
    nullptr, (binaryfunc)tpp_subscript, nullptr
};

static PyGetSetDef tpp_getset[] = {
    {(char*)"__doc__", (getter)tpp_doc, nullptr, nullptr, nullptr},
    {(char*)nullptr,   nullptr,         nullptr, nullptr, nullptr}
};


//= CPyCppyy template proxy type =============================================
PyTypeObject TemplateProxy_Type = {
   PyVarObject_HEAD_INIT(&PyType_Type, 0)
   (char*)"cppyy.TemplateProxy", // tp_name
   sizeof(TemplateProxy),     // tp_basicsize
   0,                         // tp_itemsize
   (destructor)tpp_dealloc,   // tp_dealloc
   0,                         // tp_print
   0,                         // tp_getattr
   0,                         // tp_setattr
   0,                         // tp_compare
   0,                         // tp_repr
   0,                         // tp_as_number
   0,                         // tp_as_sequence
   &tpp_as_mapping,           // tp_as_mapping
   0,                         // tp_hash
   (ternaryfunc)tpp_call,     // tp_call
   0,                         // tp_str
   0,                         // tp_getattro
   0,                         // tp_setattro
   0,                         // tp_as_buffer
   Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,      // tp_flags
   (char*)"cppyy template proxy (internal)",     // tp_doc
   (traverseproc)tpp_traverse,// tp_traverse
   (inquiry)tpp_clear,        // tp_clear
   0,                         // tp_richcompare
   0,                         // tp_weaklistoffset
   0,                         // tp_iter
   0,                         // tp_iternext
   0,                         // tp_methods
   0,                         // tp_members
   tpp_getset,                // tp_getset
   0,                         // tp_base
   0,                         // tp_dict
   (descrgetfunc)tpp_descrget,// tp_descr_get
   0,                         // tp_descr_set
   0,                         // tp_dictoffset
   0,                         // tp_init
   0,                         // tp_alloc
   (newfunc)tpp_new,          // tp_new
   0,                         // tp_free
   0,                         // tp_is_gc
   0,                         // tp_bases
   0,                         // tp_mro
   0,                         // tp_cache
   0,                         // tp_subclasses
   0                          // tp_weaklist
#if PY_VERSION_HEX >= 0x02030000
   , 0                        // tp_del
#endif
#if PY_VERSION_HEX >= 0x02060000
   , 0                        // tp_version_tag
#endif
#if PY_VERSION_HEX >= 0x03040000
   , 0                        // tp_finalize
#endif
};

} // namespace CPyCppyy
