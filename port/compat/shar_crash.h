// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Crash reports (port/compat/shar_crash.cpp): on SIGSEGV, SIGBUS, SIGILL, SIGFPE or SIGABRT the game prints a
 * report to stderr (signal, fault address, thread, faulting PC and backtrace as module+offset), then dies with the
 * same signal. port/play.sh keeps the console output and, when the game crashes, saves it with the backtrace
 * resolved to source lines in crashes/ in the run dir. SHAR_CRASH_HANDLER=0 leaves the signals alone.
 * stdout is made line-buffered so that the last lines before a crash reach a pipe (play.sh's log).
 * Report lines: "  #N pc|ret 0xADDR module+0xOFFSET [symbol+0xOFF]"; port/crash_symbolize.py resolves them. */
#ifndef SHAR_COMPAT_SHAR_CRASH_H
#define SHAR_COMPAT_SHAR_CRASH_H

#ifdef __cplusplus
extern "C" {
#endif

void shar_CrashInstall(void);        /* main thread, first thing in main() */
void shar_CrashThreadInit(void);     /* each thread the game creates: an alternate signal stack (stack overflows) */
void shar_CrashThreadExit(void);

#ifdef __cplusplus
}
#endif

#endif
