/* c0 - start-up module.  Always linked first: it receives control from the kernel's entry stub,
 * builds argc/argv/environ, runs the #pragma startup procedures, calls main() and leaves through
 * exit(), which runs atexit() handlers and the #pragma exit procedures.  It uses only system calls,
 * so programs that are linked without the standard library still start and finish properly. */
#include <_fcrt.h>
#include <stdlib.h>

int _argc;
char **_argv;
char **environ;
unsigned _stklen = 16384;           /* accepted for source compatibility; Linux grows the stack on demand */

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

void _fc_start(int argc, char **argv)
{
    _argc = argc;
    _argv = argv;
    environ = argv + argc + 1;
    __fc_init();
    exit(main(_argc, _argv, environ));
}
