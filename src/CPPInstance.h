#ifndef CPYCPPYY_CPPINSTANCE_H
#define CPYCPPYY_CPPINSTANCE_H

//////////////////////////////////////////////////////////////////////////////
//                                                                          //
// CpyCppyy::CPPInstance                                                    //
//                                                                          //
// Python-side proxy, encapsulaties a C++ object.                           //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////


// Bindings
#include "CPPScope.h"
#include "Cppyy.h"
#include "CallContext.h"     // for Parameter

// Standard
#include <utility>
#include <vector>


namespace CPyCppyy {

typedef std::vector<std::pair<ptrdiff_t, PyObject*>> CI_DatamemberCache_t;

class CPPInstance {
public:
    enum EFlags {
        kNone        = 0x0,
        kDefault     = 0x0001,
        kIsExtended  = 0x0002,
        kIsOwner     = 0x0004,
        kIsReference = 0x0008,
        kIsRValue    = 0x0010,
        kIsValue     = 0x0020,
        kIsSmartPtr  = 0x0040,
        kIsPtrPtr    = 0x0080 };

public:                 // public, as the python C-API works with C structs
    PyObject_HEAD
    void*     fObject;
    int       fFlags;

public:
// construction (never done directly)
    CPPInstance() = delete;

    void Set(void* address, EFlags flags = kDefault);
    CPPInstance* Copy(void* cppinst);

// access to C++ pointer and type
    void* GetObject() const;
    void*& GetObjectRaw() { return (fFlags & kIsExtended) ? *(void**) fObject : fObject; }
    Cppyy::TCppType_t ObjectIsA() const;

// memory management: ownership of the underlying C++ object
    void PythonOwns();
    void CppOwns();

// smart pointer management
    void SetSmartPtr(Cppyy::TCppType_t ptrtype, Cppyy::TCppMethod_t deref);
    Cppyy::TCppType_t GetSmartType();

// data member cache
    CI_DatamemberCache_t& GetDatamemberCache();

// cross-inheritence dispatch
    void SetDispatchPtr(void*);

private:
    void* GetExtendedObject() const;
    void CreateExtension();
};


//- public methods -----------------------------------------------------------
inline void CPPInstance::Set(void* address, EFlags flags)
{
// Initialize the proxy with the pointer value 'address.'
    if (flags != kDefault) fFlags = flags;
    GetObjectRaw() = address;
}

//----------------------------------------------------------------------------
inline void* CPPInstance::GetObject() const
{
// Retrieve a pointer to the held C++ object.
    if (!(fFlags & kIsExtended)) {
        if (fObject && (fFlags & kIsReference))
            return *(reinterpret_cast<void**>(fObject));
        else
            return fObject;   // may be null
    } else
        return GetExtendedObject();
}

//----------------------------------------------------------------------------
inline Cppyy::TCppType_t CPPInstance::ObjectIsA() const
{
// Retrieve a pointer to the C++ type; may return nullptr.
    return ((CPPClass*)Py_TYPE(this))->fCppType;
}


//- object proxy type and type verification ----------------------------------
CPYCPPYY_IMPORT PyTypeObject CPPInstance_Type;

template<typename T>
inline bool CPPInstance_Check(T* object)
{
    return object && (PyObject*)object != Py_None && PyObject_TypeCheck(object, &CPPInstance_Type);
}

template<typename T>
inline bool CPPInstance_CheckExact(T* object)
{
    return object && Py_TYPE(object) == &CPPInstance_Type;
}


//- helper for memory regulation (no PyTypeObject equiv. member in p2.2) -----
void op_dealloc_nofree(CPPInstance*);

} // namespace CPyCppyy

#endif // !CPYCPPYY_CPPINSTANCE_H
