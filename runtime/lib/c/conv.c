/*
 * File:    conv.c
 * Purpose: conv - string to number and number to string conversions
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

static int __cv_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* common scanner of strtol and strtoul: returns the magnitude, *neg and *over describe it */
static unsigned long __strtox(const char *s, char **end, int base, int *neg, int *over)
{
    unsigned long v = 0, lim;
    int d, any = 0;
    const char *start = s;
    *neg = 0; *over = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-' || *s == '+') { *neg = *s == '-'; s++; }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && __cv_digit(s[2]) < 16) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    if (base < 2 || base > 36) { if (end) *end = (char *)start; errno = EINVAL; return 0; }
    lim = ULONG_MAX / base;
    for (;; s++) {
        d = __cv_digit((unsigned char)*s);
        if (d >= base) break;
        any = 1;
        if (v > lim || (v == lim && (unsigned long)d > ULONG_MAX - lim * base)) *over = 1;
        else v = v * base + d;
    }
    if (end) *end = (char *)(any ? s : start);
    return v;
}

long strtol(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long v = __strtox(s, end, base, &neg, &over);
    if (over || (!neg && v > (unsigned long)LONG_MAX) || (neg && v > (unsigned long)LONG_MAX + 1)) {
        errno = ERANGE;
        return neg ? LONG_MIN : LONG_MAX;
    }
    return neg ? -(long)v : (long)v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long v = __strtox(s, end, base, &neg, &over);
    if (over) { errno = ERANGE; return ULONG_MAX; }
    return neg ? -v : v;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

static char *__cv_utoa(unsigned long u, char *buf, int base, int neg)
{
    char tmp[72]; int i = 0, j = 0;
    if (base < 2 || base > 36) base = 10;
    if (!u) tmp[i++] = '0';
    while (u) { int d = (int)(u % base); tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10; u /= base; }
    if (neg) buf[j++] = '-';
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

char *ultoa(unsigned long v, char *buf, int base) { return __cv_utoa(v, buf, base, 0); }

char *ltoa(long v, char *buf, int base)
{
    if (v < 0 && base == 10) return __cv_utoa(-(unsigned long)v, buf, base, 1);
    return __cv_utoa((unsigned long)v, buf, base, 0);
}

char *itoa(int v, char *buf, int base)
{
    if (v < 0 && base == 10) return __cv_utoa(-(unsigned long)(long)v, buf, base, 1);
    return __cv_utoa((unsigned long)(unsigned int)v, buf, base, 0);
}
