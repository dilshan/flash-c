#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
static int cmpi(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int cmps(const void *a, const void *b) { return strcmp(*(char **)a, *(char **)b); }
int main(void)
{
    int a[40], i, key = 17, *p; char *names[] = { "pear", "apple", "fig", "banana", "cherry" }; char buf[40]; char *e;
    for (i = 0; i < 40; i++) a[i] = (i * 37) % 41;
    qsort(a, 40, sizeof a[0], cmpi);
    for (i = 0; i < 40; i++) printf("%d ", a[i]); printf("\n");
    p = bsearch(&key, a, 40, sizeof a[0], cmpi); printf("%d\n", p ? (int)(p - a) : -1);
    qsort(names, 5, sizeof names[0], cmps);
    for (i = 0; i < 5; i++) printf("%s ", names[i]); printf("\n");
    printf("%ld %ld %ld %ld\n", strtol("  -42abc", &e, 10), strtol("0x1F", 0, 16), strtol("777", 0, 8), strtol("0b", 0, 0));
    printf("%s\n", e);
    printf("%lu\n", strtoul("4294967295", 0, 10));
    errno = 0; printf("%ld %d\n", strtol("99999999999999999999", 0, 10) == LONG_MAX, errno == ERANGE);
    printf("%d %ld %d %ld\n", abs(-5), labs(-123456789L), atoi("  12x"), atol("-99"));
    { div_t d = div(-17, 5); ldiv_t l = ldiv(100000L, 7L); printf("%d %d %ld %ld\n", d.quot, d.rem, l.quot, l.rem); }
    { char *d = strdup("copy"); printf("%s %d\n", d, (int)strlen(d)); free(d); }
    { char *m = malloc(100); memset(m, 'x', 99); m[99] = 0; m = realloc(m, 200); printf("%d\n", (int)strlen(m)); free(m); }
    { char *c = calloc(10, 4); int z = 0; for (i = 0; i < 40; i++) z += c[i]; printf("%d\n", z); free(c); }
    { char s[] = "a,b;;c"; char *t = strtok(s, ",;"); while (t) { printf("<%s>", t); t = strtok(NULL, ",;"); } printf("\n"); }
    { char *h = strstr("hello world", "o w"); printf("%s %s\n", h, strchr("abcabc", 'c')); printf("%s\n", strrchr("abcabc", 'b')); }
    printf("%d %d %d %d\n", isalpha('a') != 0, isdigit('x') != 0, toupper('q'), isspace('\t') != 0);
    { putenv("FCTEST=hello"); printf("%s %d\n", getenv("FCTEST"), getenv("FC_NOT_SET") == NULL); }
    return 0;
}
