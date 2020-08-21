// Partial reproduction of ROOT's TException.h

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef CPYCPPYY_SIGNALTRYCATCH_H
#define CPYCPPYY_SIGNALTRYCATCH_H

#include <setjmp.h>
#include "CPyCppyy/CommonDefs.h"

#ifndef _WIN32
#define NEED_SIGJMP 1
#endif

namespace CppyyLegacy {
struct ExceptionContext_t {
#ifdef NEED_SIGJMP
    sigjmp_buf fBuf;
#else
    jmp_buf fBuf;
#endif
};
}

#ifdef NEED_SIGJMP
# define CLING_EXCEPTION_SETJMP(buf) sigsetjmp(buf,1)
#else
# define CLING_EXCEPTION_SETJMP(buf) setjmp(buf)
#endif

#define CLING_EXCEPTION_RETRY \
    { \
        static CppyyLegacy::ExceptionContext_t R__curr, *R__old = gException; \
        int R__code; \
        gException = &R__curr; \
        R__code = CLING_EXCEPTION_SETJMP(gException->fBuf); if (R__code) { }; {

#define CLING_EXCEPTION_TRY \
    { \
        static CppyyLegacy::ExceptionContext_t R__curr, *R__old = gException; \
        int R__code; \
        gException = &R__curr; \
        if ((R__code = CLING_EXCEPTION_SETJMP(gException->fBuf)) == 0) {

#define CLING_EXCEPTION_CATCH(n) \
            gException = R__old; \
        } else { \
            int n = R__code; \
            gException = R__old;

#define CLING_EXCEPTION_ENDTRY \
        } \
        gException = R__old; \
    }

CPYCPPYY_IMPORT CppyyLegacy::ExceptionContext_t *gException;

#endif
