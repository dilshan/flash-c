/* fpcvt - floating-point conversion: the formatting behind %e %f %g and the parsing behind %lf and strtod.
 * Linking this module (any program that uses floating point does) registers the two conversion vectors
 * that printf and scanf call through. */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <_fcrt.h>

int __fpused;
static long __f_bits(double x) { return *(long *)&x; }
static int __f_isinf(double x) { return x - x != x - x && x == x; }

/* ----- exact decimal expansion of |x| and the %f %e %g converters ----- */
static int __f_expand(double x, char *buf, int *ipart)
{
    long bits = __f_bits(x);
    unsigned long m = (unsigned long)bits & 0xFFFFFFFFFFFFFUL;
    int e = (int)((bits >> 52) & 0x7FF), n = 0, i, k;
    if (e == 0) e = 1; else m = m | (1UL << 52);
    e = e - 1075;
    if (e >= 0) {
        unsigned long limb[40]; int nl = 0; unsigned long carry, t;
        while (m) { limb[nl++] = m % 1000000000UL; m = m / 1000000000UL; }
        if (!nl) limb[nl++] = 0;
        for (i = 0; i < e; i++) {
            carry = 0;
            for (k = 0; k < nl; k++) { t = limb[k] * 2 + carry; limb[k] = t % 1000000000UL; carry = t / 1000000000UL; }
            if (carry) limb[nl++] = carry;
        }
        { char tmp[12]; int j;
          for (k = nl - 1; k >= 0; k--) {
              t = limb[k];
              for (j = 8; j >= 0; j--) { tmp[j] = '0' + t % 10; t = t / 10; }
              for (j = 0; j < 9; j++) {
                  if (n == 0 && tmp[j] == '0' && !(k == 0 && j == 8)) continue;
                  buf[n++] = tmp[j];
              }
          }
        }
        *ipart = n;
        return n;
    } else {
        unsigned long ip, frac, a[40]; int nl, pos, d, b; unsigned long carry, t;
        k = -e;
        if (k >= 64) { ip = 0; frac = m; }
        else { ip = m >> k; frac = m & ((1UL << k) - 1); }
        { char tmp[24]; int j = 0;
          if (!ip) tmp[j++] = '0';
          while (ip) { tmp[j++] = '0' + ip % 10; ip = ip / 10; }
          while (j) buf[n++] = tmp[--j];
        }
        *ipart = n;
        nl = 0;
        a[0] = frac & 0xFFFFFFFFUL; a[1] = frac >> 32; nl = 2;
        for (;;) {
            int nz = 0;
            for (i = 0; i < nl; i++) if (a[i]) nz = 1;
            if (!nz) break;
            carry = 0;
            for (i = 0; i < nl; i++) { t = a[i] * 10 + carry; a[i] = t & 0xFFFFFFFFUL; carry = t >> 32; }
            if (carry) a[nl++] = carry;
            d = 0;
            for (b = 0; b < 4; b++) {
                pos = k + b;
                if ((pos >> 5) < nl && ((a[pos >> 5] >> (pos & 31)) & 1)) { d = d | (1 << b); a[pos >> 5] = a[pos >> 5] & ~(1UL << (pos & 31)); }
            }
            buf[n++] = '0' + d;
        }
        return n;
    }
}

/* round the digit string to 'keep' digits (round half to even on the exact value); returns 1 on carry out */
static int __f_round(char *d, int n, int keep)
{
    int up = 0, i;
    if (keep >= n) return 0;
    if (keep < 0) return 0;
    if (d[keep] > '5') up = 1;
    else if (d[keep] == '5') {
        int nz = 0;
        for (i = keep + 1; i < n; i++) if (d[i] != '0') nz = 1;
        up = nz ? 1 : (keep > 0 && ((d[keep - 1] - '0') & 1));
    }
    if (up) {
        i = keep - 1;
        while (i >= 0 && d[i] == '9') { d[i] = '0'; i--; }
        if (i >= 0) d[i]++;
        else return 1;
    }
    return 0;
}

static char __f_dig[1700];

/* converts |x| according to conv ('f', 'e' or 'g'); returns the length written to out */
static int __f_fmt(char *out, double x, int conv, int prec, int alt, int upper)
{
    int n, ip, o = 0, i, f, expo, p, keep, carry, usef;
    if (x != x) { strcpy(out, upper ? "NAN" : "nan"); return 3; }
    if (__f_isinf(x)) { strcpy(out, upper ? "INF" : "inf"); return 3; }
    if (prec < 0) prec = 6;
    if (prec > 340) prec = 340;
    n = __f_expand(x, __f_dig, &ip);
    f = 0;
    while (f < n && __f_dig[f] == '0') f++;
    if (f == n) { __f_dig[0] = '0'; n = 1; ip = 1; f = 0; expo = 0; }
    else expo = ip - 1 - f;
    usef = conv == 'f';
    if (conv == 'g') {
        p = prec == 0 ? 1 : prec;
        if (f < n) {
            char t2[1700]; memcpy(t2, __f_dig, n);
            carry = __f_round(t2, n, f + p);
            if (carry) { expo = expo + 1; }
        }
        if (expo < -4 || expo >= p) { conv = 'e'; prec = p - 1; }
        else { usef = 1; prec = p - 1 - expo; }
    }
    if (usef) {
        keep = ip + prec;
        carry = __f_round(__f_dig, n, keep);
        if (carry) { memmove(__f_dig + 1, __f_dig, n); __f_dig[0] = '1'; n++; ip++; }
        for (i = 0; i < ip; i++) out[o++] = i < n ? __f_dig[i] : '0';
        if (prec > 0 || alt) out[o++] = '.';
        for (i = 0; i < prec; i++) out[o++] = (ip + i) < n ? __f_dig[ip + i] : '0';
    } else {
        char *d = __f_dig + f; int dn = n - f;
        if (f == n - 0 && __f_dig[0] == '0' && n == 1) { d = __f_dig; dn = 1; }
        carry = __f_round(d, dn, prec + 1);
        if (carry) { memmove(d + 1, d, dn); d[0] = '1'; dn++; expo = expo + 1; }
        out[o++] = dn > 0 ? d[0] : '0';
        if (prec > 0 || alt) out[o++] = '.';
        for (i = 1; i <= prec; i++) out[o++] = i < dn ? d[i] : '0';
        out[o++] = upper ? 'E' : 'e';
        if (expo < 0) { out[o++] = '-'; expo = -expo; } else out[o++] = '+';
        if (expo >= 100) { out[o++] = '0' + expo / 100; expo = expo % 100; }
        out[o++] = '0' + expo / 10; out[o++] = '0' + expo % 10;
    }
    if (conv == 'g' || (conv == 'e' && 0)) { }
    out[o] = 0;
    return o;
}

static int __f_strip(char *s, int len)
{
    int i, hasdot = 0, epos = len;
    for (i = 0; i < len; i++) { if (s[i] == '.') hasdot = 1; if (s[i] == 'e' || s[i] == 'E') { epos = i; break; } }
    if (!hasdot) return len;
    i = epos;
    while (i > 0 && s[i - 1] == '0') i--;
    if (i > 0 && s[i - 1] == '.') i--;
    memmove(s + i, s + epos, len - epos + 1);
    return i + (len - epos);
}

/* string to double: decimal only, correctly rounded for up to 15 significant digits and moderate exponents */
static double __f_strtod(const char *s, char **end)
{
    double v; unsigned long m = 0; int e10 = 0, nd = 0, neg = 0, seen = 0, ex = 0, eneg = 0, i;
    const char *p = s;
    while (isspace(*p)) p++;
    if (*p == '-' || *p == '+') { neg = *p == '-'; p++; }
    while (*p >= '0' && *p <= '9') { seen = 1; if (nd < 19) { m = m * 10 + (*p - '0'); if (m) nd++; } else e10++; p++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { seen = 1; if (nd < 19) { m = m * 10 + (*p - '0'); if (m) nd++; e10--; } p++; }
    }
    if (!seen) { if (end) *end = (char *)s; return 0.0; }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        if (*q == '-' || *q == '+') { eneg = *q == '-'; q++; }
        if (*q >= '0' && *q <= '9') { while (*q >= '0' && *q <= '9') { ex = ex * 10 + (*q - '0'); if (ex > 9999) ex = 9999; q++; } p = q; e10 = e10 + (eneg ? -ex : ex); }
    }
    if (end) *end = (char *)p;
    v = (double)m;
    if (e10 < 0) {
        int n = -e10; double pw = 1.0;
        while (n > 22) { v = v / 1e22; n -= 22; }
        for (i = 0; i < n; i++) pw = pw * 10.0;
        v = v / pw;
    } else if (e10 > 0) {
        int n = e10; double pw = 1.0;
        while (n > 22) { v = v * 1e22; n -= 22; }
        for (i = 0; i < n; i++) pw = pw * 10.0;
        v = v * pw;
    }
    return neg ? -v : v;
}

double strtod(const char *s, char **end) { return __f_strtod(s, end); }
double atof(const char *s) { return __f_strtod(s, NULL); }
long double _atold(const char *s) { return __f_strtod(s, NULL); }

/* ecvt/fcvt: digit strings without a decimal point */
static char __cvtbuf[400];
static char *__cvt(double v, int ndig, int *dec, int *sign, int fixed)
{
    char t[800]; int len, p, i;
    *sign = v < 0 || (v == 0 && __f_bits(v) < 0);
    if (*sign) v = -v;
    if (ndig < 0) ndig = 0;
    if (ndig > 300) ndig = 300;
    len = __f_fmt(t, v, fixed ? 'f' : 'e', fixed ? ndig : (ndig ? ndig - 1 : 0), 0, 0);
    p = 0; *dec = 0;
    if (fixed) {
        int ip = 0;
        for (i = 0; i < len && t[i] != '.'; i++) ip++;
        for (i = 0; i < len; i++) if (t[i] != '.') __cvtbuf[p++] = t[i];
        *dec = ip;
        if (ip == 1 && __cvtbuf[0] == '0') {            /* strip the leading zero, count zeros after the point */
            int z = 0;
            while (z < p - 1 && __cvtbuf[z] == '0') z++;
            if (z == p - 1 && __cvtbuf[z] == '0') { *dec = 0; }
            else { memmove(__cvtbuf, __cvtbuf + z, p - z); p -= z; *dec = 1 - z; }
        }
    } else {
        int e = 0, neg = 0;
        for (i = 0; i < len && t[i] != 'e'; i++) if (t[i] != '.') __cvtbuf[p++] = t[i];
        if (i < len) { i++; if (t[i] == '-') { neg = 1; i++; } else if (t[i] == '+') i++; for (; i < len; i++) e = e * 10 + t[i] - '0'; }
        *dec = (neg ? -e : e) + 1;
        if (v == 0) *dec = 0;
    }
    __cvtbuf[p] = 0;
    return __cvtbuf;
}

char *ecvt(double v, int ndig, int *dec, int *sign) { return __cvt(v, ndig, dec, sign, 0); }
char *fcvt(double v, int ndig, int *dec, int *sign) { return __cvt(v, ndig, dec, sign, 1); }

char *gcvt(double v, int ndig, char *buf)
{
    char t[800]; int len, neg = v < 0;
    if (neg) v = -v;
    len = __f_fmt(t, v, 'g', ndig > 0 ? ndig : 1, 0, 0);
    len = __f_strip(t, len);
    if (neg) { buf[0] = '-'; memcpy(buf + 1, t, len + 1); } else memcpy(buf, t, len + 1);
    return buf;
}

/* printf: negative values arrive as magnitudes; %g drops trailing zeros unless the # flag is used */
static int __realcvt(char *out, double x, int conv, int prec, int alt, int upper)
{
    int len = __f_fmt(out, x, conv, prec, alt, upper);
    if (conv == 'g' && !alt) len = __f_strip(out, len);
    return len;
}

static void __fp_init(void)
{
    __RealCvtVector = __realcvt;
    __ScanTodVector = __f_strtod;
}
#pragma startup __fp_init 6
