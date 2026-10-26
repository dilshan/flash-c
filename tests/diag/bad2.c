#include <stdio.h>
int twice(int x) { return x * 2; }
int main()
{
    int total = 0;
    char *name;
    total = twice(1, 2);
    name = total;
    undefined_call(total);
    printf("%d\n", total + missing);
    return;
}
