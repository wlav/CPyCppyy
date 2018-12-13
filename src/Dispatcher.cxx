// Bindings
#include "CPyCppyy.h"
#include "Dispatcher.h"
#include "CPPConstructor.h"
#include "CPPOverload.h"
#include "CPPScope.h"

// Standard
#include <sstream>


//----------------------------------------------------------------------------
static bool includesDone = false;
bool CPyCppyy::InsertDispatcher(const std::string& name, CPPScope* klass, PyObject* dct)
{
// Scan all methods in dct and where it overloads base methods in klass, create
// dispatchers on the C++ side. Then interject the dispatcher class.
    if (Cppyy::IsNamespace(klass->fCppType) || !PyDict_Check(dct))
        return false;

    if (!includesDone) {
        bool okay = Cppyy::Compile("#ifdef _WIN32\n"
            "#pragma warning (disable : 4275)\n"
            "#pragma warning (disable : 4251)\n"
            "#pragma warning (disable : 4800)\n"
            "#endif\n"
            "#if defined(linux)\n"
            "#include <stdio.h>\n"
            "#ifdef _POSIX_C_SOURCE\n"
            "#undef _POSIX_C_SOURCE\n"
            "#endif\n"
            "#ifdef _FILE_OFFSET_BITS\n"
            "#undef _FILE_OFFSET_BITS\n"
            "#endif\n"
            "#ifdef _XOPEN_SOURCE\n"
            "#undef _XOPEN_SOURCE\n"
            "#endif\n"
            "#endif\n"
            "#include \"Python.h\"\n"
            "namespace CPyCppyy {\n"
       // the following really should live in a header ...
            "struct Parameter; struct CallContext;\n"
            "class Converter {\n"
            "public:\n"
            "   virtual ~Converter() {}\n"
            "   virtual bool SetArg(PyObject*, Parameter&, CallContext* = nullptr) = 0;\n"
            "   virtual PyObject* FromMemory(void* address);\n"
            "   virtual bool ToMemory(PyObject* value, void* address);\n"
            "};\n"
            "Converter* CreateConverter(const std::string& fullType, long* dims = nullptr);\n"
            "}\n");
        includesDone = okay;
    }

    if (!includesDone)
        return false;

// once classes can be extended, should consider re-use; for now, since derived
// python classes can differ in what they override, simply use different shims
    static int counter = 0;
    std::ostringstream osname;
    osname << name << ++counter;
    const std::string clName = osname.str();

// generate proxy class with the relevant method dispatchers
    std::ostringstream code;

// start class declaration
    code << "namespace __cppyy_internal {\n"
         << "class " << clName << " : public ::" << name << " {\n"
            "  PyObject* m_self;\n"
            "public:\n";

// constructors
    code << "  " << clName << "(PyObject* self) : m_self(self) {}\n";

// methods
    const Cppyy::TCppIndex_t nMethods = Cppyy::GetNumMethods(klass->fCppType);
    for (Cppyy::TCppIndex_t imeth = 0; imeth < nMethods; ++imeth) {
        Cppyy::TCppMethod_t method = Cppyy::GetMethod(klass->fCppType, imeth);

        std::string mtCppName = Cppyy::GetMethodName(method);
        PyObject* key = CPyCppyy_PyUnicode_FromString(mtCppName.c_str());
        int contains = PyDict_Contains(dct, key);
        Py_DECREF(key);
        if (contains == -1) PyErr_Clear();
        if (contains != 1) continue;

    // method declaration
        std::string retType = Cppyy::GetMethodResultType(method);
        code << "  " << retType << " " << mtCppName;

        // argument loop here ...
             code << "() {\n";

    // function body
        code << "    static CPyCppyy::Converter* conv = CPyCppyy::CreateConverter(\"" << retType << "\");\n"
                "    " << retType << " ret{};\n"
                "    PyObject* val = PyObject_CallMethod(m_self, (char*)\"" << mtCppName << "\", NULL);\n"
                "    conv->ToMemory(val, &ret);\n"
                "    return ret;\n"
                "  }\n";
    }

// finish class declaration
    code << "};\n}";
    if (!Cppyy::Compile(code.str()))
        return false;

    Cppyy::TCppScope_t disp = Cppyy::GetScope("__cppyy_internal::"+clName);
    if (!disp) return false;

// interject the new constructor only (this ensures that C++ sees the proxy, but
// Python does not, pre-empting circular calls)
    const auto& v = Cppyy::GetMethodIndicesFromName(disp, name);
    if (v.empty()) return false;
    Cppyy::TCppMethod_t cppmeth = Cppyy::GetMethod(disp, v[0]);
    CPPConstructor* ctor = new CPPDispatcherConstructor(disp, cppmeth);
    CPPOverload* ol = CPPOverload_New("__init__", ctor);
    bool isOk = PyObject_SetAttrString(
        (PyObject*)klass, const_cast<char*>("__init__"), (PyObject*)ol) == 0;
    Py_DECREF(ol);
    return isOk;
}

