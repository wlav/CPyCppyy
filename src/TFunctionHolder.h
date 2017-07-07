#ifndef CPYCPPYY_TFUNCTIONHOLDER_H
#define CPYCPPYY_TFUNCTIONHOLDER_H

// Bindings
#include "TMethodHolder.h"


namespace CPyCppyy {

   class TFunctionHolder : public TMethodHolder {
   public:
      using TMethodHolder::TMethodHolder;

      virtual PyCallable* Clone() { return new TFunctionHolder( *this ); }

      virtual PyObject* PreProcessArgs( ObjectProxy*& self, PyObject* args, PyObject* kwds );
      virtual PyObject* Call(
         ObjectProxy*&, PyObject* args, PyObject* kwds, TCallContext* ctx = 0 );
   };

} // namespace CPyCppyy

#endif // !CPYCPPYY_TFUNCTIONHOLDER_H
