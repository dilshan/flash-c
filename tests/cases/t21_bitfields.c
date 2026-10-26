#include <stdio.h>
#include <string.h>
struct flags {
    unsigned a : 1;
    unsigned b : 3;
    int c : 4;
    unsigned : 0;
    unsigned d : 5;
    int : 3;
    signed int e : 7;
    int plain;
};
struct full { unsigned int x : 32; int y : 32; };
union u { unsigned lo : 4; unsigned hi : 4; int whole; };
struct flags g = { 1, 5, -3, 17, -9, 1234 };
struct flags garr[2] = { {1,2,3,4,5,6}, {0,7,-8,31,63,7} };
int main(void)
{
    struct flags f;
    struct flags l = { 1, 2, -2, 3, -4, 99 };
    struct full fu;
    union u un;
    int i, n;
    memset(&f, 0, sizeof f);
    printf("sizeof flags=%d full=%d union=%d\n", (int)sizeof(struct flags), (int)sizeof(struct full), (int)sizeof(union u));
    f.a = 1; f.b = 6; f.c = -5; f.d = 31; f.e = -64; f.plain = 77;
    printf("%d %d %d %d %d %d\n", f.a, f.b, f.c, f.d, f.e, f.plain);
    f.b = 9;                      /* truncates to 1 */
    f.c = 9;                      /* truncates to -7 */
    printf("trunc %d %d\n", f.b, f.c);
    f.b += 3; f.c -= 2; f.d++; ++f.d; f.e--;
    printf("ops %d %d %d %d\n", f.b, f.c, f.d, f.e);
    n = f.d++;
    printf("post %d %d\n", n, f.d);
    printf("g: %d %d %d %d %d %d\n", g.a, g.b, g.c, g.d, g.e, g.plain);
    printf("l: %d %d %d %d %d %d\n", l.a, l.b, l.c, l.d, l.e, l.plain);
    for (i = 0; i < 2; i++) printf("garr[%d]: %d %d %d %d %d %d\n", i, garr[i].a, garr[i].b, garr[i].c, garr[i].d, garr[i].e, garr[i].plain);
    fu.x = 0xFFFFFFFFu; fu.y = -1;
    printf("full %u %d\n", fu.x, fu.y);
    un.whole = 0; un.lo = 0xA; printf("union %d %d\n", un.hi, un.whole);
    f.a = 0; f.b = 7;
    printf("expr %d %d\n", f.a + f.b * 2, f.b > 3 ? 100 : 200);
    { struct flags *p = &f; p->c = 3; printf("ptr %d %d\n", p->c, p->plain); }
    { struct flags cp = f; printf("copy %d %d %d\n", cp.b, cp.c, cp.d); }
    return f.b == 7 ? 0 : 1;
}
