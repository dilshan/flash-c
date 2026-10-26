#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int apply(int (*f)(int, int), int a, int b) { return f(a, b); }
int vsum(int n, ...)
{
    va_list ap; int s = 0;
    va_start(ap, n);
    while (n--) s += va_arg(ap, int);
    va_end(ap);
    return s;
}
int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int counter(void) { static int c = 0; return ++c; }
#define SQR(x) ((x) * (x))
#define MAXV 100
#define CAT(a, b) a##b
#define STR(x) #x
int main()
{
    int (*ops[2])(int, int) = { add, mul };
    int data[8] = { 5, 2, 9, 1, 7, 3, 8, 4 };
    int i;
    char buf[64];
    printf("%d %d\n", apply(add, 3, 4), apply(ops[1], 3, 4));
    printf("%d\n", vsum(4, 1, 2, 3, 4));
    qsort(data, 8, sizeof(int), cmp);
    for (i = 0; i < 8; i++) printf("%d ", data[i]);
    printf("\n");
    counter(); counter();
    printf("%d\n", counter());
    printf("%d %d\n", SQR(5), SQR(2 + 1));
    int CAT(x, y) = 42;
    printf("%d %s\n", xy, STR(hello));
    sprintf(buf, "%05d|%-5d|%5s|%x|%X|%c", 42, 42, "ab", 255, 255, 'z');
    printf("%s\n", buf);
    printf("%+d % d %o %#x %lu\n", 5, 5, 8, 255, 123456789012UL);
    int a, b; char w[16];
    sscanf("12 34 word", "%d %d %s", &a, &b, w);
    printf("%d %d %s\n", a, b, w);
    printf("%d\n", atoi("  -123abc"));
    char *m = malloc(100);
    strcpy(m, "heap"); printf("%s\n", m); free(m);
    printf("%s\n", strstr("find the needle here", "needle"));
    printf("%d\n", strcmp("abc", "abd") < 0);
    return 0;
}
