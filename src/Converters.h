#ifndef CPYCPPYY_CONVERTERS_H
#define CPYCPPYY_CONVERTERS_H

// Standard
#include <limits.h>
#include <string>
#include <map>


namespace CPyCppyy {

    struct TParameter;
    struct TCallContext;

    class TConverter {
    public:
        virtual ~TConverter() {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr) = 0;
        virtual PyObject* FromMemory(void* address);
        virtual bool ToMemory(PyObject* value, void* address);
    };

// converters for special cases
    class TVoidArrayConverter : public TConverter {
    public:
        TVoidArrayConverter(bool keepControl = true) { fKeepControl = keepControl; }

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
        virtual bool ToMemory(PyObject* value, void* address);

    protected:
        virtual bool GetAddressSpecialCase(PyObject* pyobject, void*& address);
        bool KeepControl() { return fKeepControl; }

    private:
        bool fKeepControl;
    };

    class TCppObjectConverter : public TVoidArrayConverter {
    public:
        TCppObjectConverter(Cppyy::TCppType_t klass, bool keepControl = false) :
            TVoidArrayConverter(keepControl), fClass(klass) {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
        virtual bool ToMemory(PyObject* value, void* address);
        
    protected:
        Cppyy::TCppType_t fClass;
    };

    class TStrictCppObjectConverter : public TCppObjectConverter {
    public:
        using TCppObjectConverter::TCppObjectConverter;

    protected:
        virtual bool GetAddressSpecialCase(PyObject*, void*&) { return false; }
    };

// create converter from fully qualified type
    TConverter* CreateConverter(const std::string& fullType, Long_t size = -1);

} // namespace CPyCppyy

#endif // !CPYCPPYY_CONVERTERS_H
