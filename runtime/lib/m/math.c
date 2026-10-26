/*
 * File:    math.c
 * Purpose: math - elementary functions. Domain and range errors go through
 *          matherr(): the default handler prints the reference library's "name:
 *          DOMAIN error" style message on stderr and sets errno.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <float.h>


static long __m_bits(double x) { return *(long *)&x; }
static double __m_frombits(long b) { return *(double *)&b; }
static double __m_zero(void) { return 0.0; }
static double __m_nan(void) { double z = __m_zero(); return z / z; }
static double __m_inf(void) { double z = __m_zero(); return 1.0 / z; }
int isnan(double x) { return x != x; }
int isinf(double x) { return x - x != x - x && x == x; }

double fabs(double x) { return __m_frombits(__m_bits(x) & 0x7FFFFFFFFFFFFFFFL); }
double __matherr_call(int type, const char *name, double a1, double a2, double def);
double sqrt(double x) { if (x < 0) return __matherr_call(DOMAIN, "sqrt", x, 0, 0.0); return __sqrt(x); }

double floor(double x)
{
    double t;
    if (x != x || fabs(x) >= 4503599627370496.0) return x;
    t = (double)(long)x;
    if (t > x) t = t - 1.0;
    return t;
}
double ceil(double x)
{
    double t;
    if (x != x || fabs(x) >= 4503599627370496.0) return x;
    t = (double)(long)x;
    if (t < x) t = t + 1.0;
    return t;
}
double trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double ldexp(double x, int n)
{
    while (n > 1000) { x = x * 1.0715086071862673e301; n -= 1000; }
    while (n < -1000) { x = x * 9.332636185032189e-302; n += 1000; }
    return x * __m_frombits((long)(n + 1023) << 52);
}
double frexp(double x, int *e)
{
    long b = __m_bits(x); int ex = (int)((b >> 52) & 0x7FF);
    if (x == 0 || x != x || ex == 0x7FF) { *e = 0; return x; }
    if (ex == 0) { x = x * 18014398509481984.0; b = __m_bits(x); ex = (int)((b >> 52) & 0x7FF) - 54; }
    *e = ex - 1022;
    return __m_frombits((b & 0x800FFFFFFFFFFFFFL) | 0x3FE0000000000000L);
}
double modf(double x, double *ip) { double t = trunc(x); *ip = t; return x - t; }
double fmod(double x, double y)
{
    double q;
    if (x != x || y != y) return __m_nan();
    if (y == 0) return __matherr_call(DOMAIN, "fmod", x, y, 0.0);
    q = trunc(x / y);
    return x - q * y;
}

double exp(double x)
{
    double n, r, term, sum; int k, i;
    if (x != x) return x;
    if (x > 709.78) return __matherr_call(OVERFLOW, "exp", x, 0, HUGE_VAL);
    if (x < -745.2) return 0.0;
    n = floor(x * 1.4426950408889634 + 0.5);
    r = (x - n * 0.693145751953125) - n * 1.4286068203094173e-06;
    sum = 1.0; term = 1.0;
    for (i = 1; i < 20; i++) { term = term * r / i; sum = sum + term; }
    k = (int)n;
    return ldexp(sum, k);
}
double log(double x)
{
    int e; double m, s, s2, sum, t; int i;
    if (x != x) return x;
    if (x < 0) return __matherr_call(DOMAIN, "log", x, 0, -HUGE_VAL);
    if (x == 0) return __matherr_call(SING, "log", x, 0, -HUGE_VAL);
    m = frexp(x, &e);
    if (m < 0.7071067811865476) { m = m * 2.0; e--; }
    s = (m - 1.0) / (m + 1.0); s2 = s * s;
    sum = 0.0; t = s;
    for (i = 1; i < 40; i += 2) { sum = sum + t / i; t = t * s2; }
    return (2.0 * sum + e * 1.4286068203094173e-06) + e * 0.693145751953125;
}
double log10(double x)
{
    if (x < 0) return __matherr_call(DOMAIN, "log10", x, 0, -HUGE_VAL);
    if (x == 0) return __matherr_call(SING, "log10", x, 0, -HUGE_VAL);
    if (x >= 1.0 && x <= 1e22) {            /* exact powers of ten give exact results */
        double p = 1.0; int n;
        for (n = 0; n <= 22; n++, p *= 10.0) if (x == p) return (double)n;
    }
    return log(x) * 0.4342944819032518;
}
double log2(double x)
{
    int e; double m;
    if (x > 0 && x == x) { m = frexp(x, &e); if (m == 0.5) return (double)(e - 1); }     /* exact powers of two */
    return log(x) / 0.6931471805599453;
}
double pow(double x, double y)
{
    double r = 1.0; long n; int neg = 0;
    if (y == 0) return 1.0;
    if (x == 0) { if (y > 0) return 0.0; return __matherr_call(DOMAIN, "pow", x, y, 0.0); }
    if (y == floor(y) && fabs(y) < 1073741824.0) {
        n = (long)y; if (n < 0) { neg = 1; n = -n; }
        while (n) { if (n & 1) r = r * x; x = x * x; n >>= 1; }
        return neg ? 1.0 / r : r;
    }
    if (x < 0) return __matherr_call(DOMAIN, "pow", x, y, 0.0);
    return exp(y * log(x));
}

static void __m_sincos(double x, double *r, int *q)
{
    double k = floor(x * 0.6366197723675814 + 0.5);
    *r = (x - k * 1.5707963267948966) - k * 6.123233995736766e-17;
    *q = (int)(k - 4.0 * floor(k / 4.0));
}
static double __m_sinp(double r)
{
    double r2 = r * r, t = r, sum = r; int i;
    for (i = 1; i < 13; i++) { t = -t * r2 / ((2 * i) * (2 * i + 1)); sum = sum + t; }
    return sum;
}
static double __m_cosp(double r)
{
    double r2 = r * r, t = 1.0, sum = 1.0; int i;
    for (i = 1; i < 13; i++) { t = -t * r2 / ((2 * i - 1) * (2 * i)); sum = sum + t; }
    return sum;
}
double sin(double x)
{
    double r; int q;
    if (x != x) return x;
    __m_sincos(x, &r, &q);
    if (q == 0) return __m_sinp(r);
    if (q == 1) return __m_cosp(r);
    if (q == 2) return -__m_sinp(r);
    return -__m_cosp(r);
}
double cos(double x)
{
    double r; int q;
    if (x != x) return x;
    __m_sincos(x, &r, &q);
    if (q == 0) return __m_cosp(r);
    if (q == 1) return -__m_sinp(r);
    if (q == 2) return -__m_cosp(r);
    return __m_sinp(r);
}
double tan(double x) { return sin(x) / cos(x); }
double atan(double x)
{
    double r2, t, sum; int i, neg = 0, inv = 0;
    if (x != x) return x;
    if (x < 0) { neg = 1; x = -x; }
    if (x > 1.0) { inv = 1; x = 1.0 / x; }
    x = x / (1.0 + sqrt(1.0 + x * x));
    x = x / (1.0 + sqrt(1.0 + x * x));
    r2 = x * x; t = x; sum = 0.0;
    for (i = 1; i < 40; i += 2) { sum = sum + t / i; t = -t * r2; }
    sum = sum * 4.0;
    if (inv) sum = 1.5707963267948966 - sum;
    return neg ? -sum : sum;
}
double atan2(double y, double x)
{
    if (x > 0) return atan(y / x);
    if (x < 0) return y >= 0 ? atan(y / x) + 3.141592653589793 : atan(y / x) - 3.141592653589793;
    if (y > 0) return 1.5707963267948966;
    if (y < 0) return -1.5707963267948966;
    return 0.0;
}
double asin(double x) { if (x > 1 || x < -1) return __matherr_call(DOMAIN, "asin", x, 0, 0.0); return atan2(x, sqrt(1.0 - x * x)); }
double acos(double x) { if (x > 1 || x < -1) return __matherr_call(DOMAIN, "acos", x, 0, 0.0); return 1.5707963267948966 - asin(x); }
double sinh(double x) { double e = exp(x); return (e - 1.0 / e) / 2.0; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) / 2.0; }
double tanh(double x) { double e = exp(2.0 * x); return x > 20 ? 1.0 : (x < -20 ? -1.0 : (e - 1.0) / (e + 1.0)); }
double cbrt(double x) { return x < 0 ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0); }
double hypot(double x, double y) { return sqrt(x * x + y * y); }
double fmax(double a, double b) { return a > b ? a : b; }
double fmin(double a, double b) { return a < b ? a : b; }

/* ---- error reporting */
static const char *__m_types[] = { "", "DOMAIN", "SING", "OVERFLOW", "UNDERFLOW", "TLOSS", "PLOSS" };

int matherr(struct exception *e) { return 0; }

double __matherr_call(int type, const char *name, double a1, double a2, double def)
{
    struct exception e;
    e.type = type; e.name = (char *)name; e.arg1 = a1; e.arg2 = a2; e.retval = def;
    if (!matherr(&e)) {
        if (type == DOMAIN || type == SING) errno = EDOM;
        else if (type == OVERFLOW) errno = ERANGE;
        fprintf(stderr, "%s: %s error\n", name, __m_types[type]);
    }
    return e.retval;
}

double pow10(int p) { return pow(10.0, (double)p); }

double poly(double x, int n, double c[])
{
    double r = c[n];
    int i;
    for (i = n - 1; i >= 0; i--) r = r * x + c[i];
    return r;
}

double sinhl(double x) { return sinh(x); }
double acosl(double x) { return acos(x); }
double asinl(double x) { return asin(x); }
double atanl(double x) { return atan(x); }
double atan2l(double y, double x) { return atan2(y, x); }
double ceill(double x) { return ceil(x); }
double cosl(double x) { return cos(x); }
double coshl(double x) { return cosh(x); }
double expl(double x) { return exp(x); }
double fabsl(double x) { return fabs(x); }
double floorl(double x) { return floor(x); }
double fmodl(double x, double y) { return fmod(x, y); }
double frexpl(double x, int *e) { return frexp(x, e); }
double hypotl(double x, double y) { return hypot(x, y); }
double ldexpl(double x, int n) { return ldexp(x, n); }
double logl(double x) { return log(x); }
double log10l(double x) { return log10(x); }
double modfl(double x, double *ip) { return modf(x, ip); }
double polyl(double x, int n, double c[]) { return poly(x, n, c); }
double powl(double x, double y) { return pow(x, y); }
double pow10l(int p) { return pow10(p); }
double sinl(double x) { return sin(x); }
double sqrtl(double x) { return sqrt(x); }
double tanl(double x) { return tan(x); }
double tanhl(double x) { return tanh(x); }
