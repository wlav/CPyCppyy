#ifndef CPYCPPYY_CPYCPPYYTYPE_H
#define CPYCPPYY_CPYCPPYYTYPE_H

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION == 2

// In p2.2, PyHeapTypeObject is not yet part of the interface
#include "structmember.h"

typedef struct {
    PyTypeObject type;
    PyNumberMethods as_number;
    PySequenceMethods as_sequence;
    PyMappingMethods as_mapping;
    PyBufferProcs as_buffer;
    PyObject *name, *slots;
    PyMemberDef members[1];
} PyHeapTypeObject;

#endif

// Standard
#include <map>


namespace CPyCppyy {

/** Type object to hold class reference (this is only semantically a presentation
    of CPyCppyyType instances, not in a C++ sense)
      @author  WLAV
      @date    07/06/2017
      @version 2.0
 */

    typedef std::map<Cppyy::TCppObject_t, PyObject*> CppToPyMap_t;
    typedef std::map<PyObject*, CppToPyMap_t::iterator> WeakRefMap_t;

    class CPyCppyyClass {
    public:
        PyHeapTypeObject  fType;
        Cppyy::TCppType_t fCppType;
        CppToPyMap_t*     fCppObjects;
        WeakRefMap_t*     fWeakRefs;

    private:
        CPyCppyyClass() = delete;
    };

//- metatype type and type verification --------------------------------------
    extern PyTypeObject CPyCppyyType_Type;

    template<typename T>
    inline bool CPyCppyyType_Check(T* object)
    {
        return object && PyObject_TypeCheck(object, &CPyCppyyType_Type);
    }

    template<typename T>
    inline bool CPyCppyyType_CheckExact(T* object)
    {
        return object && Py_TYPE(object) == &CPyCppyyType_Type;
    }

} // namespace CPyCppyy

#endif // !CPYCPPYY_CPYCPPYYTYPE_H
