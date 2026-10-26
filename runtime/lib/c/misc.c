/* misc - assert, locale, multibyte, BIOS-style and DOS-style helpers */
#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <bios.h>
#include <dos.h>
#include <time.h>
#include <_fcrt.h>

int _osmajor = 5, _osminor = 0;

void __assertfail(const char *fmt, const char *expr, const char *file, int line)
{
    fprintf(stderr, fmt, expr, file, line);
    abort();
}

/* ---- locale: only the "C" locale exists */
static struct lconv __lc = { ".", "", "", "", "", "", "", "", "", "", 127, 127, 127, 127, 127, 127, 127, 127 };

char *setlocale(int category, const char *locale)
{
    if (!locale || !*locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX")) return "C";
    return NULL;
}

struct lconv *localeconv(void) { return &__lc; }

/* ---- multibyte: single-byte characters */
int mblen(const char *s, size_t n) { if (!s) return 0; if (!*s) return 0; return n ? 1 : -1; }
int mbtowc(wchar_t *wc, const char *s, size_t n)
{
    if (!s) return 0;
    if (!n) return -1;
    if (wc) *wc = (unsigned char)*s;
    return *s ? 1 : 0;
}
int wctomb(char *s, wchar_t wc) { if (!s) return 0; *s = (char)wc; return 1; }
size_t mbstowcs(wchar_t *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) if (dst) dst[i] = (unsigned char)src[i];
    if (dst && i < n) dst[i] = 0;
    return i;
}
size_t wcstombs(char *dst, const wchar_t *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) if (dst) dst[i] = (char)src[i];
    if (dst && i < n) dst[i] = 0;
    return i;
}

/* ---- delays */
void delay(unsigned ms)
{
    long ts[2];
    ts[0] = ms / 1000; ts[1] = (long)(ms % 1000) * 1000000L;
    while (__syscall(SYS_nanosleep, ts, ts) == -4) ;
}

unsigned sleep(unsigned sec)
{
    long ts[2];
    ts[0] = sec; ts[1] = 0;
    if (__syscall(SYS_nanosleep, ts, ts) == -4) return (unsigned)ts[0];
    return 0;
}

void sound(unsigned freq) { fputs("\a", stderr); }
void nosound(void) { }

void getdfree(unsigned char drive, struct dfree *t)
{
    long sf[15];                                  /* struct statfs */
    t->df_avail = t->df_total = 0xFFFFU; t->df_bsec = 512; t->df_sclus = 8;
    if (__syscall(SYS_statfs, "/", sf) == 0) {
        long bsize = sf[1], total = sf[2], avail = sf[4];
        long per = bsize / 512;                   /* sectors per block */
        if (per > 0) t->df_sclus = (unsigned)per;
        t->df_total = total > 0xFFFF ? 0xFFFF : (unsigned)total;
        t->df_avail = avail > 0xFFFF ? 0xFFFF : (unsigned)avail;
    }
}

/* ---- BIOS-style */
long biostime(int cmd, long newtime)
{
    long ts[2];
    if (cmd) return 0;
    __syscall(SYS_clock_gettime, 0, ts);
    return (ts[0] % 86400L) * 182L / 10 + ts[1] / 54925400L;       /* 18.2 ticks per second since midnight */
}

int biosequip(void) { return 0x0021; }
int biosmemory(void) { return 640; }
