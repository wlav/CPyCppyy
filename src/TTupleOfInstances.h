#ifndef CPYCPPYY_TTUPLEOFINSTANCES_H
#define CPYCPPYY_TTUPLEOFINSTANCES_H

namespace CPyCppyy {

/** Representation of C-style array of instances
      @author  WLAV
      @date    02/10/2014
      @version 1.0
 */

//- custom tuple type that can pass through C-style arrays -------------------
   extern PyTypeObject TTupleOfInstances_Type;

   template< typename T >
   inline Bool_t TTupleOfInstances_Check( T* object )
   {
      return object && PyObject_TypeCheck( object, &TTupleOfInstances_Type );
   }

   template< typename T >
   inline Bool_t TTupleOfInstances_CheckExact( T* object )
   {
      return object && Py_TYPE(object) == &TTupleOfInstances_Type;
   }

   PyObject* TTupleOfInstances_New(
      Cppyy::TCppObject_t address, Cppyy::TCppType_t klass, Py_ssize_t nelems );

} // namespace CPyCppyy

#endif // !CPYCPPYY_TTUPLEOFINSTANCES_H
