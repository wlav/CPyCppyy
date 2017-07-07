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


namespace CPyCppyy {

/** Type object to hold class reference (this is only semantically a presentation
    of CPyCppyyType instances, not in a C++ sense)
      @author  WLAV
      @date    07/06/2017
      @version 1.0
 */

   class CPyCppyyClass {
   public:
      PyHeapTypeObject fType;
      Cppyy::TCppType_t fCppType;

   private:
      CPyCppyyClass() {}
   };

//- metatype type and type verification --------------------------------------
   extern PyTypeObject CPyCppyyType_Type;

   template< typename T >
   inline Bool_t CPyCppyyType_Check( T* object )
   {
      return object && PyObject_TypeCheck( object, &CPyCppyyType_Type );
   }

   template< typename T >
   inline Bool_t CPyCppyyType_CheckExact( T* object )
   {
      return object && Py_TYPE(object) == &CPyCppyyType_Type;
   }

} // namespace CPyCppyy

#endif // !CPYCPPYY_CPYCPPYYTYPE_H
