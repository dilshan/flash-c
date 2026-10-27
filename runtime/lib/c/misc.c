/*
 * File:    misc.c
 * Purpose: misc - assert, locale, multibyte, BIOS-style and DOS-style helpers
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
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

/* Prints the message of a failed assert() and aborts. */
void __assertfail(const char *fmt, const char *expr, const char *file, int line)
{
    fprintf(stderr, fmt, expr, file, line);
    abort();
}

/* ---- locale: only the "C" locale exists */
static struct lconv __lc = {".", "",  "",  "",  "",  "",  "",  "",  "",
                            "",  127, 127, 127, 127, 127, 127, 127, 127};

/* Selects the locale. Returns the locale name, or NULL. */
char *setlocale(int category, const char *locale)
{
    if (!locale || !*locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX")) {
        return "C";
    }
    return NULL;
}

/* Gets the numeric formatting of the locale. Returns a pointer to a structure. */
struct lconv *localeconv(void)
{
    return &__lc;
}

/* ---- multibyte: single-byte characters */
int mblen(const char *s, size_t n)
{
    if (!s) {
        return 0;
    }
    if (!*s) {
        return 0;
    }
    return n ? 1 : -1;
}

/*
 * Converts a multibyte character to a wide character. Returns the number of bytes used, 0 for
 * the null character, or -1.
 */
int mbtowc(wchar_t *wc, const char *s, size_t n)
{
    if (!s) {
        return 0;
    }
    if (!n) {
        return -1;
    }
    if (wc) {
        *wc = (unsigned char)*s;
    }
    return *s ? 1 : 0;
}

/*
 * Converts a wide character to a multibyte character. Returns the number of bytes stored, or
 * -1.
 */
int wctomb(char *s, wchar_t wc)
{
    if (!s) {
        return 0;
    }
    *s = (char)wc;
    return 1;
}

/*
 * Converts a multibyte string to a wide-character string. Returns the number of wide
 * characters stored (not counting the null), or (size_t)-1 on an invalid sequence.
 */
size_t mbstowcs(wchar_t *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) {
        if (dst) {
            dst[i] = (unsigned char)src[i];
        }
    }
    if (dst && i < n) {
        dst[i] = 0;
    }
    return i;
}

/*
 * Converts a wide-character string to a multibyte string. Returns the number of bytes stored
 * (not counting the null), or (size_t)-1.
 */
size_t wcstombs(char *dst, const wchar_t *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) {
        if (dst) {
            dst[i] = (char)src[i];
        }
    }
    if (dst && i < n) {
        dst[i] = 0;
    }
    return i;
}

/* ---- delays */
void delay(unsigned ms)
{
    long ts[2];
    ts[0] = ms / 1000;
    ts[1] = (long)(ms % 1000) * 1000000L;
    while (__syscall(SYS_nanosleep, ts, ts) == -4)
        ;
}

/* Waits for a number of seconds. Returns 0, or the seconds left if the wait was interrupted. */
unsigned sleep(unsigned sec)
{
    long ts[2];
    ts[0] = sec;
    ts[1] = 0;
    if (__syscall(SYS_nanosleep, ts, ts) == -4) {
        return (unsigned)ts[0];
    }
    return 0;
}

/* Turns the speaker on at a frequency. */
void sound(unsigned freq)
{
    fputs("\a", stderr);
}

/* Turns the speaker off. */
void nosound(void)
{
}

/* Gets the free space of the disk. */
void getdfree(unsigned char drive, struct dfree *t)
{
    long sf[15]; /* struct statfs */
    t->df_avail = t->df_total = 0xFFFFU;
    t->df_bsec = 512;
    t->df_sclus = 8;
    if (__syscall(SYS_statfs, "/", sf) == 0) {
        long bsize = sf[1], total = sf[2], avail = sf[4];
        long per = bsize / 512; /* sectors per block */
        if (per > 0) {
            t->df_sclus = (unsigned)per;
        }
        t->df_total = total > 0xFFFF ? 0xFFFF : (unsigned)total;
        t->df_avail = avail > 0xFFFF ? 0xFFFF : (unsigned)avail;
    }
}

/* ---- BIOS-style */
long biostime(int cmd, long newtime)
{
    long ts[2];
    if (cmd) {
        return 0;
    }
    __syscall(SYS_clock_gettime, 0, ts);
    return (ts[0] % 86400L) * 182L / 10 +
           ts[1] / 54925400L; /* 18.2 ticks per second since midnight */
}

/* Returns the equipment list. */
int biosequip(void)
{
    return 0x0021;
}

/* Returns the memory size in kilobytes. */
int biosmemory(void)
{
    return 640;
}
