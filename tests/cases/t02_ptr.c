#include <stdio.h>
#include <string.h>
int arr[10];
int sum(int *p, int n) { int s = 0; while (n--) s += *p++; return s; }
void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }
int main()
{
    int i, x = 1, y = 2;
    char buf[32];
    char *s = "hello";
    int *p;
    for (i = 0; i < 10; i++) arr[i] = i * i;
    printf("%d\n", sum(arr, 10));
    p = &arr[3];
    printf("%d %d %d\n", *p, p[2], *(p - 1));
    printf("%d\n", (int)(&arr[9] - &arr[2]));
    swap(&x, &y);
    printf("%d %d\n", x, y);
    strcpy(buf, s);
    strcat(buf, " world");
    printf("%s %d\n", buf, (int)strlen(buf));
    printf("%c\n", s[1]);
    char **pp = &s;
    printf("%s\n", *pp + 2);
    int m[3][4];
    int r, c;
    for (r = 0; r < 3; r++) for (c = 0; c < 4; c++) m[r][c] = r * 10 + c;
    printf("%d %d %d\n", m[0][0], m[1][2], m[2][3]);
    printf("%d\n", (int)sizeof(m) / (int)sizeof(m[0]));
    int (*mp)[4] = m;
    printf("%d\n", mp[2][1]);
    return 0;
}
