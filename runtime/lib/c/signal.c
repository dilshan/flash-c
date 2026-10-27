/*
 * File:    signal.c
 * Purpose: signal - signal handling. Handlers are called by the kernel through a
 *          small stub written by the linker (__sigtramp), which converts the
 *          system's register convention to the one used by compiled code.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <signal.h>
#include <errno.h>
#include <_fcrt.h>

static __sighandler_t __handlers[NSIG];
void __sigtramp(void);    /* written by the linker */
void __sigrestorer(void); /* written by the linker: system call rt_sigreturn */

/* Called by the system when a signal arrives; runs the handler installed with signal(). */
void __sig_dispatch(int sig)
{
    __sighandler_t h;
    if (sig <= 0 || sig >= NSIG) {
        return;
    }
    h = __handlers[sig];
    if (h != SIG_DFL && h != SIG_IGN && h != SIG_ERR) {
        h(sig);
    }
}

/* Sets the handler for a signal. Returns the previous handler, or SIG_ERR. */
__sighandler_t signal(int sig, __sighandler_t h)
{
    long act[4], old[4];
    __sighandler_t prev;
    if (sig <= 0 || sig >= NSIG || sig == SIGKILL || h == SIG_ERR) {
        errno = EINVAL;
        return SIG_ERR;
    }
    prev = __handlers[sig];
    if (h == SIG_DFL || h == SIG_IGN) {
        act[0] = (long)h;
    } else {
        act[0] = (long)__sigtramp;
    }
    act[1] = 0x04000000L | 0x10000000L; /* SA_RESTORER | SA_RESTART */
    act[2] = (long)__sigrestorer;
    act[3] = 0;
    if (__sysret(__syscall(SYS_rt_sigaction, sig, act, old, 8)) < 0) {
        return SIG_ERR;
    }
    __handlers[sig] = h;
    return prev;
}

/* Sends a signal to the program. Returns 0 on success. */
int raise(int sig)
{
    if (sig <= 0 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    return __sysret(__syscall(SYS_kill, __syscall(SYS_getpid), sig)) < 0 ? -1 : 0;
}

/* abort() gives a SIGABRT handler the chance to run before the program ends */
static void __sig_abort(void)
{
    if (__handlers[SIGABRT] != SIG_DFL && __handlers[SIGABRT] != SIG_IGN) {
        raise(SIGABRT);
    }
}

/* Start-up function: lets abort() raise SIGABRT through the signal handlers. */
static void __sig_init(void)
{
    __abort_hook = __sig_abort;
}

#pragma startup __sig_init 8
