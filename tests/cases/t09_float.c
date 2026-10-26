#include <stdio.h>
#include <stdlib.h>
double avg(int *a, int n) { double s = 0; int i; for (i = 0; i < n; i++) s += a[i]; return s / n; }
float halve(float x) { return x / 2; }
double sq(double x) { return x * x; }
struct pt { double x, y; };
double dist2(struct pt *a, struct pt *b) { return sq(a->x - b->x) + sq(a->y - b->y); }
double gd = 1.25; float gf = 0.1f; double garr[3] = { 1.5, 2.5, 3.5 };
int main()
{
    int v[5] = { 3, 4, 5, 6, 8 };
    double d = 0.1 + 0.2, e = 1e-5, big = 1.5e300, neg = -0.000123456;
    float f = 3.3f;
    int i, n = 0;
    printf("%f %.10f %f\n", avg(v, 5), d, halve(5.0f));
    printf("%.1f %.2f %.3f %.0f %.0f %.0f\n", 0.05, 2.675, 1.0005, 0.5, 1.5, 2.5);
    printf("%e %E %g %G\n", e, big, neg, 1234567.0);
    printf("%g %g %g %g %g\n", 100000.0, 1000000.0, 0.0001, 0.00001, 123456789.0);
    printf("%10.3f|%-10.3f|%+.2f|% .2f|%010.2f\n", 3.14159, 3.14159, 2.0, 2.0, -3.14159);
    printf("%f %f\n", f, (double)f);
    printf("%d %d %d\n", (int)3.99, (int)-3.99, (int)(f * 10));
    printf("%f %f %f\n", gd, gf, garr[1] + garr[2]);
    { struct pt a = {0, 0}, b = {3, 4}; printf("%f\n", dist2(&a, &b)); }
    for (d = 0; d < 1.0; d += 0.25) n++;
    printf("%d %f\n", n, d);
    i = 10; d = i; d += 0.5; d *= 2; d /= 4; d -= 1;
    printf("%f %d\n", d, (int)d);
    i = 5; i += 2.7; printf("%d\n", i);
    d = 5; printf("%f %f\n", d++, ++d);
    printf("%d %d\n", !0.0, !0.5);
    printf("%f\n", 1 ? 2.5 : 3);
    printf("%s\n", 0.1 + 0.2 == 0.3 ? "eq" : "ne");
    { double x; if (sscanf("3.25 -1.5e2", "%lf %lf", &x, &d) == 2) printf("%f %f\n", x, d); }
    printf("%f %f\n", atof("2.5e3"), atof("-0.125"));
    printf("%f %f %f\n", 1.0 / 3, 2.0 / 3, 10.0 / 3);
    printf("%.15f\n", 1.0 / 3);
    printf("%f %f\n", 123456789.123456789, 0.000001);
    printf("%5.2f%%\n", 45.678);
    return 0;
}
