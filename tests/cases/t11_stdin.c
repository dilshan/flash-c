#include <stdio.h>
#include <string.h>
int main()
{
    int n, i, sum = 0, v[20];
    char line[100], ch;
    float f; int k;
    scanf("%d", &n);
    for (i = 0; i < n; i++) { scanf("%d", &v[i]); sum += v[i]; }
    printf("n=%d sum=%d avg=%.2f\n", n, sum, (float)sum / n);
    getchar();
    fgets(line, sizeof line, stdin);
    line[strlen(line) - 1] = 0;
    printf("line=[%s] len=%d\n", line, (int)strlen(line));
    ch = getchar();
    printf("ch=%c\n", ch);
    scanf("%f %d", &f, &k);
    printf("%.1f %d %.2f\n", f, k, f * k);
    printf("eof=%d\n", getchar() == EOF || 1);
    return 0;
}
