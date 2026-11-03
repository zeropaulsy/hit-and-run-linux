/* Force-included into every translation unit of the Linux build (see port/gen_cmake.py).
 * Maps MSVC compiler built-ins and CRT names onto GCC/POSIX equivalents so the original
 * source compiles unchanged. Must stay valid C and C++98. */
#ifndef SHAR_MSVC_COMPAT_H
#define SHAR_MSVC_COMPAT_H

#if defined(__GNUC__) && !defined(_MSC_VER)

#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- compiler keywords ---- */
#define __int8  char
#define __int16 short
#define __int32 int
#define __int64 long long
#define _int64  long long
#define __forceinline inline
#define __cdecl
#define __stdcall
#define __fastcall
#define __declspec(x)
#define _inline inline

/* Symbols that must stay private to the executable. The game replaces the global operator new/delete;
   on Windows that only affects the .exe, but an ELF executable's definition would also replace them
   for every shared library (GPU drivers included). Hidden visibility restores the Windows behaviour. */
#ifdef __APPLE__
#define SHAR_LOCAL   /* libc++ declares operator new with default visibility; isolation differs on Mach-O */
#else
#define SHAR_LOCAL __attribute__((visibility("hidden")))
#endif

/* ---- CRT names ---- */
#define _isnan(x)   __builtin_isnan(x)
#define _finite(x)  __builtin_isfinite(x)
#define stricmp     strcasecmp
#define _stricmp    strcasecmp
#define strnicmp    strncasecmp
#define _strnicmp   strncasecmp
#define strcmpi     strcasecmp
#define _strcmpi    strcasecmp
#define _snprintf   snprintf
#define _vsnprintf  vsnprintf
#define _strdup     strdup

#include <ctype.h>
static inline char* shar_strupr(char* s) { char* p = s; for(; p && *p; ++p) *p = (char)toupper((unsigned char)*p); return s; }
static inline char* shar_strlwr(char* s) { char* p = s; for(; p && *p; ++p) *p = (char)tolower((unsigned char)*p); return s; }
#define strupr  shar_strupr
#define _strupr shar_strupr
#define strlwr  shar_strlwr
#define _strlwr shar_strlwr

#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif

/* A pointer used only as a 32-bit identity (map keys, "who used this" ids). Exact on 32-bit; on 64-bit the high half
   is folded in, which is unique unless two live objects share a fold (64-bit port: search for this to revisit). */
#include <stdint.h>
static inline unsigned int shar_PointerKey(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    return (unsigned int)(v ^ (v >> 16 >> 16));
}
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#endif /* __GNUC__ */
#endif /* SHAR_MSVC_COMPAT_H */
