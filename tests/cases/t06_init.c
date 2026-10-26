#include <stdio.h>
int g1 = 5;
int g2[5] = { 1, 2, 3 };
char gs[] = "global string";
char *names[] = { "alpha", "beta", "gamma" };
struct pt { int x, y; } gp = { 7, 8 };
int *gptr = &g1;
int (*gfn)(void);
int zero;
static int sq(int v) { return v * v; }
int table[3] = { 1 + 1, 2 * 3, sizeof(int) };
const char *msg = "const";
int main()
{
    int la[4] = { 1, 2 };
    char lc[10] = "abc";
    int i;
    printf("%d %d %d %d %d\n", g1, g2[0], g2[2], g2[4], zero);
    printf("%s %d\n", gs, (int)sizeof(gs));
    for (i = 0; i < 3; i++) printf("%s ", names[i]);
    printf("\n%d %d %d\n", gp.x, gp.y, *gptr);
    printf("%d %d %d %d\n", la[0], la[1], la[2], la[3]);
    printf("%s %d\n", lc, lc[5]);
    printf("%d %d %d\n", table[0], table[1], table[2]);
    printf("%s\n", msg);
    gfn = (int (*)(void))0;
    printf("%d\n", sq(9));
    return 0;
}
