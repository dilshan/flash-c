/*
 * File:    math.h
 * Purpose: Mathematical functions and constants.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_MATH_H
#define _FC_MATH_H
#define M_E 2.7182818284590452354
#define M_LOG2E 1.4426950408889634074
#define M_LOG10E 0.43429448190325182765
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_1_SQRTPI 0.56418958354775628695
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT_2 0.70710678118654752440
#define HUGE_VAL (1.0 / 0.0)
#define HUGE_VALL HUGE_VAL
#define HUGE HUGE_VAL
#define DOMAIN 1
#define SING 2
#define OVERFLOW 3
#define UNDERFLOW 4
#define TLOSS 5
#define PLOSS 6

struct exception {
    int type;
    char *name;
    double arg1, arg2, retval;
};

int matherr(struct exception *e);
int abs(int v);
long labs(long v);
double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double atof(const char *s);
double ceil(double x);
double cos(double x);
double cosh(double x);
double exp(double x);
double fabs(double x);
double floor(double x);
double fmod(double x, double y);
double frexp(double x, int *e);
double hypot(double x, double y);
double ldexp(double x, int n);
double log(double x);
double log10(double x);
double modf(double x, double *ip);
double poly(double x, int n, double c[]);
double pow(double x, double y);
double pow10(int p);
double sin(double x);
double sinh(double x);
double sqrt(double x);
double tan(double x);
double tanh(double x);
/* long double is the same as double in this implementation */
double acosl(double x);
double asinl(double x);
double atanl(double x);
double atan2l(double y, double x);
double ceill(double x);
double cosl(double x);
double coshl(double x);
double expl(double x);
double fabsl(double x);
double floorl(double x);
double fmodl(double x, double y);
double frexpl(double x, int *e);
double hypotl(double x, double y);
double ldexpl(double x, int n);
double logl(double x);
double log10l(double x);
double modfl(double x, double *ip);
double polyl(double x, int n, double c[]);
double powl(double x, double y);
double pow10l(int p);
double sinl(double x);
double sinhl(double x);
double sqrtl(double x);
double tanl(double x);
double tanhl(double x);
/* extras beyond the classic library */
double trunc(double x);
double round(double x);
double log2(double x);
double cbrt(double x);
double fmax(double a, double b);
double fmin(double a, double b);
int isnan(double x);
int isinf(double x);
#endif
