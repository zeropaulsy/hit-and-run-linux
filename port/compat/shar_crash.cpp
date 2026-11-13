// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Crash reports, see shar_crash.h. The handler only formats into stack buffers and write()s; snprintf, dladdr
// and backtrace are not async-signal-safe on paper but are fine in practice, and the process is dying anyway.

#include "shar_crash.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>

namespace
{
    const int kSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    const size_t kAltStackSize = 64 * 1024;
    volatile int s_inHandler = 0;
    __thread void* t_altStack = NULL;

    void Out(const char* s) { size_t n = strlen(s); while(n) { ssize_t w = write(2, s, n); if(w <= 0) return; s += w; n -= (size_t)w; } }

    const char* SignalName(int sig)
    {
        switch(sig)
        {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS:  return "SIGBUS";
        case SIGILL:  return "SIGILL";
        case SIGFPE:  return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        default:      return "signal";
        }
    }

    const char* CodeName(int sig, int code)
    {
        if(sig == SIGABRT) return "abort(): failed assert, std::terminate, heap corruption found by glibc, ...";
        if(code == SI_USER || code == SI_TKILL) return "sent by kill/raise";
        switch(sig)
        {
        case SIGSEGV: return code == SEGV_MAPERR ? "address not mapped" : code == SEGV_ACCERR ? "access not permitted" : "";
        case SIGBUS:  return code == BUS_ADRALN ? "misaligned address" : code == BUS_ADRERR ? "address does not exist" : "";
        case SIGILL:  return code == ILL_ILLOPN ? "illegal operand (GCC __builtin_trap: e.g. no return value)"
                           : code == ILL_ILLOPC ? "illegal opcode" : "";
        case SIGFPE:  return code == FPE_INTDIV ? "integer divide by zero" : code == FPE_INTOVF ? "integer overflow" : "";
        default:      return "";
        }
    }

    void Frame(int index, const char* kind, void* addr)
    {
        char line[512];
        Dl_info info;
        if(dladdr(addr, &info) && info.dli_fname)
        {
            const char* module = strrchr(info.dli_fname, '/');
            module = module ? module + 1 : info.dli_fname;
            unsigned long off = (unsigned long)((char*)addr - (char*)info.dli_fbase);
            if(info.dli_sname)
                snprintf(line, sizeof line, "  #%-2d %s %p %s+0x%lx [%s+0x%lx]\n", index, kind, addr, module, off,
                         info.dli_sname, (unsigned long)((char*)addr - (char*)info.dli_saddr));
            else
                snprintf(line, sizeof line, "  #%-2d %s %p %s+0x%lx\n", index, kind, addr, module, off);
        }
        else
            snprintf(line, sizeof line, "  #%-2d %s %p ?\n", index, kind, addr);
        Out(line);
    }

    void Handler(int sig, siginfo_t* si, void* uc)
    {
        if(__sync_lock_test_and_set(&s_inHandler, 1))
        {
            for(;;) pause();                  // another thread is reporting and will take the process down
        }
        char line[512];
        char thread[32] = "?";
        long tid = syscall(SYS_gettid);
        snprintf(line, sizeof line, "/proc/self/task/%ld/comm", tid);
        int fd = open(line, O_RDONLY);
        if(fd >= 0)
        {
            ssize_t n = read(fd, thread, sizeof thread - 1);
            close(fd);
            thread[n > 0 ? n : 0] = '\0';
            if(n > 0 && thread[n - 1] == '\n') thread[n - 1] = '\0';
        }
        time_t now = time(NULL);
        struct tm tmNow;
        char when[32];
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime_r(&now, &tmNow));

        void* pc = NULL;
#if defined(__i386__)
        pc = (void*)((ucontext_t*)uc)->uc_mcontext.gregs[REG_EIP];
#elif defined(__x86_64__)
        pc = (void*)((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP];
#endif
        char fault[48] = "";
        if(si->si_code > 0 && sig != SIGABRT)    // kernel-generated: si_addr is the faulting address / instruction
            snprintf(fault, sizeof fault, ", fault address %p", si->si_addr);
        snprintf(line, sizeof line, "\n=== SHAR CRASH: %s (%s) at %s%s, thread %ld \"%s\"%s ===\n",
                 SignalName(sig), CodeName(sig, si->si_code), when, fault, tid, thread,
                 tid == getpid() ? " (main)" : "");
        Out(line);

        // backtrace() runs through the kernel's signal frame: skip up to the faulting PC so the report starts there
        void* frames[64];
        int n = backtrace(frames, 64);
        int first = 0;
        if(pc)
        {
            for(int i = 0; i < n; i++)
                if(frames[i] == pc) { first = i + 1; break; }
            Frame(0, "pc ", pc);
        }
        int index = pc ? 1 : 0;
        for(int i = (pc && first) ? first : 0; i < n; i++)
            Frame(index++, "ret", frames[i]);
        Out("=== end of crash report ===\n");

        signal(sig, SIG_DFL);
        raise(sig);                           // delivered on return: dies with the original signal (core dump etc.)
    }

    void SetAltStack(void)
    {
        if(t_altStack) return;
        stack_t ss;
        ss.ss_sp = malloc(kAltStackSize);     // malloc only: compat-layer rule
        if(!ss.ss_sp) return;
        ss.ss_size = kAltStackSize;
        ss.ss_flags = 0;
        if(sigaltstack(&ss, NULL) == 0)
            t_altStack = ss.ss_sp;
        else
            free(ss.ss_sp);
    }
}

extern "C" void shar_CrashInstall(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* env = getenv("SHAR_CRASH_HANDLER");
    if(env && strcmp(env, "0") == 0) return;

    void* warm[2];
    backtrace(warm, 2);                       // loads the unwinder now, not inside the handler
    SetAltStack();
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = Handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for(size_t i = 0; i < sizeof kSignals / sizeof kSignals[0]; i++)
        sigaction(kSignals[i], &sa, NULL);
}

extern "C" void shar_CrashThreadInit(void)
{
    SetAltStack();
}

extern "C" void shar_CrashThreadExit(void)
{
    if(!t_altStack) return;
    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_flags = SS_DISABLE;
    if(sigaltstack(&ss, NULL) == 0)
    {
        free(t_altStack);
        t_altStack = NULL;
    }
}
