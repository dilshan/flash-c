#include <stdio.h>
int main()
{
    int a = 7, b = 3;
    unsigned u = 4000000000U;
    long big = 1L << 40;
    printf("%d %d %d %d %d\n", a + b, a - b, a * b, a / b, a % b);
    printf("%d %d %d\n", -a / b, -a % b, a / -b);
    printf("%u %u\n", u, u / 3);
    printf("%ld\n", big);
    printf("%d %d %d %d\n", a & b, a | b, a ^ b, ~a);
    printf("%d %d\n", a << 3, -a >> 1);
    printf("%u\n", u >> 4);
    printf("%d %d %d %d\n", a < b, a > b, a == 7, a != 7);
    printf("%d\n", u > 5);
    printf("%d\n", (char)200);
    printf("%d\n", (unsigned char)200);
    printf("%d\n", (short)70000);
    printf("%d\n", 0x7fffffff + 1 < 0);
    a += 5; a -= 2; a *= 3; a /= 2; a %= 7; a <<= 2; a >>= 1; a |= 8; a &= 12; a ^= 5;
    printf("%d\n", a);
    int i = 5;
    printf("%d ", i++); printf("%d ", ++i); printf("%d ", i--); printf("%d\n", --i);
    printf("%d\n", i);
    printf("%d %d\n", !0, !5);
    printf("%d\n", 5 > 3 && 2 > 1 || 0);
    printf("%d\n", 1 ? 10 : 20);
    printf("%d\n", (1, 2, 3));
    return 0;
}
