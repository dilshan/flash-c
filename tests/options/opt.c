#include <stdio.h>
#include <string.h>
#include <stdlib.h>
enum small { S_A, S_B, S_C };
enum wide { W_A = 1000, W_B = 70000 };
enum neg { N_A = -3, N_B = 3 };
struct mixed { char c; int i; char d; long l; };
static int depth(int n) { char pad[200]; pad[0] = (char)n; return n ? depth(n - 1) + pad[0] : 0; }
int main(int argc, char **argv)
{
    char *a = "same text", *b = "same text";
    long k, acc = 0;
    printf("enum %d %d %d\n", (int)sizeof(enum small), (int)sizeof(enum wide), (int)sizeof(enum neg));
    printf("struct %d\n", (int)sizeof(struct mixed));
    printf("strings %s\n", a == b ? "merged" : "separate");
    for (k = 0; k < 20; k++) { if (k % 3 == 0) continue; acc += k * 7 - (k << 1) + (k > 10 ? 1 : 0); }
    printf("acc %ld\n", acc);
    printf("depth %d\n", depth(argc > 1 ? atoi(argv[1]) : 50));
    return 0;
}
