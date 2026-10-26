#include <stdio.h>
struct pt { int x, y; };
struct big { long a[6]; char tag[10]; };
struct pt add(struct pt a, struct pt b) { struct pt r; r.x = a.x + b.x; r.y = a.y + b.y; return r; }
struct pt scale(struct pt p, int k) { p.x *= k; p.y *= k; return p; }
struct big mk(long base)
{
    struct big b; int i;
    for (i = 0; i < 6; i++) b.a[i] = base + i;
    b.tag[0] = 'o'; b.tag[1] = 'k'; b.tag[2] = 0;
    return b;
}
long sum(struct big b, int extra) { long s = extra; int i; for (i = 0; i < 6; i++) s += b.a[i]; b.a[0] = 999; return s; }
struct pt (*fp)(struct pt, int) = scale;
int main(void)
{
    struct pt a = {1, 2}, b = {10, 20}, c;
    struct big g;
    c = add(a, b);
    printf("%d %d\n", c.x, c.y);
    printf("%d\n", add(scale(a, 3), b).y);
    g = mk(100);
    printf("%ld %s\n", sum(g, 5), g.tag);
    printf("%ld\n", g.a[0]);
    printf("%d\n", fp(b, 2).x);
    printf("%ld\n", mk(7).a[5]);
    { struct pt d = add(a, a); printf("%d\n", d.x + d.y); }
    return 0;
}
