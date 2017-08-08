#ifndef CPYCPPYY_OBJECTPROXY_H
#define CPYCPPYY_OBJECTPROXY_H

//////////////////////////////////////////////////////////////////////////////
//                                                                          //
// CpyCppyy::ObjectProxy                                                    //
//                                                                          //
// Python-side proxy, encapsulaties a C++ object.                           //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////


// Bindings
#include "CPyCppyyType.h"
#include "Cppyy.h"
#include "TCallContext.h"


// TODO: have an ObjectProxy derived or alternative type for smart pointers

namespace CPyCppyy {

    class ObjectProxy {
    public:
        enum EFlags {
            kNone        = 0x0,
            kIsOwner     = 0x0001,
            kIsReference = 0x0002,
            kIsRValue    = 0x0004,
            kIsValue     = 0x0008,
            kIsSmartPtr  = 0x0010 };

    public:
        void Set(void* address, EFlags flags = kNone)
        {
        // Initialize the proxy with the pointer value 'address.'
            fObject = address;
            fFlags  = flags;
        }

        void SetSmartPtr(void* address, Cppyy::TCppType_t ptrType)
        {
            fFlags |= kIsSmartPtr;
            fSmartPtr = address;
            fSmartPtrType = ptrType;
        }

        void* GetObject() const
        {
        // Retrieve a pointer to the held C++ object.

        // We get the raw pointer from the smart pointer each time, in case
        // it has changed or has been freed.
            if (fFlags & kIsSmartPtr) {
            // TODO: this is icky and slow
                std::vector<Cppyy::TCppIndex_t> methods =
                    Cppyy::GetMethodIndicesFromName(fSmartPtrType, "operator->");
                std::vector<TParameter> args;
                return Cppyy::CallR(Cppyy::GetMethod(fSmartPtrType, methods[0]), fSmartPtr, &args);
            }

            if (fObject && (fFlags & kIsReference))
                return *(reinterpret_cast<void**>(const_cast<void*>(fObject)));
            else
                return const_cast<void*>(fObject);          // may be null
        }

        Cppyy::TCppType_t ObjectIsA() const
        {
        // Retrieve a pointer to the C++ type; may return NULL.
            return ((CPyCppyyClass*)Py_TYPE(this))->fCppType;
        }

        void HoldOn() { fFlags |= kIsOwner; }
        void Release() { fFlags &= ~kIsOwner; }

    public:              // public, as the python C-API works with C structs
        PyObject_HEAD
        void*     fObject;
        int       fFlags;
        void*     fSmartPtr;
        Cppyy::TCppType_t fSmartPtrType;

    private:
        ObjectProxy() = delete;
    };


//- object proxy type and type verification ----------------------------------
    extern PyTypeObject ObjectProxy_Type;

    template<typename T>
    inline bool ObjectProxy_Check(T* object)
    {
        return object && PyObject_TypeCheck(object, &ObjectProxy_Type);
    }

    template<typename T>
    inline bool ObjectProxy_CheckExact(T* object)
    {
        return object && Py_TYPE(object) == &ObjectProxy_Type;
    }


//- helper for memory regulation (no PyTypeObject equiv. member in p2.2) -----
    void op_dealloc_nofree(ObjectProxy*);

} // namespace CPyCppyy

#endif // !CPYCPPYY_OBJECTPROXY_H
