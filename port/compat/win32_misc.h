// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Win32 user-interface odds and ends used by the engine's error paths (port/compat/win32_misc.cpp).
 * MessageBox prints to stderr and answers as if the user chose the non-fatal option (Ignore / OK / No),
 * so a failed assert is logged and the game keeps going. Set SHAR_ASSERT_ABORT=1 to abort instead
 * (useful under a debugger). */
#ifndef SHAR_COMPAT_WIN32_MISC_H
#define SHAR_COMPAT_WIN32_MISC_H

#ifdef __cplusplus
extern "C" {
#endif

#define MB_OK               0x00000000u
#define MB_OKCANCEL         0x00000001u
#define MB_ABORTRETRYIGNORE 0x00000002u
#define MB_YESNOCANCEL      0x00000003u
#define MB_YESNO            0x00000004u
#define MB_RETRYCANCEL      0x00000005u
#define MB_ICONHAND         0x00000010u
#define MB_ICONERROR        MB_ICONHAND
#define MB_ICONSTOP         MB_ICONHAND
#define MB_ICONQUESTION     0x00000020u
#define MB_ICONEXCLAMATION  0x00000030u
#define MB_ICONWARNING      MB_ICONEXCLAMATION
#define MB_ICONINFORMATION  0x00000040u
#define MB_SETFOREGROUND    0x00010000u
#define MB_TOPMOST          0x00040000u
#define MB_TASKMODAL        0x00002000u
#define MB_SYSTEMMODAL      0x00001000u

#define IDOK     1
#define IDCANCEL 2
#define IDABORT  3
#define IDRETRY  4
#define IDIGNORE 5
#define IDYES    6
#define IDNO     7

int  MessageBox(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
#define MessageBoxA MessageBox
void OutputDebugString(LPCSTR text);
/* debug-only pointer validation: only NULL is reported as bad */
BOOL IsBadReadPtr(const void* p, UINT_PTR size);
BOOL IsBadWritePtr(void* p, UINT_PTR size);
#define OutputDebugStringA OutputDebugString

#ifdef __cplusplus
}
#endif

#endif /* SHAR_COMPAT_WIN32_MISC_H */
