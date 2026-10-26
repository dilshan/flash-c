/* stringx - string functions that are extensions of the ANSI set */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

int stricmp(const char *a, const char *b)
{
    while (*a && tolower(*a) == tolower(*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strcmpi(const char *a, const char *b) { return stricmp(a, b); }

int strnicmp(const char *a, const char *b, size_t n)
{
    while (n && *a && tolower(*a) == tolower(*b)) { a++; b++; n--; }
    if (!n) return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

char *strupr(char *s)
{
    char *p;
    for (p = s; *p; p++) *p = toupper((unsigned char)*p);
    return s;
}

char *strlwr(char *s)
{
    char *p;
    for (p = s; *p; p++) *p = tolower((unsigned char)*p);
    return s;
}

char *strrev(char *s)
{
    size_t i = 0, j = strlen(s);
    while (j > i + 1) {
        char t = s[i]; s[i] = s[j - 1]; s[j - 1] = t;
        i++; j--;
    }
    return s;
}

char *strset(char *s, int c)
{
    char *p;
    for (p = s; *p; p++) *p = c;
    return s;
}

char *strnset(char *s, int c, size_t n)
{
    char *p;
    for (p = s; *p && n; p++, n--) *p = c;
    return s;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *stpcpy(char *d, const char *s)
{
    while ((*d = *s++) != 0) d++;
    return d;
}
