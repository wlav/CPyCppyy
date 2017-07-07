#ifndef CPYCPPYY_TPYCLASSGENERATOR
#define CPYCPPYY_TPYCLASSGENERATOR

// TODO: not sure if any of this still makes sense ...
#if 0

// ROOT
#include "TClassGenerator.h"


class TPyClassGenerator : public TClassGenerator {
public:
   virtual TClass* GetClass( const char* name, Bool_t load );
   virtual TClass* GetClass( const std::type_info& typeinfo, Bool_t load );
   virtual TClass* GetClass( const char* name, Bool_t load, Bool_t silent );
   virtual TClass* GetClass( const std::type_info& typeinfo, Bool_t load, Bool_t silent );
};

#endif

#endif // !CPYCPPYY_TPYCLASSGENERATOR
