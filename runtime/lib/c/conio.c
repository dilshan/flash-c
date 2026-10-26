/*
 * File:    conio.c
 * Purpose: conio - console I/O on an ANSI terminal: windows, colours, cursor
 *          control, unbuffered keyboard input. Extended keys are returned the way
 *          the DOS console does: first 0, then the scan code.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <bios.h>
#include <_fcrt.h>

int directvideo = 1;

static int __wl = 1, __wt = 1, __wr = 80, __wb = 25;   /* window, 1-based */
static int __cx = 1, __cy = 1;                         /* cursor inside the window, 1-based */
static int __attr = 0x07, __norm = 0x07;
static int __cmode = C80;
static int __cinit;
static int __unget = -1;
static char __tio_save[64];
static int __tio_raw;

static void __out(const char *s, int n) { long r; while (n > 0) { r = __syscall(SYS_write, 1, s, n); if (r <= 0) break; s += r; n -= (int)r; } }
static void __outs(const char *s) { __out(s, (int)strlen(s)); }

static void __num(char *p, int v)
{
    char t[12]; int n = 0, i = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) p[i++] = t[--n];
    p[i] = 0;
}

static void __csi2(int a, int b, char fin)
{
    char buf[32]; int n = 0;
    buf[n++] = 27; buf[n++] = '[';
    __num(buf + n, a); n += (int)strlen(buf + n);
    buf[n++] = ';';
    __num(buf + n, b); n += (int)strlen(buf + n);
    buf[n++] = fin;
    __out(buf, n);
}

static void __size(void)
{
    short ws[4];
    if (__syscall(SYS_ioctl, 1, 0x5413, ws) == 0 && ws[0] > 0 && ws[1] > 0) { __wb = ws[0]; __wr = ws[1]; }
    else { __wb = 25; __wr = 80; }
    if (__wb > 255) __wb = 255;
    if (__wr > 255) __wr = 255;
}

static void __init(void)
{
    if (__cinit) return;
    __cinit = 1;
    __size();
    __wl = __wt = 1;
}

static void __setattr(void)
{
    static const char ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };       /* DOS colour order to ANSI */
    int fg = __attr & 15, bg = (__attr >> 4) & 7;
    char b[48]; int n = 0;
    b[n++] = 27; b[n++] = '[';
    b[n++] = '0'; b[n++] = ';';
    __num(b + n, (fg & 8 ? 90 : 30) + ansi[fg & 7]); n += (int)strlen(b + n);
    b[n++] = ';';
    __num(b + n, 40 + ansi[bg]); n += (int)strlen(b + n);
    if (__attr & 0x80) { b[n++] = ';'; b[n++] = '5'; }
    b[n++] = 'm';
    __out(b, n);
}

static void __move(void) { __csi2(__wt + __cy - 1, __wl + __cx - 1, 'H'); }

void window(int l, int t, int r, int b)
{
    __init();
    if (l < 1 || t < 1 || r > __wr || b > __wb || l > r || t > b) { if (r > 255 || b > 255) return; }
    if (l < 1 || t < 1 || l > r || t > b) return;
    __wl = l; __wt = t; __wr = r; __wb = b; __cx = __cy = 1;
    __move();
}

void gotoxy(int x, int y)
{
    __init();
    if (x < 1 || y < 1 || x > __wr - __wl + 1 || y > __wb - __wt + 1) return;
    __cx = x; __cy = y;
    __move();
}

int wherex(void) { __init(); return __cx; }
int wherey(void) { __init(); return __cy; }

void textattr(int a) { __init(); __attr = a; __setattr(); }
void textcolor(int c) { __init(); __attr = (__attr & 0x70) | (c & 0x8F); __setattr(); }
void textbackground(int c) { __init(); __attr = (__attr & 0x8F) | ((c & 7) << 4); __setattr(); }
void highvideo(void) { textattr(__attr | 8); }
void lowvideo(void) { textattr(__attr & ~8); }
void normvideo(void) { textattr(__norm); }

void textmode(int m)
{
    __init();
    __cmode = m == LASTMODE ? __cmode : m;
    __size();
    __wl = __wt = 1; __attr = __norm;
    __setattr();
}

void clrscr(void)
{
    int y;
    __init();
    if (__wl == 1 && __wt == 1 && __wr >= 80 && 0) { }
    for (y = __wt; y <= __wb; y++) {
        __csi2(y, __wl, 'H');
        if (__wl == 1 && __wr >= 80) __outs("\033[K");
        else { int i; for (i = __wl; i <= __wr; i++) __out(" ", 1); }
    }
    __cx = __cy = 1;
    __move();
}

void clreol(void)
{
    int i;
    __init();
    for (i = __cx; i <= __wr - __wl + 1; i++) __out(" ", 1);
    __move();
}

static void __scroll(int up)
{
    char b[40]; int n = 0;
    b[n++] = 27; b[n++] = '[';
    __num(b + n, __wt); n += (int)strlen(b + n); b[n++] = ';';
    __num(b + n, __wb); n += (int)strlen(b + n); b[n++] = 'r';
    __out(b, n);
    __csi2(up ? __wb : __wt, 1, 'H');
    __outs(up ? "\n" : "\033M");
    __outs("\033[r");
}

void insline(void)
{
    __init();
    __csi2(__wt + __cy - 1, __wl, 'H');
    __outs("\033[L");                                     /* inserts inside the whole line; good for full-width windows */
    __move();
}

void delline(void)
{
    __init();
    __csi2(__wt + __cy - 1, __wl, 'H');
    __outs("\033[M");
    __move();
}

int putch(int c)
{
    char ch = (char)c;
    __init();
    if (ch == '\n') {
        if (__cy >= __wb - __wt + 1) { __scroll(1); __cy = __wb - __wt + 1; }
        else __cy++;
        __move();
        return c;
    }
    if (ch == '\r') { __cx = 1; __move(); return c; }
    if (ch == '\b') { if (__cx > 1) __cx--; __move(); return c; }
    if (ch == '\a') { __out("\a", 1); return c; }
    if (ch == '\t') { int n = 8 - (__cx - 1) % 8; while (n-- > 0) putch(' '); return c; }
    if (__cx > __wr - __wl + 1) {                        /* wrap */
        __cx = 1;
        if (__cy >= __wb - __wt + 1) __scroll(1); else __cy++;
        __move();
    }
    __out(&ch, 1);
    __cx++;
    return c;
}

int cputs(const char *s)
{
    int c = 0;
    while (*s) { putch(*s++); c = 1; }
    return c ? s[-1] : 0;
}

int cprintf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    cputs(buf);
    return n;
}

void gettextinfo(struct text_info *ti)
{
    __init();
    ti->winleft = __wl; ti->wintop = __wt; ti->winright = __wr; ti->winbottom = __wb;
    ti->attribute = __attr; ti->normattr = __norm; ti->currmode = __cmode;
    ti->screenheight = __wb; ti->screenwidth = __wr;
    ti->curx = __cx; ti->cury = __cy;
}

void _setcursortype(int t)
{
    __outs(t == _NOCURSOR ? "\033[?25l" : "\033[?25h");
    if (t == _SOLIDCURSOR) __outs("\033[2 q"); else if (t == _NORMALCURSOR) __outs("\033[4 q");
}

/* ---- keyboard: the terminal is put in non-canonical mode for the duration of a read */
static int __raw_on(void)
{
    char t[64];
    int i;
    if (__syscall(SYS_ioctl, 0, 0x5401, __tio_save) < 0) return 0;
    memcpy(t, __tio_save, 64);
    *(unsigned int *)(t + 12) &= ~(2u | 8u | 16u | 32u | 1u);          /* ~ICANON ~ECHO (c_lflag at offset 12) */
    t[17 + 6] = 1; t[17 + 5] = 0;                                      /* VMIN = 1, VTIME = 0 */
    (void)i;
    __syscall(SYS_ioctl, 0, 0x5402, t);
    __tio_raw = 1;
    return 1;
}

static void __raw_off(void)
{
    if (__tio_raw) __syscall(SYS_ioctl, 0, 0x5402, __tio_save);
    __tio_raw = 0;
}

static int __rd(int timeout_ms)
{
    unsigned char c;
    long pf[2];
    if (timeout_ms >= 0) {
        pf[0] = 0 | ((long)1 << 32);                                 /* fd 0, events POLLIN */
        if (__syscall(SYS_poll, pf, 1, timeout_ms) <= 0) return -1;
    }
    if (__syscall(SYS_read, 0, &c, 1) != 1) return -2;
    return c;
}

/* scan codes for the common escape sequences */
static int __keycode(void)
{
    int c = __rd(-1), d, e;
    static const struct { char fin; char sc; } tab[] = {
        { 'A', 72 }, { 'B', 80 }, { 'C', 77 }, { 'D', 75 }, { 'H', 71 }, { 'F', 79 }
    };
    int i;
    if (c == -2) return -2;
    if (c != 27) return c;
    d = __rd(40);
    if (d < 0) return 27;
    if (d == 'O') { e = __rd(40); d = '['; if (e >= 'P' && e <= 'S') return 0x100 | (59 + e - 'P'); goto fin; }
    if (d != '[') return 27;
    e = __rd(40);
fin:
    for (i = 0; i < 6; i++) if (e == tab[i].fin) return 0x100 | tab[i].sc;
    if (e >= '0' && e <= '9') {
        int num = e - '0';
        while ((e = __rd(40)) >= '0' && e <= '9') num = num * 10 + e - '0';
        if (e == ';') { while ((e = __rd(40)) >= '0' && e <= '9') ; }       /* modifiers are ignored */
        if (e == '~') {
            switch (num) {
            case 1: case 7: return 0x100 | 71;
            case 4: case 8: return 0x100 | 79;
            case 2: return 0x100 | 82;
            case 3: return 0x100 | 83;
            case 5: return 0x100 | 73;
            case 6: return 0x100 | 81;
            case 11: case 12: case 13: case 14: case 15: return 0x100 | (59 + num - 11);
            case 17: case 18: case 19: case 20: case 21: return 0x100 | (64 + num - 17);
            case 23: case 24: return 0x100 | (133 + num - 23);
            }
        }
    }
    return 27;
}

static int __pending = -1;                                          /* second half of an extended key */

int getch(void)
{
    int k;
    if (__unget >= 0) { k = __unget; __unget = -1; return k; }
    if (__pending >= 0) { k = __pending; __pending = -1; return k; }
    fflush(stdout);
    if (!__raw_on()) { k = getchar(); return k; }
    k = __keycode();
    __raw_off();
    if (k < 0) return -1;
    if (k & 0x100) { __pending = k & 0xFF; return 0; }
    if (k == 127) k = 8;
    if (k == '\n') k = '\r';
    return k;
}

int getche(void)
{
    int c = getch();
    if (c > 0) putch(c);
    return c;
}

int ungetch(int c) { if (__unget >= 0) return -1; __unget = c; return c; }

int kbhit(void)
{
    long pf[2];
    if (__unget >= 0 || __pending >= 0) return 1;
    pf[0] = 0 | ((long)1 << 32);
    return __syscall(SYS_poll, pf, 1, 0) > 0;
}

int bioskey(int cmd)
{
    int k;
    if (cmd == 1) return kbhit();
    if (cmd == 2) return 0;
    k = getch();
    if (k == 0) return getch() << 8;
    return k;
}

char *cgets(char *buf)
{
    int max = (unsigned char)buf[0], n = 0, c;
    for (;;) {
        c = getch();
        if (c == '\r' || c == '\n' || c < 0) break;
        if (c == 8) { if (n) { n--; putch(8); putch(' '); putch(8); } continue; }
        if (c == 0) { getch(); continue; }
        if (n < max) { buf[2 + n++] = c; putch(c); }
    }
    buf[2 + n] = 0; buf[1] = n;
    putch('\r'); putch('\n');
    return buf + 2;
}

int cscanf(const char *fmt, ...)
{
    char line[256], *p;
    va_list ap;
    line[0] = (char)254;
    p = cgets(line);
    va_start(ap, fmt);
    return vsscanf(p, fmt, ap);
}

char *getpass(const char *prompt)
{
    static char pw[128];
    int n = 0, c;
    cputs(prompt);
    while ((c = getch()) != '\r' && c != '\n' && c >= 0) {
        if (c == 8) { if (n) n--; continue; }
        if (c == 0) { getch(); continue; }
        if (n < 126) pw[n++] = c;
    }
    pw[n] = 0;
    putch('\r'); putch('\n');
    return pw;
}
