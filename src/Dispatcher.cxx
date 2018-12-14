// Bindings
#include "CPyCppyy.h"
#include "Dispatcher.h"
#include "CPPScope.h"

// Standard
#include <sstream>


//----------------------------------------------------------------------------
static bool includesDone = false;
bool CPyCppyy::InsertDispatcher(CPPScope* klass, PyObject* dct)
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

    const std::string& baseName       = Cppyy::GetFinalName(klass->fCppType);
    const std::string& baseNameScoped = Cppyy::GetScopedFinalName(klass->fCppType);

// once classes can be extended, should consider re-use; for now, since derived
// python classes can differ in what they override, simply use different shims
    static int counter = 0;
    std::ostringstream osname;
    osname << baseName << ++counter;
    const std::string& derivedName = osname.str();

// generate proxy class with the relevant method dispatchers
    std::ostringstream code;

// start class declaration
    code << "namespace __cppyy_internal {\n"
         << "class " << derivedName << " : public ::" << baseNameScoped << " {\n"
            "  PyObject* m_self;\n"
            "public:\n";

// constructors are simply inherited
    code << "  using " << baseName << "::" << baseName << ";\n";

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

    // function body (TODO: if the method throws a C++ exception, the GIL will
    // not be released.)
        code << "    static CPyCppyy::Converter* conv = CPyCppyy::CreateConverter(\"" << retType << "\");\n"
                "    " << retType << " ret{};\n"
                "    PyGILState_STATE state = PyGILState_Ensure();\n"
                "    PyObject* val = PyObject_CallMethod(m_self, (char*)\"" << mtCppName << "\", NULL);\n"
                "    if (val) conv->ToMemory(val, &ret);\n"
                "    else PyErr_Print();\n"       // should throw TPyException instead
                "    PyGILState_Release(state);\n"
                "    return ret;\n"
                "  }\n";
    }

// finish class declaration
    code << "};\n}";
    if (!Cppyy::Compile(code.str()))
        return false;

// keep track internally of the actual C++ type (this is used in
// CPPConstructor to call the dispatcher's one instead of the base)
    Cppyy::TCppScope_t disp = Cppyy::GetScope("__cppyy_internal::"+derivedName);
    if (!disp) return false;
    klass->fCppType = disp;

    return true;
}
