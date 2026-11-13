// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Win32 thread, synchronisation and timer API subset implemented on POSIX (port/compat/win32_threads.cpp).
 * Only what radcore uses: critical sections, semaphores, threads (incl. CREATE_SUSPENDED start),
 * waits with timeouts, Sleep, and the performance counter.
 * Limits: SuspendThread on a RUNNING thread is unsupported (logged; the game never does it), and thread
 * priorities are recorded but not applied (raising them needs privileges on Linux). */
#ifndef SHAR_COMPAT_WIN32_THREADS_H
#define SHAR_COMPAT_WIN32_THREADS_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- critical sections: recursive mutexes, like Win32 ---- */
typedef struct _CRITICAL_SECTION { pthread_mutex_t mutex; } CRITICAL_SECTION, *LPCRITICAL_SECTION;
void InitializeCriticalSection(LPCRITICAL_SECTION cs);
void DeleteCriticalSection(LPCRITICAL_SECTION cs);
void EnterCriticalSection(LPCRITICAL_SECTION cs);
void LeaveCriticalSection(LPCRITICAL_SECTION cs);
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs);

/* ---- kernel objects ---- */
typedef void* LPSECURITY_ATTRIBUTES;
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);

#define INFINITE        0xFFFFFFFFu
#define WAIT_OBJECT_0   0x00000000u
#define WAIT_TIMEOUT    0x00000102u
#define WAIT_FAILED     0xFFFFFFFFu
#define CREATE_SUSPENDED 0x00000004u

#define THREAD_PRIORITY_IDLE          (-15)
#define THREAD_PRIORITY_LOWEST        (-2)
#define THREAD_PRIORITY_BELOW_NORMAL  (-1)
#define THREAD_PRIORITY_NORMAL        0
#define THREAD_PRIORITY_ABOVE_NORMAL  1
#define THREAD_PRIORITY_HIGHEST       2
#define THREAD_PRIORITY_TIME_CRITICAL 15

HANDLE CreateSemaphore(LPSECURITY_ATTRIBUTES attr, LONG initialCount, LONG maximumCount, LPCSTR name);
BOOL   ReleaseSemaphore(HANDLE semaphore, LONG releaseCount, LONG* previousCount);

HANDLE CreateThread(LPSECURITY_ATTRIBUTES attr, size_t stackSize, LPTHREAD_START_ROUTINE start,
                    LPVOID param, DWORD flags, DWORD* threadId);
DWORD  ResumeThread(HANDLE thread);
DWORD  SuspendThread(HANDLE thread);
BOOL   TerminateThread(HANDLE thread, DWORD exitCode);
void   ExitThread(DWORD exitCode);
BOOL   SetThreadPriority(HANDLE thread, int priority);
int    GetThreadPriority(HANDLE thread);
HANDLE GetCurrentThread(void);
DWORD  GetCurrentThreadId(void);

#define STILL_ACTIVE 259u
BOOL   GetExitCodeThread(HANDLE thread, DWORD* exitCode);

DWORD  WaitForSingleObject(HANDLE object, DWORD milliseconds);
BOOL   CloseHandle(HANDLE object);

/* ---- fibers (cooperative coroutines), on ucontext ---- */
typedef void (WINAPI *LPFIBER_START_ROUTINE)(LPVOID);
LPVOID ConvertThreadToFiber(LPVOID param);
LPVOID CreateFiber(size_t stackSize, LPFIBER_START_ROUTINE start, LPVOID param);
void   SwitchToFiber(LPVOID fiber);
void   DeleteFiber(LPVOID fiber);

/* ---- Linux extension: called at the start of every thread created by CreateThread (before its start
   routine). The OpenGL renderer uses it to give worker threads a shared GL context, since Direct3D 8
   allowed any thread to use the device. */
void shar_SetThreadStartHook(void (*hook)(void));
/* run the hook on the calling thread (for threads created before the hook was installed) */
void shar_RunThreadStartHook(void);

/* ---- time ---- */
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;
void   GetLocalTime(LPSYSTEMTIME time);

void   Sleep(DWORD milliseconds);
BOOL   QueryPerformanceCounter(LARGE_INTEGER* count);
BOOL   QueryPerformanceFrequency(LARGE_INTEGER* frequency);
DWORD  GetTickCount(void);
DWORD  timeGetTime(void);

#ifdef __cplusplus
}
#endif

#endif /* SHAR_COMPAT_WIN32_THREADS_H */
