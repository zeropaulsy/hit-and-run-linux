// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// POSIX implementation of the Win32 subset declared in win32_threads.h. See that header for the limits.

#include "windows.h"
#include "shar_crash.h"

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>

// ---------------------------------------------------------------- critical sections

extern "C" void InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);   // Win32 critical sections are re-entrant
    pthread_mutex_init(&cs->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

extern "C" void DeleteCriticalSection(LPCRITICAL_SECTION cs)   { pthread_mutex_destroy(&cs->mutex); }
extern "C" void EnterCriticalSection(LPCRITICAL_SECTION cs)    { pthread_mutex_lock(&cs->mutex); }
extern "C" void LeaveCriticalSection(LPCRITICAL_SECTION cs)    { pthread_mutex_unlock(&cs->mutex); }
extern "C" BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs) { return pthread_mutex_trylock(&cs->mutex) == 0; }

// ---------------------------------------------------------------- kernel objects

// Win32 kernel objects are allocated by the OS, never through the program's heap. The game replaces the
// global operator new with its own memory manager (and initialises it lazily, from inside the first
// allocation, which itself creates threads/fibers), so this layer must only ever use malloc/free.
template <class T> static T* Alloc()
{
    T* p = static_cast<T*>(calloc(1, sizeof(T)));
    if(!p) abort();
    return p;
}

namespace
{
    enum ObjectType { OBJ_SEMAPHORE = 0x53454d41, OBJ_THREAD = 0x54485244 };

    struct Object
    {
        int             type;
        pthread_mutex_t lock;
        pthread_cond_t  cond;
    };

    struct Semaphore : Object
    {
        LONG count;
        LONG maximum;
    };

    struct Thread : Object
    {
        pthread_t              handle;
        LPTHREAD_START_ROUTINE start;
        LPVOID                 param;
        DWORD                  id;
        DWORD                  exitCode;
        int                    priority;
        bool                   started;     // released from CREATE_SUSPENDED
        bool                   finished;
        bool                   closed;      // CloseHandle called
    };

    pthread_key_t s_currentThreadKey;
    pthread_once_t s_keyOnce = PTHREAD_ONCE_INIT;
    void MakeKey() { pthread_key_create(&s_currentThreadKey, NULL); }

    void InitObject(Object* o, int type)
    {
        o->type = type;
        pthread_mutex_init(&o->lock, NULL);
        pthread_cond_init(&o->cond, NULL);
    }

    void FreeObject(Object* o)
    {
        pthread_mutex_destroy(&o->lock);
        pthread_cond_destroy(&o->cond);
        free(o);                              // see Alloc: never the program's operator new
    }

    // absolute CLOCK_REALTIME deadline for pthread_cond_timedwait
    timespec Deadline(DWORD ms)
    {
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += ms / 1000;
        ts.tv_nsec += (long)(ms % 1000) * 1000000L;
        if(ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        return ts;
    }

    void FinishThread(Thread* t, DWORD code)
    {
        pthread_mutex_lock(&t->lock);
        t->exitCode = code;
        t->finished = true;
        bool closed = t->closed;
        pthread_cond_broadcast(&t->cond);
        pthread_mutex_unlock(&t->lock);
        if(closed) FreeObject(t);            // nobody holds the handle any more
    }

    void (*volatile s_threadStartHook)(void) = NULL;

    void* ThreadTrampoline(void* arg)
    {
        Thread* t = static_cast<Thread*>(arg);
        pthread_once(&s_keyOnce, MakeKey);
        pthread_setspecific(s_currentThreadKey, t);
        t->id = (DWORD)syscall(SYS_gettid);

        pthread_mutex_lock(&t->lock);
        while(!t->started)                    // CREATE_SUSPENDED: wait for ResumeThread
            pthread_cond_wait(&t->cond, &t->lock);
        pthread_mutex_unlock(&t->lock);

        shar_CrashThreadInit();
        if(s_threadStartHook) s_threadStartHook();
        DWORD code = t->start(t->param);
        shar_CrashThreadExit();
        FinishThread(t, code);
        return NULL;
    }

    Thread* AsThread(HANDLE h)
    {
        Object* o = static_cast<Object*>(h);
        return (o && o->type == OBJ_THREAD) ? static_cast<Thread*>(o) : NULL;
    }

    Semaphore* AsSemaphore(HANDLE h)
    {
        Object* o = static_cast<Object*>(h);
        return (o && o->type == OBJ_SEMAPHORE) ? static_cast<Semaphore*>(o) : NULL;
    }
}

extern "C" void shar_SetThreadStartHook(void (*hook)(void))
{
    s_threadStartHook = hook;
}

extern "C" void shar_RunThreadStartHook(void)
{
    if(s_threadStartHook) s_threadStartHook();
}

extern "C" HANDLE CreateSemaphore(LPSECURITY_ATTRIBUTES, LONG initialCount, LONG maximumCount, LPCSTR)
{
    Semaphore* s = Alloc<Semaphore>();
    InitObject(s, OBJ_SEMAPHORE);
    s->count = initialCount;
    s->maximum = maximumCount;
    return s;
}

extern "C" BOOL ReleaseSemaphore(HANDLE h, LONG releaseCount, LONG* previousCount)
{
    Semaphore* s = AsSemaphore(h);
    if(!s || releaseCount <= 0) return FALSE;
    pthread_mutex_lock(&s->lock);
    if(previousCount) *previousCount = s->count;
    if(s->count + releaseCount > s->maximum)
    {
        pthread_mutex_unlock(&s->lock);
        return FALSE;                         // Win32 fails without changing the count
    }
    s->count += releaseCount;
    pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->lock);
    return TRUE;
}

extern "C" HANDLE CreateThread(LPSECURITY_ATTRIBUTES, size_t stackSize, LPTHREAD_START_ROUTINE start,
                               LPVOID param, DWORD flags, DWORD* threadId)
{
    Thread* t = Alloc<Thread>();
    InitObject(t, OBJ_THREAD);
    t->start = start;
    t->param = param;
    t->id = 0;
    t->exitCode = 0;
    t->priority = THREAD_PRIORITY_NORMAL;
    t->started = !(flags & CREATE_SUSPENDED);
    t->finished = false;
    t->closed = false;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Win32's dwStackSize is only the initial COMMIT: every thread still reserves the default 1 MB, so
    // radcore asks for small sizes (e.g. 16 KB) that would overflow as real pthread stacks.
    const size_t kWin32DefaultReserve = 1024 * 1024;
    pthread_attr_setstacksize(&attr, stackSize > kWin32DefaultReserve ? stackSize : kWin32DefaultReserve);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);   // completion is tracked by Thread::finished
    int rc = pthread_create(&t->handle, &attr, ThreadTrampoline, t);
    pthread_attr_destroy(&attr);
    if(rc != 0)
    {
        FreeObject(t);
        return NULL;
    }
    if(threadId)
        *threadId = (DWORD)(uintptr_t)t;     // unique until closed; the real tid is set inside the thread
    return t;
}

extern "C" DWORD ResumeThread(HANDLE h)
{
    Thread* t = AsThread(h);
    if(!t) return (DWORD)-1;
    pthread_mutex_lock(&t->lock);
    DWORD previous = t->started ? 0 : 1;      // Win32 returns the previous suspend count
    t->started = true;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
    return previous;
}

extern "C" DWORD SuspendThread(HANDLE h)
{
    fprintf(stderr, "[win32 compat] SuspendThread on a running thread is not supported on Linux; ignored\n");
    return 0;
}

extern "C" BOOL TerminateThread(HANDLE h, DWORD exitCode)
{
    Thread* t = AsThread(h);
    if(!t) return FALSE;
    pthread_mutex_lock(&t->lock);
    bool running = t->started && !t->finished;
    pthread_mutex_unlock(&t->lock);
    if(running)
        pthread_cancel(t->handle);            // as abrupt as Win32's TerminateThread
    else
        ResumeThread(h);                      // let a never-started thread run to its gate check
    pthread_mutex_lock(&t->lock);
    t->exitCode = exitCode;
    t->finished = true;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
    return TRUE;
}

extern "C" void ExitThread(DWORD exitCode)
{
    pthread_once(&s_keyOnce, MakeKey);
    Thread* t = static_cast<Thread*>(pthread_getspecific(s_currentThreadKey));
    if(t) FinishThread(t, exitCode);
    pthread_exit(NULL);
}

extern "C" BOOL GetExitCodeThread(HANDLE h, DWORD* exitCode)
{
    Thread* t = AsThread(h);
    if(!t || !exitCode) return FALSE;
    pthread_mutex_lock(&t->lock);
    *exitCode = t->finished ? t->exitCode : STILL_ACTIVE;
    pthread_mutex_unlock(&t->lock);
    return TRUE;
}

extern "C" BOOL SetThreadPriority(HANDLE h, int priority)
{
    Thread* t = AsThread(h);
    if(t) t->priority = priority;             // recorded only; see header
    return TRUE;
}

extern "C" int GetThreadPriority(HANDLE h)
{
    Thread* t = AsThread(h);
    return t ? t->priority : THREAD_PRIORITY_NORMAL;
}

extern "C" HANDLE GetCurrentThread(void)
{
    pthread_once(&s_keyOnce, MakeKey);
    return pthread_getspecific(s_currentThreadKey);   // NULL for the main thread (a pseudo-handle on Win32)
}

extern "C" DWORD GetCurrentThreadId(void)
{
    // must match the id CreateThread returned, since radcore compares the two
    pthread_once(&s_keyOnce, MakeKey);
    Thread* t = static_cast<Thread*>(pthread_getspecific(s_currentThreadKey));
    return t ? (DWORD)(uintptr_t)t : (DWORD)syscall(SYS_gettid);
}

extern "C" DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
    Object* o = static_cast<Object*>(h);
    if(!o) return WAIT_FAILED;
    timespec deadline = Deadline(ms == INFINITE ? 0 : ms);
    DWORD result = WAIT_OBJECT_0;

    pthread_mutex_lock(&o->lock);
    if(o->type == OBJ_SEMAPHORE)
    {
        Semaphore* s = static_cast<Semaphore*>(o);
        while(s->count == 0 && result == WAIT_OBJECT_0)
        {
            int rc = (ms == INFINITE) ? pthread_cond_wait(&s->cond, &s->lock)
                                      : pthread_cond_timedwait(&s->cond, &s->lock, &deadline);
            if(rc == ETIMEDOUT) result = WAIT_TIMEOUT;
        }
        if(result == WAIT_OBJECT_0) s->count--;
    }
    else if(o->type == OBJ_THREAD)
    {
        Thread* t = static_cast<Thread*>(o);
        while(!t->finished && result == WAIT_OBJECT_0)
        {
            int rc = (ms == INFINITE) ? pthread_cond_wait(&t->cond, &t->lock)
                                      : pthread_cond_timedwait(&t->cond, &t->lock, &deadline);
            if(rc == ETIMEDOUT) result = WAIT_TIMEOUT;
        }
    }
    else
    {
        result = WAIT_FAILED;
    }
    pthread_mutex_unlock(&o->lock);
    return result;
}

extern "C" BOOL CloseHandle(HANDLE h)
{
    Object* o = static_cast<Object*>(h);
    if(!o) return FALSE;
    if(o->type == OBJ_THREAD)
    {
        Thread* t = static_cast<Thread*>(o);
        pthread_mutex_lock(&t->lock);
        t->closed = true;
        bool finished = t->finished;
        pthread_mutex_unlock(&t->lock);
        if(finished) FreeObject(t);           // else freed by the thread when it finishes
        return TRUE;
    }
    if(o->type == OBJ_SEMAPHORE)
    {
        FreeObject(o);
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------- time

extern "C" void Sleep(DWORD ms)
{
    if(ms == 0)
    {
        sched_yield();                        // Win32 Sleep(0): give up the rest of the time slice
        return;
    }
    timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while(nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

extern "C" BOOL QueryPerformanceCounter(LARGE_INTEGER* count)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    count->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}

extern "C" BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency)
{
    frequency->QuadPart = 1000000000LL;       // nanosecond ticks
    return TRUE;
}

extern "C" DWORD GetTickCount(void)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000);
}

extern "C" DWORD timeGetTime(void)
{
    return GetTickCount();
}

// ---------------------------------------------------------------- fibers

namespace
{
    struct Fiber
    {
        ucontext_t            context;
        LPFIBER_START_ROUTINE start;
        LPVOID                param;
        void*                 stack;          // NULL for a thread converted to a fiber
    };

    __thread Fiber* s_currentFiber = NULL;

    void FiberTrampoline(unsigned lo, unsigned hi)
    {
        // makecontext passes int arguments; on i386 a pointer fits in one, the split keeps it portable
        Fiber* f = (Fiber*)(uintptr_t)(((unsigned long long)hi << 32) | lo);
        f->start(f->param);
        fprintf(stderr, "[win32 compat] a fiber returned (Win32 fibers must not return); exiting thread\n");
        pthread_exit(NULL);
    }
}

extern "C" LPVOID ConvertThreadToFiber(LPVOID param)
{
    if(s_currentFiber) return s_currentFiber;
    Fiber* f = Alloc<Fiber>();
    f->start = NULL;
    f->param = param;
    f->stack = NULL;
    s_currentFiber = f;                       // its context is filled in by the first SwitchToFiber away
    return f;
}

extern "C" LPVOID CreateFiber(size_t stackSize, LPFIBER_START_ROUTINE start, LPVOID param)
{
    Fiber* f = Alloc<Fiber>();
    f->start = start;
    f->param = param;
    if(stackSize < 65536) stackSize = 65536;
    f->stack = malloc(stackSize);
    getcontext(&f->context);
    f->context.uc_stack.ss_sp = f->stack;
    f->context.uc_stack.ss_size = stackSize;
    f->context.uc_link = NULL;
    unsigned long long v = (uintptr_t)f;
    makecontext(&f->context, (void (*)())FiberTrampoline, 2, (unsigned)(v & 0xffffffffu), (unsigned)(v >> 32));
    return f;
}

extern "C" void SwitchToFiber(LPVOID fiber)
{
    Fiber* to = static_cast<Fiber*>(fiber);
    Fiber* from = s_currentFiber;
    if(!to || to == from) return;
    s_currentFiber = to;
    if(from) swapcontext(&from->context, &to->context);
    else     setcontext(&to->context);
}

extern "C" void DeleteFiber(LPVOID fiber)
{
    Fiber* f = static_cast<Fiber*>(fiber);
    if(!f) return;
    if(f == s_currentFiber)
    {
        fprintf(stderr, "[win32 compat] DeleteFiber on the running fiber exits the thread (as on Win32)\n");
        s_currentFiber = NULL;
        free(f->stack);
        free(f);
        pthread_exit(NULL);
    }
    free(f->stack);
    free(f);
}

// ---------------------------------------------------------------- calendar time

extern "C" void GetLocalTime(LPSYSTEMTIME st)
{
    timeval tv;
    gettimeofday(&tv, NULL);
    time_t secs = tv.tv_sec;
    tm local;
    localtime_r(&secs, &local);
    st->wYear = (WORD)(local.tm_year + 1900);
    st->wMonth = (WORD)(local.tm_mon + 1);
    st->wDayOfWeek = (WORD)local.tm_wday;
    st->wDay = (WORD)local.tm_mday;
    st->wHour = (WORD)local.tm_hour;
    st->wMinute = (WORD)local.tm_min;
    st->wSecond = (WORD)local.tm_sec;
    st->wMilliseconds = (WORD)(tv.tv_usec / 1000);
}
