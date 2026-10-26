#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(void)
{
    FILE *f; char line[80]; int a, n; long pos; char w[20];
    f = fopen("fc_t14.tmp", "w");
    fprintf(f, "alpha %d\nbeta %5.2f\n", 42, 3.14159);
    fputs("gamma\n", f); fputc('x', f); fputc('\n', f);
    fclose(f);
    f = fopen("fc_t14.tmp", "r");
    while (fgets(line, sizeof line, f)) printf("[%s]", line);
    printf("\n");
    rewind(f);
    fscanf(f, "%s %d", w, &a); printf("%s %d\n", w, a);
    pos = ftell(f); fseek(f, 0, SEEK_END); printf("%ld %ld\n", pos, ftell(f));
    fseek(f, -2, SEEK_END); printf("%d\n", fgetc(f)); printf("%d\n", fgetc(f)); printf("%d\n", fgetc(f));
    printf("eof %d\n", feof(f) != 0);
    fclose(f);
    f = fopen("fc_t14.tmp", "a"); fwrite("tail", 1, 4, f); fclose(f);
    f = fopen("fc_t14.tmp", "rb"); n = fread(line, 1, 80, f); printf("%d\n", n); fclose(f);
    printf("%d\n", remove("fc_t14.tmp"));
    printf("%d\n", fopen("/nonexistent/zzz", "r") == NULL);
    n = printf("%s|%-6s|%6s|%.2s\n", "ab", "cd", "ef", "ghij"); printf("%d\n", n);
    printf("%5d|%-5d|%05d|%+d|% d|%x|%X|%#x|%o|%u\n", 42, 42, 42, 42, 42, 255, 255, 255, 8, 3000000000u);
    printf("%ld %lu %hd %c %%\n", -123456789L, 4000000000UL, (short)70000, 'q');
    printf("%*d|%-*d|%.*f\n", 6, 7, 6, 7, 2, 2.71828);
    { char b[32]; int r = snprintf(b, 8, "%s", "truncated text"); printf("%s %d\n", b, r); }
    { int x, y; char c; sscanf("10,20;z", "%d,%d;%c", &x, &y, &c); printf("%d %d %c\n", x, y, c); }
    { char s[20]; int k = sscanf("  hello   world", "%s", s); printf("%d %s\n", k, s); }
    { int v; char s[20]; sscanf("abc123", "%[a-z]%d", s, &v); printf("%s %d\n", s, v); }
    { int v; int k = sscanf("zzz", "%d", &v); printf("%d\n", k); }
    return 0;
}
