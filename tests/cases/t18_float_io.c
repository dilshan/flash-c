#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int main(void)
{
    double x = 3.14159265358979, v; float f; int d, s;
    printf("%f %e %g %.3f %10.2f|%-10.1f|%G\n", x, x, x, x, x, x, 1e-10);
    printf("%g %g %g %f %g\n", 100000.0, 1000000.0, 0.0001, 1e20, 1e-5);
    printf("%.0f %.1f %f %.2e\n", 2.5, 0.05, -1.5, 123456.789);
    printf("%#g %#.0f %+f %08.3f\n", 1.5, 3.0, 2.0, -3.14159);
    printf("%f %f %f %f\n", sin(1.0), cos(1.0), tan(1.0), atan(1.0));
    printf("%f %f %f\n", exp(1.0), log(10.0), pow(2.0, 0.5));
    printf("%f %f %f %f\n", floor(-2.5), ceil(-2.5), fabs(-7.25), fmod(10.0, 3.0));
    printf("%f %f %f\n", sinh(1.0), cosh(1.0), tanh(0.5));
    printf("%f %f\n", asin(0.5), acos(0.5));
    printf("%.3f %.3f\n", hypot(3.0, 4.0), sqrt(144.0));
    printf("%f %f\n", atof("2.5e3"), strtod("  -7.25abc", NULL));
    sscanf("1.5 2.25", "%f %lf", &f, &v); printf("%f %f\n", f, v);
    { int e; double m = frexp(48.0, &e); printf("%f %d %f\n", m, e, ldexp(m, e)); }
    { double ip, fp = modf(-3.75, &ip); printf("%f %f\n", ip, fp); }
    return 0;
}
