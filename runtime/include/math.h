#ifndef _FC_MATH_H
#define _FC_MATH_H
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define M_PI_2 1.57079632679489661923
#define M_SQRT2 1.41421356237309504880
#define HUGE_VAL (1.0 / 0.0)
double sqrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double fmod(double x, double y);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double cbrt(double x);
double hypot(double x, double y);
double ldexp(double x, int n);
double frexp(double x, int *e);
double modf(double x, double *ip);
double fmax(double a, double b);
double fmin(double a, double b);
int isnan(double x);
int isinf(double x);
#endif
