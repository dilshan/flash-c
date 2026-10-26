#include <stdio.h>
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
int main()
{
    int i, j, k = 0;
    for (i = 0; i < 5; i++) {
        if (i == 3) continue;
        for (j = 0; j < 3; j++) { if (j == 2) break; k += i * j; }
    }
    printf("%d\n", k);
    i = 0; while (i < 10) { i += 3; }
    printf("%d\n", i);
    do { i--; } while (i > 5);
    printf("%d\n", i);
    for (i = 0; i < 6; i++) {
        switch (i) {
        case 0: printf("zero "); break;
        case 1: case 2: printf("small "); break;
        case 4: printf("four ");
        case 5: printf("five "); break;
        default: printf("other ");
        }
    }
    printf("\n");
    printf("%d %d\n", fib(15), fact(10));
    i = 0;
loop:
    i++;
    if (i < 5) goto loop;
    printf("%d\n", i);
    for (i = 0, j = 10; i < j; i++, j--) ;
    printf("%d %d\n", i, j);
    return 0;
}
