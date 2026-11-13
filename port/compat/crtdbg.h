// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Linux stand-in for MSVC's <crtdbg.h> (debug heap): the macros used by debug builds, as no-ops.
   _ASSERT/_ASSERTE map to assert(). */
#ifndef SHAR_COMPAT_CRTDBG_H
#define SHAR_COMPAT_CRTDBG_H
#include <assert.h>
#ifndef _ASSERT
#define _ASSERT(expr)  assert(expr)
#endif
#ifndef _ASSERTE
#define _ASSERTE(expr) assert(expr)
#endif
#define _NORMAL_BLOCK 1
#define _CLIENT_BLOCK 4
#define _CRTDBG_ALLOC_MEM_DF        0x01
#define _CRTDBG_DELAY_FREE_MEM_DF   0x02
#define _CRTDBG_CHECK_ALWAYS_DF     0x04
#define _CRTDBG_LEAK_CHECK_DF       0x20
#define _CRTDBG_REPORT_FLAG         (-1)
typedef struct _CrtMemState { int unused; } _CrtMemState;
#define _CrtSetDbgFlag(flag)        (0)
#define _CrtSetBreakAlloc(n)        (0)
#define _CrtCheckMemory()           (1)
#define _CrtDumpMemoryLeaks()       (0)
#define _CrtMemCheckpoint(state)    ((void)(state))
#define _CrtMemDumpAllObjectsSince(state) ((void)(state))
#include <stdlib.h>
#define _malloc_dbg(size, type, file, line)            malloc(size)
#define _realloc_dbg(p, size, type, file, line)        realloc((p), (size))
#define _free_dbg(p, type)                             free(p)
#endif
