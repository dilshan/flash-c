H = "math.h"
def m1(n, what, extra="", ex=None):
    return (n,"double %s(double x);"%n,what,extra,"The result. A domain error sets errno to EDOM; a range error sets it to ERANGE.","",ex)
F = [
m1("acos","Arc cosine.","x must be in -1 to 1. The result is in 0 to pi."),
m1("asin","Arc sine.","x must be in -1 to 1. The result is in -pi/2 to pi/2."),
m1("atan","Arc tangent.","The result is in -pi/2 to pi/2."),
("atan2","double atan2(double y, double x);","Arc tangent of y/x using both signs.","The result is in -pi to pi and is the angle of the point (x, y).","The angle in radians.","atan",None),
m1("ceil","Smallest integer not less than x.","The result is returned as a double."),
m1("cos","Cosine of an angle in radians."),
m1("cosh","Hyperbolic cosine."),
m1("exp","e raised to the power x."),
m1("fabs","Absolute value of a double."),
m1("floor","Largest integer not greater than x.","The result is returned as a double."),
("fmod","double fmod(double x, double y);","Floating-point remainder of x/y.","The result has the sign of x and a magnitude less than that of y.","The remainder; 0 if y is 0 (domain error).","modf",None),
("frexp","double frexp(double value, int *exp);","Splits a number into fraction and power of two.","value = fraction * 2^exp, with the fraction in 0.5 to 1 (or 0).","The fraction; the exponent is stored in *exp.","ldexp modf",None),
("ldexp","double ldexp(double x, int exp);","Multiplies a number by a power of two.","","x * 2^exp.","frexp",None),
m1("log","Natural logarithm.","x must be greater than 0."),
m1("log10","Base-10 logarithm.","x must be greater than 0."),
("modf","double modf(double value, double *iptr);","Splits a number into integer and fractional parts.","Both parts have the sign of value.","The fractional part; the integral part is stored in *iptr.","fmod frexp",None),
("pow","double pow(double x, double y);","x raised to the power y.","A domain error occurs if x is negative and y is not an integer, or if x is 0 and y is not positive.","x^y.","exp sqrt",None),
m1("sin","Sine of an angle in radians."),
m1("sinh","Hyperbolic sine."),
m1("sqrt","Square root.","x must not be negative.","#include <math.h>\n#include <stdio.h>\n\nint main(void)\n{\n    double a = 3.0, b = 4.0;\n    printf(\"hypotenuse = %g\\n\", sqrt(a * a + b * b));\n    return 0;\n}"),
m1("tan","Tangent of an angle in radians."),
m1("tanh","Hyperbolic tangent."),
]
F = [(a,b,c,d,e,f or "sin cos tan atan2 sqrt pow exp log floor ceil fabs".replace(a,"").strip(),g) for a,b,c,d,e,f,g in F]
TYPES = [("HUGE_VAL","The value returned by math functions on overflow (positive infinity, or the largest double)."),("errno","Math functions report domain errors with EDOM and range errors with ERANGE in errno.")]
