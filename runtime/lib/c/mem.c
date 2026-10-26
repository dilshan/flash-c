/* mem - memory block functions */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void *memcpy(void *d, const void *s, size_t n)
{
    char *dp = d; const char *sp = s;
    while (n >= 8) { *(long *)dp = *(const long *)sp; dp += 8; sp += 8; n -= 8; }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    char *dp = d; const char *sp = s;
    if (dp < sp) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    char *dp = d;
    long w = (unsigned char)c;
    w = w | (w << 8); w = w | (w << 16); w = w | (w << 32);
    while (n >= 8) { *(long *)dp = w; dp += 8; n -= 8; }
    while (n--) *dp++ = c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++; y++;
    }
    return 0;
}

int memicmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        int p = tolower(*x), q = tolower(*y);
        if (p != q) return p - q;
        x++; y++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return NULL;
}

void *memccpy(void *d, const void *s, int c, size_t n)
{
    char *dp = d; const char *sp = s;
    while (n--) {
        *dp = *sp++;
        if ((unsigned char)*dp++ == (unsigned char)c) return dp;
    }
    return NULL;
}

void movmem(const void *src, void *dst, unsigned n) { memmove(dst, src, n); }
void setmem(void *dst, unsigned n, char c) { memset(dst, c, n); }

void swab(char *from, char *to, int nbytes)
{
    int i;
    for (i = 0; i + 1 < nbytes; i += 2) { char t = from[i]; to[i] = from[i + 1]; to[i + 1] = t; }
}
