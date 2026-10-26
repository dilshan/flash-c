#include <string.h>
struct big { int a, b; };
static void take(struct big b) { (void)b; }
static int f(int x)
{
    return x;
    x++;
}
int main(void)
{
    unsigned char *u = 0;
    char *c = (char *)"x";
    struct big b;
    int x = 1, y = 2, z = 3;
    b.a = b.b = 0;
    u = c;
    if (1) x++;
    if (0) y++;
    take(b);
    z = x && y || z;
    z = x & y | z;
    z = x << y + 1;
    return f(z) + (x != 0);
}
