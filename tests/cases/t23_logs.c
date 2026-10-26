#include <math.h>
#include <stdio.h>
int main(void)
{
    double x; int bad = 0, n;
    for (n = 0; n <= 22; n++) { x = 1; { int k; for (k = 0; k < n; k++) x *= 10; } if (log10(x) != n) { printf("log10(1e%d)=%.17g\n", n, log10(x)); bad++; } }
    for (n = -10; n <= 60; n++) if (log2(ldexp(1.0, n)) != n) { printf("log2(2^%d)=%.17g\n", n, log2(ldexp(1.0, n))); bad++; }
    printf("%.17g %.17g %.17g %.17g\n", log10(2.0), log(10.0), log(2.718281828459045), log10(0.001));
    printf("%.17g %.17g\n", log(100.0), log(0.5));
    return bad;
}
