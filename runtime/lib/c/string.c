/* string - ANSI string functions */
#include <string.h>
#include <stdlib.h>

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++) != 0) ;
    return r;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *r = d;
    while (*d) d++;
    while (n-- && *s) *d++ = *s++;
    *d = 0;
    return r;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (*h == *n && !strncmp(h, n, nl)) return (char *)h;
    return NULL;
}

size_t strspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && strchr(set, s[n])) n++;
    return n;
}

size_t strcspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && !strchr(set, s[n])) n++;
    return n;
}

char *strpbrk(const char *s, const char *set)
{
    for (; *s; s++) if (strchr(set, *s)) return (char *)s;
    return NULL;
}

static char *__tok_save;
char *strtok(char *s, const char *delim)
{
    char *start;
    if (!s) s = __tok_save;
    if (!s) return NULL;
    s += strspn(s, delim);
    if (!*s) { __tok_save = NULL; return NULL; }
    start = s;
    s += strcspn(s, delim);
    if (*s) { *s = 0; __tok_save = s + 1; }
    else __tok_save = NULL;
    return start;
}

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

size_t strxfrm(char *d, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (n) { size_t k = l < n - 1 ? l : n - 1; memcpy(d, s, k); d[k] = 0; }
    return l;
}
