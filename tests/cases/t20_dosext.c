#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    char buf[80];
    printf("%s %s %s\n", itoa(255, buf, 16), ltoa(-1234567L, buf + 10, 10), ultoa(3000000000UL, buf + 20, 2));
    srand(1); printf("%d %d %d\n", rand(), rand(), rand());
    srand(12345); printf("%d %d\n", rand(), rand());
    strcpy(buf, "MiXed"); printf("%s|", strupr(buf)); printf("%s\n", strlwr(buf));
    printf("%d %d\n", stricmp("ABC", "abd") < 0, strnicmp("ABCd", "abcE", 3));
    printf("%d %d\n", _rotl(0x80000001u, 1), (int)_rotr(1u, 1));
    return 0;
}
