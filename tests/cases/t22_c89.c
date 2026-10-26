#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define HDR <stdlib.h>
#include HDR
#define S(x) #x
const int k = 3;
wchar_t w = L'a';
wchar_t ws[] = L"hi\x41";
wchar_t *wp = L"xyz" L"w";
int main(void)
{
    int a??(3??) = { 1, 2, 3 };
    const char *msg = "ok??!";
    char *const fixed = "fix";
    printf("%d %d %s %d\n", a[1], (int)sizeof ws / 4, strlen(__DATE__) == 11 ? "date" : "bad", (int)strlen(__TIME__));
    printf("%d %d %d %d %d\n", w, ws[2], wp[3], wp[4], k);
    printf("%s %s %s\n", msg, fixed, S(abc));
    return 0;
}
