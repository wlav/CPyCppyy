// Bindings
#include "CPyCppyy.h"
#include "TCallContext.h"


//- data _____________________________________________________________________
namespace CPyCppyy {

   TCallContext::ECallFlags TCallContext::sMemoryPolicy = TCallContext::kUseHeuristics;
// this is just a data holder for linking; actual value is set in CPyCppyyModule.cxx
   TCallContext::ECallFlags TCallContext::sSignalPolicy = TCallContext::kSafe;

} // namespace CPyCppyy


//-----------------------------------------------------------------------------
Bool_t CPyCppyy::TCallContext::SetMemoryPolicy( ECallFlags e )
{
// Set the global memory policy, which affects object ownership when objects
// are passed as function arguments.
   if ( kUseHeuristics == e || e == kUseStrict ) {
      sMemoryPolicy = e;
      return kTRUE;
   }
   return kFALSE;
}

//-----------------------------------------------------------------------------
Bool_t CPyCppyy::TCallContext::SetSignalPolicy( ECallFlags e )
{
// Set the global signal policy, which determines whether a jmp address
// should be saved to return to after a C++ segfault.
   if ( kFast == e || e == kSafe ) {
      sSignalPolicy = e;
      return kTRUE;
   }
   return kFALSE;
}

