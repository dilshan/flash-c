#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 4
typedef int (*binop)(int, int);
typedef struct { char name[16]; int score; double gpa; } Student;
union bits { unsigned int u; unsigned char b[4]; };
static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int cmp_score(const void *a, const void *b) { return ((const Student *)b)->score - ((const Student *)a)->score; }
static void bubble(int *a, int n) { int i, j, t; for (i = 0; i < n - 1; i++) for (j = 0; j < n - 1 - i; j++) if (a[j] > a[j + 1]) { t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; } }
static long gcd(long a, long b) { return b ? gcd(b, a % b) : a; }
static int bitcount(unsigned int x) { int c = 0; while (x) { c += x & 1; x >>= 1; } return c; }
static void matmul(double a[N][N], double b[N][N], double c[N][N]) { int i, j, k; for (i = 0; i < N; i++) for (j = 0; j < N; j++) { c[i][j] = 0; for (k = 0; k < N; k++) c[i][j] += a[i][k] * b[k][j]; } }
static char *itoa_b(unsigned int v, int base, char *buf) { char *p = buf + 32; *p = 0; do { *--p = "0123456789ABCDEF"[v % base]; v /= base; } while (v); return p; }
int main(int argc, char **argv)
{
    int data[8] = { 42, 7, 19, 3, 88, 1, 56, 23 }, i, j;
    binop ops[2] = { add, sub };
    Student cls[3] = { { "Ann", 91, 3.8 }, { "Bob", 78, 3.1 }, { "Cy", 85, 3.5 } };
    union bits u;
    double A[N][N], B[N][N], C[N][N];
    char buf[40], *words[] = { "pear", "apple", "fig" };
    long fact = 1;
    bubble(data, 8);
    for (i = 0; i < 8; i++) printf("%d ", data[i]);
    printf("\n%d %d\n", ops[0](5, 3), ops[1](5, 3));
    qsort(cls, 3, sizeof(Student), cmp_score);
    for (i = 0; i < 3; i++) printf("%-5s %3d %.1f\n", cls[i].name, cls[i].score, cls[i].gpa);
    u.u = 0xA1B2C3D4;
    printf("%02X %02X %d\n", u.b[0], u.b[3], bitcount(u.u));
    for (i = 0; i < N; i++) for (j = 0; j < N; j++) { A[i][j] = i + j; B[i][j] = i == j; }
    matmul(A, B, C);
    printf("%.1f %.1f %.1f\n", C[0][0], C[1][2], C[3][3]);
    printf("%s %s\n", itoa_b(255, 2, buf), itoa_b(255, 16, buf));
    printf("%ld %ld\n", gcd(1071, 462), gcd(17, 5));
    for (i = 1; i <= 15; i++) fact *= i;
    printf("%ld\n", fact);
    for (i = 0; i < 3; i++) for (j = i + 1; j < 3; j++) if (strcmp(words[i], words[j]) > 0) { char *t = words[i]; words[i] = words[j]; words[j] = t; }
    printf("%s %s %s\n", words[0], words[1], words[2]);
    printf("%d %d %d\n", argc, (int)sizeof(Student), (int)sizeof(union bits));
    { char *p = malloc(16); int *q = (int *)p; q[0] = 7; q[1] = 9; printf("%d\n", q[0] + q[1]); free(p); }
    { unsigned char c = 200; signed char s = -5; unsigned short us = 65535; short ss = -1; printf("%d %d %d %d\n", c + 100, s * 2, us + 1, ss >> 1); }
    { int x = 5; int *px = &x; int **ppx = &px; **ppx += 10; printf("%d\n", x); }
    return 0;
}
