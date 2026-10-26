#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
static jmp_buf jb;
static int hits;
static void onsig(int s) { hits += s; }
static void deep(int n) { if (n == 0) longjmp(jb, 7); deep(n - 1); }
static void bye(void) { printf("atexit 1\n"); }
static void bye2(void) { printf("atexit 2\n"); }
int main(void)
{
    int r; volatile int local = 5;
    atexit(bye); atexit(bye2);
    r = setjmp(jb);
    if (r == 0) { printf("first\n"); local = 9; deep(20); printf("not reached\n"); }
    else printf("back with %d, local %d\n", r, local);
    if (setjmp(jb) == 0) longjmp(jb, 0); else printf("zero became 1\n");
    signal(SIGUSR1, onsig); raise(SIGUSR1); raise(SIGUSR1);
    printf("hits %d\n", hits);
    signal(SIGUSR1, SIG_IGN); raise(SIGUSR1); printf("ignored\n");
    return 3;
}
