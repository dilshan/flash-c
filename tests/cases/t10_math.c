#include <stdio.h>
#include <math.h>
int main()
{
    double x;
    for (x = 0.1; x < 3.0; x += 0.7) {
        printf("%.6f %.6f %.6f %.6f\n", sin(x), cos(x), tan(x), atan(x));
        printf("%.6f %.6f %.6f %.6f\n", exp(x), log(x), sqrt(x), pow(x, 2.5));
    }
    printf("%.6f %.6f %.6f %.6f\n", floor(-2.5), ceil(-2.5), fabs(-7.25), fmod(7.5, 2.0));
    printf("%.6f %.6f\n", pow(2, 10), pow(2, -2));
    printf("%.6f %.6f %.6f\n", asin(0.5), acos(0.5), atan2(1, -1));
    printf("%.6f %.6f %.6f\n", sinh(1.0), cosh(1.0), tanh(0.5));
    printf("%.6f %.6f\n", log10(1000.0), M_PI);
    printf("%.6f\n", sin(100.0));
    return 0;
}
