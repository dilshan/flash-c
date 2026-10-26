/*
 * File:    c0.c
 * Purpose: c0 - start-up module. Always linked first: it receives control from
 *          the kernel's entry stub, builds argc/argv/environ, runs the #pragma
 *          startup procedures, calls main() and leaves through exit(), which runs
 *          atexit() handlers and the #pragma exit procedures. It uses only system
 *          calls, so programs that are linked without the standard library still
 *          start and finish properly.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <_fcrt.h>
#include <stdlib.h>

int _argc;
char **_argv;
char **environ;
#ifdef _FC_STKKB
unsigned _stklen = _FC_STKKB * 1024;   /* Linker|Stack size: applied to the stack limit at start-up */
#else
unsigned _stklen = 16384;           /* accepted for source compatibility; Linux grows the stack on demand */
#endif
long __fc_stklimit;                 /* lowest address the stack may reach; tested on function entry when compiled with -N */

/* run-time vectors: the floating-point modules fill them in only when a program uses floating point */
int (*__RealCvtVector)(char *out, double x, int conv, int prec, int alt, int upper);
double (*__ScanTodVector)(const char *s, char **end);
void (*__abort_hook)(void);
void (*__stdin_prep)(void);

int main();

static atexit_t __atexit_tab[32];
static int __atexit_cnt;

void __wr2(const char *s)
{
    long n = 0;
    while (s[n]) n++;
    __syscall(SYS_write, 2, s, n);
}

int atexit(atexit_t fn)
{
    if (__atexit_cnt >= 32) return 1;
    __atexit_tab[__atexit_cnt++] = fn;
    return 0;
}

void _exit(int code)
{
    __syscall(SYS_exit_group, code);
    for (;;) ;
}

/* exit processing without leaving the program */
void _cexit(void)
{
    while (__atexit_cnt > 0) __atexit_tab[--__atexit_cnt]();
    __fc_exit();
}

void _c_exit(void) { }

void exit(int code)
{
    _cexit();
    _exit(code);
}

void abort(void)
{
    if (__abort_hook) __abort_hook();
    __wr2("Abnormal program termination\n");
    _exit(3);
}

void __fp_not_linked(const char *who)
{
    __wr2(who);
    __wr2(" : floating point formats not linked\n");
    abort();
}

void __fc_stkover(void)
{
    __wr2("Stack overflow!\n");
    _exit(1);
}

/* stack limit: the Linux stack grows on demand up to RLIMIT_STACK, so the size chosen under Linker|Stack size is
 * installed with setrlimit() and the same figure bounds the -N overflow test (with a margin for the handler) */
static void __stack_setup(void)
{
    unsigned long rl[2];
    char probe;
#ifdef _FC_STKKB
    if (__syscall(97, 3, rl) == 0) {
        unsigned long want = (unsigned long)_FC_STKKB * 1024;
        if (rl[1] != ~0UL && want > rl[1]) want = rl[1];
        rl[0] = want;
        __syscall(160, 3, rl);
    }
#endif
    if (__syscall(97, 3, rl) == 0 && rl[0] != ~0UL && rl[0] > 65536)
        __fc_stklimit = (long)&probe - (long)rl[0] + 32768;
}

void _fc_start(int argc, char **argv)
{
    __stack_setup();
    _argc = argc;
    _argv = argv;
    environ = argv + argc + 1;
    __fc_init();
    exit(main(_argc, _argv, environ));
}
