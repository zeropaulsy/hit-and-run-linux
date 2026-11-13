// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Linux stand-in for <windows.h>: basic Win32 TYPES only, so files that include windows.h just for
 * types compile unchanged. Win32 FUNCTIONS are deliberately not declared: every compile or link error
 * from a missing one marks code that needs a real RAD_LINUX implementation. */
#ifndef SHAR_COMPAT_WINDOWS_H
#define SHAR_COMPAT_WINDOWS_H

#include <stdint.h>

#define WINAPI
#define FAR
#define NEAR
#define PASCAL
#define CALLBACK
#define APIENTRY
#define CONST const
#define VOID void

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef int                BOOL;
typedef unsigned char      BYTE;
typedef unsigned short     WORD;
typedef uint32_t           DWORD;
typedef uint32_t           UINT;
typedef int32_t            LONG;
typedef uint32_t           ULONG;
typedef int32_t            INT;
typedef char               CHAR;
typedef unsigned char      UCHAR;
typedef short              SHORT;
typedef unsigned short     USHORT;
typedef float              FLOAT;
typedef long long          LONGLONG;
typedef unsigned long long ULONGLONG;
typedef uintptr_t          UINT_PTR;
typedef intptr_t           INT_PTR;
typedef uintptr_t          WPARAM;
typedef intptr_t           LPARAM;
typedef intptr_t           LRESULT;
typedef long               HRESULT;

typedef void*              LPVOID;
typedef const void*        LPCVOID;
typedef char*              LPSTR;
typedef const char*        LPCSTR;
typedef DWORD*             LPDWORD;
typedef BYTE*              LPBYTE;
typedef BOOL*              LPBOOL;

typedef void*              HANDLE;
typedef HANDLE             HWND;
typedef HANDLE             HINSTANCE;
typedef HANDLE             HMODULE;
typedef HANDLE             HDC;
typedef HANDLE             HGLRC;
typedef HANDLE             HICON;
typedef HANDLE             HCURSOR;
typedef HANDLE             HMENU;

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#include <string.h>
#define ZeroMemory(dst, len)        memset((dst), 0, (len))
#define FillMemory(dst, len, fill)  memset((dst), (fill), (len))
#define CopyMemory(dst, src, len)   memcpy((dst), (src), (len))
#define MoveMemory(dst, src, len)   memmove((dst), (src), (len))

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct tagPOINT { LONG x; LONG y; } POINT;
typedef struct tagRECT { LONG left; LONG top; LONG right; LONG bottom; } RECT;

/* Implemented API families (POSIX): threads/sync/fibers/timers, and message boxes/debug output */
#include "win32_threads.h"
#include "win32_misc.h"

#endif /* SHAR_COMPAT_WINDOWS_H */
