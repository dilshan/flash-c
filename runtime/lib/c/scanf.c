/*
 * File:    scanf.c
 * Purpose: scanf - formatted input. %e %f %g use __ScanTodVector (installed by
 *          the floating-point run-time).
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <_fcrt.h>
#include <_fcio.h>

struct __src {
    const char *s;
    FILE *fp;
    int pos;
    int pushed;
    int nread;
};

/* Reads the next input character of a scan source (a stream or a string). */
static int __sgetc(struct __src *r)
{
    int c;
    if (r->pushed != -2) {
        c = r->pushed;
        r->pushed = -2;
        if (c != EOF) {
            r->nread++;
        }
        return c;
    }
    if (r->fp) {
        c = fgetc(r->fp);
    } else if (!r->s[r->pos]) {
        c = EOF;
    } else {
        c = (unsigned char)r->s[r->pos++];
    }
    if (c != EOF) {
        r->nread++;
    }
    return c;
}

/* Pushes a character back onto the scan source. */
static void __sunget(struct __src *r, int c)
{
    r->pushed = c;
    if (c != EOF) {
        r->nread--;
    }
}

/* Returns the value of a digit character in bases up to 36, or 99. */
static int __sc_digit(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 99;
}

/* scans the "%[...]" set: returns a pointer after the closing ']' and fills tab[256] */
static const char *__scanset(const char *f, char *tab)
{
    int neg = 0, i, first = 1;
    for (i = 0; i < 256; i++) {
        tab[i] = 0;
    }
    if (*f == '^') {
        neg = 1;
        f++;
    }
    while (*f && (*f != ']' || first)) {
        unsigned char lo = *f;
        first = 0;
        if (f[1] == '-' && f[2] && f[2] != ']') {
            unsigned char hi = f[2];
            for (i = lo; i <= hi; i++) {
                tab[i] = 1;
            }
            f += 3;
        } else {
            tab[lo] = 1;
            f++;
        }
    }
    if (neg) {
        for (i = 0; i < 256; i++) {
            tab[i] = !tab[i];
        }
    }
    if (*f == ']') {
        f++;
    }
    return f;
}

/* The conversion engine behind scanf() and friends; returns the number of assignments. */
static int __vscan1(struct __src *r, const char *f, va_list ap)
{
    int count = 0, c;
    r->pushed = -2;
    while (*f) {
        int lng = 0, hmod = 0, suppress = 0, width = 0;
        if (isspace((unsigned char)*f)) {
            do {
                c = __sgetc(r);
            } while (c != EOF && isspace(c));
            __sunget(r, c);
            f++;
            continue;
        }
        if (*f != '%') {
            c = __sgetc(r);
            if (c != (unsigned char)*f) {
                __sunget(r, c);
                return count ? count : (c == EOF ? EOF : 0);
            }
            f++;
            continue;
        }
        f++;
        if (*f == '*') {
            suppress = 1;
            f++;
        }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f++ - '0');
        }
        while (*f == 'l' || *f == 'h' || *f == 'L' || *f == 'z' || *f == 'j' || *f == 't' ||
               *f == 'q') {
            if (*f == 'h') {
                hmod++;
            } else {
                lng++;
            }
            f++;
        }
        if (*f != 'c' && *f != '[' && *f != '%' && *f != 'n') {
            do {
                c = __sgetc(r);
            } while (c != EOF && isspace(c));
            __sunget(r, c);
        }
        switch (*f) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'p': {
            unsigned long v = 0;
            int neg = 0, base = 10, n = 0, conv = *f;
            if (conv == 'x' || conv == 'X' || conv == 'p') {
                base = 16;
            }
            if (conv == 'o') {
                base = 8;
            }
            if (conv == 'i') {
                base = 0;
            }
            c = __sgetc(r);
            if ((c == '-' || c == '+') && (!width || width > 1)) {
                neg = c == '-';
                n++;
                c = __sgetc(r);
            }
            if ((base == 0 || base == 16) && c == '0') {
                int c2 = __sgetc(r);
                if (c2 == 'x' || c2 == 'X') {
                    base = 16;
                    n += 2;
                    c = __sgetc(r);
                } else {
                    __sunget(r, c2);
                    if (base == 0) {
                        base = 8;
                    }
                }
            }
            if (base == 0) {
                base = 10;
            }
            {
                int nd = 0;
                while (c != EOF && (!width || n < width)) {
                    int d = __sc_digit(c);
                    if (d >= base) {
                        break;
                    }
                    v = v * base + d;
                    n++;
                    nd++;
                    c = __sgetc(r);
                }
                __sunget(r, c);
                if (!nd) {
                    return count ? count : (c == EOF ? EOF : 0);
                }
            }
            if (neg) {
                v = -v;
            }
            if (!suppress) {
                if (conv == 'p') {
                    *va_arg(ap, void **) = (void *)v;
                } else if (lng) {
                    *va_arg(ap, long *) = v;
                } else if (hmod == 1) {
                    *va_arg(ap, short *) = (short)v;
                } else if (hmod > 1) {
                    *va_arg(ap, char *) = (char)v;
                } else {
                    *va_arg(ap, int *) = (int)v;
                }
                count++;
            }
            break;
        }
        case 'f':
        case 'e':
        case 'g':
        case 'E':
        case 'G':
        case 'F': {
            char fb[128];
            int n = 0, seen_dot = 0, seen_e = 0, nd = 0;
            double v;
            c = __sgetc(r);
            if ((c == '-' || c == '+') && (!width || width > 1)) {
                fb[n++] = c;
                c = __sgetc(r);
            }
            while (c != EOF && n < 120 && (!width || n < width)) {
                if (c >= '0' && c <= '9') {
                    fb[n++] = c;
                    nd++;
                } else if (c == '.' && !seen_dot && !seen_e) {
                    fb[n++] = c;
                    seen_dot = 1;
                } else if ((c == 'e' || c == 'E') && nd && !seen_e) {
                    fb[n++] = c;
                    seen_e = 1;
                } else if ((c == '-' || c == '+') && seen_e &&
                           (fb[n - 1] == 'e' || fb[n - 1] == 'E')) {
                    fb[n++] = c;
                } else {
                    break;
                }
                c = __sgetc(r);
            }
            __sunget(r, c);
            fb[n] = 0;
            if (!nd) {
                return count ? count : (c == EOF ? EOF : 0);
            }
            if (!__ScanTodVector) {
                __fp_not_linked("scanf");
            }
            v = __ScanTodVector(fb, NULL);
            if (!suppress) {
                if (lng) {
                    *va_arg(ap, double *) = v;
                } else {
                    *va_arg(ap, float *) = (float)v;
                }
                count++;
            }
            break;
        }
        case 's': {
            char *d = suppress ? NULL : va_arg(ap, char *);
            int n = 0;
            c = __sgetc(r);
            if (c == EOF) {
                return count ? count : EOF;
            }
            while (c != EOF && !isspace(c) && (!width || n < width)) {
                if (d) {
                    d[n] = c;
                }
                n++;
                c = __sgetc(r);
            }
            __sunget(r, c);
            if (d) {
                d[n] = 0;
                count++;
            }
            break;
        }
        case 'c': {
            char *d = suppress ? NULL : va_arg(ap, char *);
            int n = 0, w = width ? width : 1;
            while (n < w) {
                c = __sgetc(r);
                if (c == EOF) {
                    if (!n) {
                        return count ? count : EOF;
                    }
                    break;
                }
                if (d) {
                    d[n] = c;
                }
                n++;
            }
            if (d) {
                count++;
            }
            break;
        }
        case '[': {
            char tab[256];
            char *d = suppress ? NULL : va_arg(ap, char *);
            int n = 0;
            f = __scanset(f + 1, tab) - 1;
            c = __sgetc(r);
            if (c == EOF) {
                return count ? count : EOF;
            }
            while (c != EOF && tab[(unsigned char)c] && (!width || n < width)) {
                if (d) {
                    d[n] = c;
                }
                n++;
                c = __sgetc(r);
            }
            __sunget(r, c);
            if (!n) {
                return count;
            }
            if (d) {
                d[n] = 0;
                count++;
            }
            break;
        }
        case 'n':
            if (!suppress) {
                *va_arg(ap, int *) = r->nread;
            }
            break;
        case '%':
            c = __sgetc(r);
            if (c != '%') {
                __sunget(r, c);
                return count;
            }
            break;
        default:
            return count;
        }
        f++;
    }
    return count;
}

/* Runs __vscan1 and gives an unused look-ahead character back to the stream. */
static int __vscan(struct __src *r, const char *f, va_list ap)
{
    int n = __vscan1(r, f, ap);
    if (r->fp && r->pushed != -2 && r->pushed != EOF) {
        ungetc(r->pushed, r->fp); /* give back the look-ahead character */
    }
    return n;
}

/* scanf from a stream with a va_list. */
int vfscanf(FILE *f, const char *fmt, va_list ap)
{
    struct __src r;
    r.s = NULL;
    r.fp = f;
    r.pos = 0;
    r.nread = 0;
    return __vscan(&r, fmt, ap);
}

/* scanf from standard input with a va_list. */
int vscanf(const char *fmt, va_list ap)
{
    return vfscanf(stdin, fmt, ap);
}

/* scanf from a string with a va_list. */
int vsscanf(const char *str, const char *fmt, va_list ap)
{
    struct __src r;
    r.s = str;
    r.fp = NULL;
    r.pos = 0;
    r.nread = 0;
    return __vscan(&r, fmt, ap);
}

/*
 * Reads formatted input from stdin. Returns the number of items assigned, or EOF if input
 * ended before the first conversion.
 */
int scanf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vfscanf(stdin, fmt, ap);
}

/*
 * Reads formatted input from a stream. Returns the number of items assigned, or EOF if input
 * ended before the first conversion.
 */
int fscanf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vfscanf(f, fmt, ap);
}

/*
 * Reads formatted input from a string. Returns the number of items assigned, or EOF if the
 * string ended before the first conversion.
 */
int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vsscanf(str, fmt, ap);
}
