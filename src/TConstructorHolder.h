#ifndef CPYCPPYY_TCONSTRUCTORHOLDER_H
#define CPYCPPYY_TCONSTRUCTORHOLDER_H

// Bindings
#include "TMethodHolder.h"


namespace CPyCppyy {

   class TConstructorHolder : public TMethodHolder {
   public:
      using TMethodHolder::TMethodHolder;

   public:
      virtual PyObject* GetDocString();
      virtual PyCallable* Clone() { return new TConstructorHolder( *this ); }

   public:
      virtual PyObject* Call(
         ObjectProxy*& self, PyObject* args, PyObject* kwds, TCallContext* ctxt = 0 );

   protected:
      virtual Bool_t InitExecutor_( TExecutor*&, TCallContext* ctxt = 0 );
   };

} // namespace CPyCppyy

#endif // !CPYCPPYY_TCONSTRUCTORHOLDER_H
