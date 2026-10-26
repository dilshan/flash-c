#include <stdio.h>
typedef unsigned char byte;
typedef unsigned int uint;
int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
int is_prime(int n) { int i; if (n < 2) return 0; for (i = 2; i * i <= n; i++) if (n % i == 0) return 0; return 1; }
void hanoi(int n, char a, char b, char c, int *moves) { if (!n) return; hanoi(n - 1, a, c, b, moves); (*moves)++; hanoi(n - 1, c, b, a, moves); }
int main()
{
    int i, cnt = 0, moves = 0;
    byte b = 250;
    uint u = 3;
    b += 10;
    printf("%d\n", b);
    printf("%u\n", u - 5);
    printf("%d\n", gcd(84, 36));
    for (i = 0; i < 50; i++) if (is_prime(i)) cnt++;
    printf("%d\n", cnt);
    hanoi(10, 'A', 'B', 'C', &moves);
    printf("%d\n", moves);
    long l = 1;
    for (i = 0; i < 20; i++) l *= 3;
    printf("%ld\n", l);
    int sieve[100]; 
    for (i = 0; i < 100; i++) sieve[i] = 1;
    for (i = 2; i < 10; i++) if (sieve[i]) { int j; for (j = i * i; j < 100; j += i) sieve[j] = 0; }
    cnt = 0; for (i = 2; i < 100; i++) cnt += sieve[i];
    printf("%d\n", cnt);
    { int x = 1; { int x = 2; printf("%d ", x); } printf("%d\n", x); }
    printf("%d\n", sizeof(char) + sizeof(short) + sizeof(int));
    unsigned char uc = 255; signed char sc = -1;
    printf("%d %d %d\n", uc == 255, sc < 0, (int)uc + sc);
    return 0;
}
