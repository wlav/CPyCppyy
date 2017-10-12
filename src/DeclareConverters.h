#ifndef CPYCPPYY_DECLARECONVERTERS_H
#define CPYCPPYY_DECLARECONVERTERS_H

// Bindings
#include "Converters.h"

// Standard
#include <limits.h>
#include <string>
#include <map>


namespace CPyCppyy {

namespace {

#define CPYCPPYY_DECLARE_BASIC_CONVERTER(name)                                \
    class T##name##Converter : public TConverter {                            \
    public:                                                                   \
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr); \
        virtual PyObject* FromMemory(void*);                                  \
        virtual bool ToMemory(PyObject*, void*);                              \
    };                                                                        \
                                                                              \
    class TConst##name##RefConverter : public TConverter {                    \
    public:                                                                   \
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr); \
    }


#define CPYCPPYY_DECLARE_BASIC_CONVERTER2(name, base)                         \
    class T##name##Converter : public T##base##Converter {                    \
    public:                                                                   \
        virtual PyObject* FromMemory(void*);                                  \
        virtual bool ToMemory(PyObject*, void*);                              \
    };                                                                        \
                                                                              \
    class TConst##name##RefConverter : public TConverter {                    \
    public:                                                                   \
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr); \
    }

#define CPYCPPYY_DECLARE_REF_CONVERTER(name)                                  \
    class T##name##RefConverter : public TConverter {                         \
    public:                                                                   \
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr); \
    };

#define CPYCPPYY_DECLARE_ARRAY_CONVERTER(name)                                \
    class T##name##Converter : public TConverter {                            \
    public:                                                                   \
        T##name##Converter(Py_ssize_t size = -1) { fSize = size; }            \
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr); \
        virtual PyObject* FromMemory(void*);                                  \
        virtual bool ToMemory(PyObject*, void*);                              \
    private:                                                                  \
        Py_ssize_t fSize;                                                     \
    };                                                                        \
                                                                              \
    class T##name##RefConverter : public T##name##Converter {                 \
    public:                                                                   \
        using T##name##Converter::T##name##Converter;                         \
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr); \
    }

// converters for built-ins
    CPYCPPYY_DECLARE_BASIC_CONVERTER(Long);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(Bool);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(Char);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(UChar);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(Short);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(UShort);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(Int);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(ULong);
    CPYCPPYY_DECLARE_BASIC_CONVERTER2(UInt, ULong);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(LongLong);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(ULongLong);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(Double);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(Float);
    CPYCPPYY_DECLARE_BASIC_CONVERTER(LongDouble);

    CPYCPPYY_DECLARE_REF_CONVERTER(Int);
    CPYCPPYY_DECLARE_REF_CONVERTER(Long);
    CPYCPPYY_DECLARE_REF_CONVERTER(Double);

    class TVoidConverter : public TConverter {
    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
    };

    class TCStringConverter : public TConverter {
    public:
        TCStringConverter(UInt_t maxSize = UINT_MAX) : fMaxSize(maxSize) {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
        virtual bool ToMemory(PyObject* value, void* address);

    protected:
        std::string fBuffer;
        UInt_t fMaxSize;
    };

    class TNonConstCStringConverter : public TCStringConverter {
    public:
        TNonConstCStringConverter(UInt_t maxSize = UINT_MAX) : TCStringConverter(maxSize) {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
    };

    class TNonConstUCStringConverter : public TNonConstCStringConverter {
    public:
        TNonConstUCStringConverter(UInt_t maxSize = UINT_MAX) : TNonConstCStringConverter(maxSize) {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
    };

// pointer/array conversions
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(BoolArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(ShortArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(UShortArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(IntArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(UIntArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(LongArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(ULongArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(FloatArray);
    CPYCPPYY_DECLARE_ARRAY_CONVERTER(DoubleArray);

    class TLongLongArrayConverter : public TVoidArrayConverter {
    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
    };

// converters for special cases
    class TValueCppObjectConverter : public TStrictCppObjectConverter {
    public:
        using TStrictCppObjectConverter::TStrictCppObjectConverter;
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
    };

    class TRefCppObjectConverter : public TConverter  {
    public:
        TRefCppObjectConverter(Cppyy::TCppType_t klass) : fClass(klass) {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);

    protected:
        Cppyy::TCppType_t fClass;
    };

    class TMoveCppObjectConverter : public TRefCppObjectConverter  {
    public:
        using TRefCppObjectConverter::TRefCppObjectConverter;
        virtual bool SetArg( PyObject*, TParameter&, TCallContext* = nullptr);
    };

    template <bool ISREFERENCE>
    class TCppObjectPtrConverter : public TCppObjectConverter {
    public:
        using TCppObjectConverter::TCppObjectConverter;

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
        virtual bool ToMemory(PyObject* value, void* address);
    };

    extern template class TCppObjectPtrConverter<true>;
    extern template class TCppObjectPtrConverter<false>;

    class TCppObjectArrayConverter : public TCppObjectConverter {
    public:
        TCppObjectArrayConverter(Cppyy::TCppType_t klass, size_t size, bool keepControl = false) :
            TCppObjectConverter(klass, keepControl), m_size(size) {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
        virtual bool ToMemory(PyObject* value, void* address);

    protected:
        size_t m_size;
    };

// CLING WORKAROUND -- classes for STL iterators are completely undefined in that
// they come in a bazillion different guises, so just do whatever
    class TSTLIteratorConverter : public TConverter {
    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
    };
// -- END CLING WORKAROUND

    class TVoidPtrRefConverter : public TConverter {
    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
    };

    class TVoidPtrPtrConverter : public TConverter {
    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
    };

    CPYCPPYY_DECLARE_BASIC_CONVERTER(PyObject);

#define CPYCPPYY_DECLARE_STRING_CONVERTER(name, strtype)                      \
    class T##name##Converter : public TCppObjectConverter {                   \
    public:                                                                   \
        T##name##Converter(bool keepControl = true);                          \
    public:                                                                   \
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr); \
        virtual PyObject* FromMemory(void* address);                          \
        virtual bool ToMemory(PyObject* value, void* address);                \
    private:                                                                  \
        strtype fBuffer;                                                      \
    }

    CPYCPPYY_DECLARE_STRING_CONVERTER(STLString, std::string);
#if __cplusplus > 201402L
    CPYCPPYY_DECLARE_STRING_CONVERTER(STLStringView, std::string_view);
#endif

    class TNotImplementedConverter : public TConverter {
    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
    };

// smart pointer converter
    class TSmartPtrCppObjectConverter : public TConverter  {
    public:
        TSmartPtrCppObjectConverter(Cppyy::TCppType_t klass,
                                    Cppyy::TCppType_t rawPtrType,
                                    Cppyy::TCppMethod_t deref,
                                    bool keepControl = false,
                                    bool handlePtr = false)
            : fClass(klass), fRawPtrType(rawPtrType), fDereferencer(deref),
              fKeepControl(keepControl), fHandlePtr(handlePtr) {}

    public:
        virtual bool SetArg(PyObject*, TParameter&, TCallContext* = nullptr);
        virtual PyObject* FromMemory(void* address);
        //virtual bool ToMemory( PyObject* value, void* address );

    protected:
        virtual bool GetAddressSpecialCase(PyObject*, void*&) { return false; }

        Cppyy::TCppType_t   fClass;
        Cppyy::TCppType_t   fRawPtrType;
        Cppyy::TCppMethod_t fDereferencer;
        bool                fKeepControl;
        bool                fHandlePtr;
    };

} // unnamed namespace

} // namespace CPyCppyy

#endif // !CPYCPPYY_DECLARECONVERTERS_H
