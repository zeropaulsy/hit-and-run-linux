// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Implementation of win32_misc.h. See the header for the MessageBox policy.

#include "windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int MessageBox(HWND, LPCSTR text, LPCSTR caption, UINT type)
{
    fprintf(stderr, "[MessageBox] %s: %s\n", caption ? caption : "", text ? text : "");
    const char* abortEnv = getenv("SHAR_ASSERT_ABORT");
    bool abortOnError = abortEnv && strcmp(abortEnv, "0") != 0;
    switch(type & 0xF)
    {
    case MB_ABORTRETRYIGNORE: return abortOnError ? IDABORT : IDIGNORE;
    case MB_OKCANCEL:         return abortOnError ? IDCANCEL : IDOK;
    case MB_YESNO:            return IDNO;
    case MB_YESNOCANCEL:      return abortOnError ? IDCANCEL : IDNO;
    case MB_RETRYCANCEL:      return IDCANCEL;
    default:                  return IDOK;
    }
}

extern "C" void OutputDebugString(LPCSTR text)
{
    if(text) fputs(text, stderr);
}

extern "C" BOOL IsBadReadPtr(const void* p, UINT_PTR)  { return p == NULL; }
extern "C" BOOL IsBadWritePtr(void* p, UINT_PTR)       { return p == NULL; }
