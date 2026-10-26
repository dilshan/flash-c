/* printf - formatted output.  Floating-point conversions are done through __RealCvtVector, which the
 * floating-point run-time (libm/fpcvt) installs when a program uses floating point; without it %e %f %g
 * stop the program with "floating point formats not linked", as the reference run-time does. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <_fcrt.h>
#include <_fcio.h>

struct __sink { char *buf; long cap; long n; FILE *fp; };

static void __put(struct __sink *s, int c)
{
    if (s->fp) fputc(c, s->fp);
    else if (s->n + 1 < s->cap) s->buf[s->n] = c;
    s->n++;
}

static void __pad(struct __sink *s, int c, int n)
{
    while (n-- > 0) __put(s, c);
}

static int __vprinter(struct __sink *s, const char *f, va_list ap)
{
    char tmp[72];
    const char *digits;
    while (*f) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1, lng = 0, hmod = 0;
        int c, base, neg, len, ndig, plen;
        unsigned long u;
        char *p;
        char prefix[4];
        if (*f != '%') { __put(s, *f++); continue; }
        f++;
        for (;;) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1;
            else if (*f == '#') alt = 1;
            else break;
            f++;
        }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; if (prec < 0) prec = -1; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
        }
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'L' || *f == 'j' || *f == 't' || *f == 'q') {
            if (*f == 'h') hmod++; else lng++;
            f++;
        }
        c = *f;
        if (!c) break;
        f++;
        digits = "0123456789abcdef";
        switch (c) {
        case '%': __put(s, '%'); break;
        case 'n': {
            int *np = va_arg(ap, int *);
            if (np) *np = (int)s->n;
            break;
        }
        case 'c': {
            int ch = va_arg(ap, int);
            if (!left) __pad(s, ' ', width - 1);
            __put(s, ch);
            if (left) __pad(s, ' ', width - 1);
            break;
        }
        case 's': {
            char *str = va_arg(ap, char *);
            if (!str) str = "(null)";
            len = 0;
            while (str[len] && (prec < 0 || len < prec)) len++;
            if (!left) __pad(s, ' ', width - len);
            for (ndig = 0; ndig < len; ndig++) __put(s, str[ndig]);
            if (left) __pad(s, ' ', width - len);
            break;
        }
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            neg = 0; plen = 0; base = 10;
            if (c == 'x' || c == 'X' || c == 'p') base = 16;
            if (c == 'o') base = 8;
            if (c == 'X') digits = "0123456789ABCDEF";
            if (c == 'p') {
                u = (unsigned long)va_arg(ap, void *); alt = 1;
                if (!u) {                                /* a null pointer prints as (nil) */
                    len = 5;
                    if (!left) __pad(s, ' ', width - len);
                    __put(s, '('); __put(s, 'n'); __put(s, 'i'); __put(s, 'l'); __put(s, ')');
                    if (left) __pad(s, ' ', width - len);
                    break;
                }
            }
            else if (c == 'd' || c == 'i') {
                long v;
                if (lng) v = va_arg(ap, long); else v = va_arg(ap, int);
                if (hmod == 1) v = (short)v; else if (hmod > 1) v = (signed char)v;
                if (v < 0) { neg = 1; u = -v; } else u = v;
            } else {
                if (lng) u = va_arg(ap, unsigned long); else u = va_arg(ap, unsigned int);
                if (hmod == 1) u = (unsigned short)u; else if (hmod > 1) u = (unsigned char)u;
            }
            p = tmp + 72; ndig = 0;
            if (u == 0 && prec == 0) { }
            else do { *--p = digits[u % base]; u /= base; ndig++; } while (u);
            if (neg) prefix[plen++] = '-';
            else if (plus && (c == 'd' || c == 'i')) prefix[plen++] = '+';
            else if (space && (c == 'd' || c == 'i')) prefix[plen++] = ' ';
            if (alt && base == 16 && (ndig || c == 'p')) { prefix[plen++] = '0'; prefix[plen++] = (c == 'X') ? 'X' : 'x'; }
            if (alt && base == 8 && (!ndig || *p != '0')) { *--p = '0'; ndig++; }
            len = ndig; if (prec > len) len = prec;
            if (prec >= 0) zero = 0;
            if (!left && !zero) __pad(s, ' ', width - len - plen);
            { int i; for (i = 0; i < plen; i++) __put(s, prefix[i]); }
            if (!left && zero) __pad(s, '0', width - len - plen);
            __pad(s, '0', len - ndig);
            { int i; for (i = 0; i < ndig; i++) __put(s, p[i]); }
            if (left) __pad(s, ' ', width - len - plen);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(ap, double);
            char nb[1800]; int fneg = 0, flen, pl = 0, lc = (c | 32);
            long bits = *(long *)&v;
            if (!__RealCvtVector) __fp_not_linked("printf");
            if (bits < 0) { fneg = 1; v = -v; }
            flen = __RealCvtVector(nb, v, lc, prec, alt, c == 'E' || c == 'G' || c == 'F');
            if (fneg) prefix[pl++] = '-';
            else if (plus) prefix[pl++] = '+';
            else if (space) prefix[pl++] = ' ';
            if ((bits & 0x7FF0000000000000L) == 0x7FF0000000000000L) zero = 0;     /* inf and nan are never zero padded */
            if (!left && !zero) __pad(s, ' ', width - flen - pl);
            { int i; for (i = 0; i < pl; i++) __put(s, prefix[i]); }
            if (!left && zero) __pad(s, '0', width - flen - pl);
            { int i; for (i = 0; i < flen; i++) __put(s, nb[i]); }
            if (left) __pad(s, ' ', width - flen - pl);
            break;
        }
        default:
            __put(s, '%'); __put(s, c);
        }
    }
    return (int)s->n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct __sink s;
    s.buf = NULL; s.cap = 0; s.n = 0; s.fp = f;
    return __vprinter(&s, fmt, ap);
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct __sink s;
    s.buf = buf; s.cap = n; s.n = 0; s.fp = NULL;
    __vprinter(&s, fmt, ap);
    if (n) buf[s.n < (long)n ? s.n : (long)n - 1] = 0;
    return (int)s.n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, 1L << 30, fmt, ap); }

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vfprintf(f, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vsnprintf(buf, 1L << 30, fmt, ap);
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vsnprintf(buf, n, fmt, ap);
}
