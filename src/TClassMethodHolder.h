#ifndef CPYCPPYY_TCLASSMETHODHOLDER_H
#define CPYCPPYY_TCLASSMETHODHOLDER_H

// Bindings
#include "TMethodHolder.h"


namespace CPyCppyy {

   class TClassMethodHolder : public TMethodHolder {
   public:
      using TMethodHolder::TMethodHolder;

      virtual PyCallable* Clone() { return new TClassMethodHolder( *this ); }
      virtual PyObject* Call(
         ObjectProxy*&, PyObject* args, PyObject* kwds, TCallContext* ctxt = 0 );
   };

} // namespace CPyCppyy

#endif // !CPYCPPYY_TCLASSMETHODHOLDER_H
