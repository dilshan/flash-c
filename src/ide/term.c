/*
 * File:    term.c
 * Purpose: Flash C IDE - terminal layer
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "term.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>

int SW = 80, SH = 25;
MouseState ms;
static uint32_t *cur_ch, *prv_ch;
static uint8_t *cur_at, *prv_at;
static int cap_w, cap_h;
static int cur_x, cur_y, cur_vis;
static struct termios orig_tio;
static int tio_saved, active;
static volatile sig_atomic_t resized;
static int truecolor;
static int forced_lines;

static const uint8_t vga[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
};
static const uint8_t cga2ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

void set_truecolor(int on) { truecolor = on; }
void set_screen_lines(int lines) { forced_lines = lines; resized = 1; }

static void on_winch(int s) { (void)s; resized = 1; }

/* restore the terminal if the process is killed or crashes, so the shell stays usable */
static void on_fatal(int sig)
{
    static const char reset[] = "\033[?2004l\033[?1006l\033[?1002l\033[?1000l\033[0m\033[?25h\033[?1049l";
    if (write(1, reset, sizeof reset - 1) < 0) { }
    if (tio_saved) tcsetattr(0, TCSAFLUSH, &orig_tio);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void alloc_screen(void)
{
    int w = SW, h = SH;
    free(cur_ch); free(prv_ch); free(cur_at); free(prv_at);
    cur_ch = calloc(w * h, 4); prv_ch = calloc(w * h, 4);
    cur_at = calloc(w * h, 1); prv_at = calloc(w * h, 1);
    cap_w = w; cap_h = h;
    for (int i = 0; i < w * h; i++) { cur_ch[i] = ' '; prv_ch[i] = 0xFFFFFFFF; }
}

static void query_size(void)
{
    struct winsize ws;
    int w = 80, h = 25;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) { w = ws.ws_col; h = ws.ws_row; }
    if (forced_lines && forced_lines < h) h = forced_lines;
    if (w < 80) w = 80;
    if (h < 25) h = 25;
    if (w > 400) w = 400;
    if (h > 200) h = 200;
    if (w != SW || h != SH || !cur_ch) { SW = w; SH = h; alloc_screen(); }
}

static void out(const char *s) { fputs(s, stdout); }

void term_init(void)
{
    const char *ct = getenv("COLORTERM");
    truecolor = ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"));
    if (!tio_saved) { tcgetattr(0, &orig_tio); tio_saved = 1; }
    struct termios t = orig_tio;
    t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON | INLCR | IGNCR);
    t.c_oflag &= ~OPOST;
    t.c_cflag |= CS8;
    t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch; sigaction(SIGWINCH, &sa, NULL);
    { static const int fatal[] = { SIGSEGV, SIGBUS, SIGFPE, SIGABRT, SIGILL, SIGTERM, SIGHUP, SIGQUIT };
      struct sigaction sf; memset(&sf, 0, sizeof sf); sf.sa_handler = on_fatal;
      for (unsigned i = 0; i < sizeof fatal / sizeof fatal[0]; i++) sigaction(fatal[i], &sf, NULL); }
    signal(SIGPIPE, SIG_IGN);
    out("\033[?1049h\033[?25l\033[?1000h\033[?1002h\033[?1006h\033[?2004h\033[2J");
    fflush(stdout);
    active = 1;
    query_size();
    scr_invalidate();
}

void term_suspend(void)
{
    if (!active) return;
    out("\033[?2004l\033[?1006l\033[?1002l\033[?1000l\033[0m\033[?25h\033[?1049l");
    fflush(stdout);
    tcsetattr(0, TCSAFLUSH, &orig_tio);
    active = 0;
}

void term_resume(void)
{
    if (active) return;
    term_init();
}

void term_done(void) { term_suspend(); }

/* ---------- screen ---------- */
void scr_invalidate(void) { for (int i = 0; i < cap_w * cap_h; i++) prv_ch[i] = 0xFFFFFFFF; }

void scr_clear(uint8_t attr) { for (int i = 0; i < SW * SH; i++) { cur_ch[i] = ' '; cur_at[i] = attr; } }

void scr_put(int x, int y, uint32_t ch, uint8_t attr)
{
    if (x < 0 || y < 0 || x >= SW || y >= SH) return;
    cur_ch[y * SW + x] = ch; cur_at[y * SW + x] = attr;
}
uint32_t scr_getch(int x, int y) { return (x < 0 || y < 0 || x >= SW || y >= SH) ? ' ' : cur_ch[y * SW + x]; }
uint8_t scr_getattr(int x, int y) { return (x < 0 || y < 0 || x >= SW || y >= SH) ? 0 : cur_at[y * SW + x]; }

static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s;
    if (c < 0x80) { (*p)++; return c; }
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (!n) { (*p)++; return '?'; }
    c &= (0x3F >> n);
    for (int i = 1; i <= n; i++) { if ((s[i] & 0xC0) != 0x80) { (*p)++; return '?'; } c = (c << 6) | (s[i] & 0x3F); }
    *p += n + 1;
    return c;
}

int scr_str(int x, int y, const char *s, uint8_t attr)
{
    int n = 0;
    while (*s) { scr_put(x + n, y, utf8_next(&s), attr); n++; }
    return n;
}
int scr_strn(int x, int y, const char *s, int max, uint8_t attr)
{
    int n = 0;
    while (*s && n < max) { scr_put(x + n, y, utf8_next(&s), attr); n++; }
    return n;
}
int hotlen(const char *s)
{
    int n = 0;
    while (*s) { if (*s == '~') { s++; continue; } utf8_next(&s); n++; }
    return n;
}
int scr_hotstr(int x, int y, const char *s, uint8_t attr, uint8_t hot)
{
    int n = 0, in = 0;
    while (*s) {
        if (*s == '~') { in = !in; s++; continue; }
        scr_put(x + n, y, utf8_next(&s), in ? hot : attr); n++;
    }
    return n;
}
void scr_fill(int x, int y, int w, int h, uint32_t ch, uint8_t attr)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) scr_put(x + i, y + j, ch, attr);
}
void scr_attr(int x, int y, int w, int h, uint8_t attr)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
        if (x + i >= 0 && y + j >= 0 && x + i < SW && y + j < SH) cur_at[(y + j) * SW + x + i] = attr;
}
void scr_shadow(int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        int px = x + i, py = y + j;
        if (px < 0 || py < 0 || px >= SW || py >= SH) continue;
        cur_at[py * SW + px] = 0x08;
    }
}
void scr_cursor(int x, int y, int vis) { cur_x = x; cur_y = y; cur_vis = vis; }

void scr_box(int x, int y, int w, int h, int style, uint8_t attr)
{
    static const uint32_t S[6] = { 0x250C, 0x2510, 0x2514, 0x2518, 0x2500, 0x2502 };
    static const uint32_t D[6] = { 0x2554, 0x2557, 0x255A, 0x255D, 0x2550, 0x2551 };
    const uint32_t *c = style == BOX_DOUBLE ? D : S;
    scr_put(x, y, c[0], attr); scr_put(x + w - 1, y, c[1], attr);
    scr_put(x, y + h - 1, c[2], attr); scr_put(x + w - 1, y + h - 1, c[3], attr);
    for (int i = 1; i < w - 1; i++) { scr_put(x + i, y, c[4], attr); scr_put(x + i, y + h - 1, c[4], attr); }
    for (int j = 1; j < h - 1; j++) { scr_put(x, y + j, c[5], attr); scr_put(x + w - 1, y + j, c[5], attr); }
}

/* ---------- flushing ---------- */
static char *ob; static size_t obn, obcap;
static void oc(char c) { if (obn + 1 >= obcap) { obcap = obcap ? obcap * 2 : 65536; ob = realloc(ob, obcap); } ob[obn++] = c; }
static void os(const char *s) { while (*s) oc(*s++); }
static void ou(uint32_t c)
{
    if (c < 0x80) oc(c);
    else if (c < 0x800) { oc(0xC0 | (c >> 6)); oc(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { oc(0xE0 | (c >> 12)); oc(0x80 | ((c >> 6) & 0x3F)); oc(0x80 | (c & 0x3F)); }
    else { oc(0xF0 | (c >> 18)); oc(0x80 | ((c >> 12) & 0x3F)); oc(0x80 | ((c >> 6) & 0x3F)); oc(0x80 | (c & 0x3F)); }
}
static void on(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); os(b); }

static void sgr(uint8_t a)
{
    int fg = a & 15, bg = (a >> 4) & 7;
    os("\033[0");
    if (truecolor) {
        os(";38;2;"); on(vga[fg][0]); oc(';'); on(vga[fg][1]); oc(';'); on(vga[fg][2]);
        os(";48;2;"); on(vga[bg][0]); oc(';'); on(vga[bg][1]); oc(';'); on(vga[bg][2]);
    } else {
        int f = cga2ansi[fg & 7], b = cga2ansi[bg];
        oc(';'); on((fg & 8 ? 90 : 30) + f);
        oc(';'); on(40 + b);
    }
    oc('m');
}

void scr_flush(void)
{
    if (resized) { resized = 0; query_size(); scr_invalidate(); }
    obn = 0;
    os("\033[?25l");
    int last_attr = -1, px = -1, py = -1;
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            int i = y * SW + x;
            if (cur_ch[i] == prv_ch[i] && cur_at[i] == prv_at[i]) continue;
            if (py != y || px != x) { os("\033["); on(y + 1); oc(';'); on(x + 1); oc('H'); }
            if (cur_at[i] != last_attr) { sgr(cur_at[i]); last_attr = cur_at[i]; }
            ou(cur_ch[i]);
            prv_ch[i] = cur_ch[i]; prv_at[i] = cur_at[i];
            px = x + 1; py = y;
        }
    }
    if (cur_vis) {
        os("\033["); on(cur_y + 1); oc(';'); on(cur_x + 1); os("H\033[?25h");
    }
    if (obn > 12 || cur_vis) { fwrite(ob, 1, obn, stdout); fflush(stdout); }
}

/* ---------- input ---------- */
static unsigned char inbuf[256]; static int inlen, inpos;

static int fill(int timeout)
{
    if (inpos < inlen) return 1;
    struct pollfd p = { 0, POLLIN, 0 };
    int r = poll(&p, 1, timeout);
    if (r <= 0) return 0;
    int n = read(0, inbuf, sizeof inbuf);
    if (n <= 0) return 0;
    inlen = n; inpos = 0;
    return 1;
}
static int getb(int timeout) { if (!fill(timeout)) return -1; return inbuf[inpos++]; }

static int mods_from(int m)
{
    int k = 0; m -= 1;
    if (m & 1) k |= K_SHIFT;
    if (m & 2) k |= K_ALT;
    if (m & 4) k |= K_CTRL;
    return k;
}

static int parse_csi(void)
{
    int params[8] = {0}, np = 0, c, have = 0, priv = 0;
    for (;;) {
        c = getb(50);
        if (c < 0) return KEY_ESC;
        if (c == '<' || c == '?') { priv = c; continue; }
        if (c >= '0' && c <= '9') { params[np] = params[np] * 10 + (c - '0'); have = 1; continue; }
        if (c == ';') { if (np < 7) np++; have = 0; continue; }
        break;
    }
    int n = np + (have || np ? 1 : 0);
    if (priv == '<' && (c == 'M' || c == 'm')) {
        int b = params[0];
        ms.x = params[1] - 1; ms.y = params[2] - 1;
        ms.mods = (b & 4 ? K_SHIFT : 0) | (b & 8 ? K_ALT : 0) | (b & 16 ? K_CTRL : 0);
        if (b & 64) ms.ev = (b & 1) ? MS_WHEEL_DOWN : MS_WHEEL_UP;
        else if (b & 32) { ms.ev = MS_DRAG; ms.btn = b & 3; }
        else if (c == 'M') { ms.ev = MS_PRESS; ms.btn = b & 3; }
        else { ms.ev = MS_RELEASE; ms.btn = b & 3; }
        return KEY_MOUSE;
    }
    int mod = n >= 2 ? mods_from(params[1]) : 0;
    switch (c) {
    case 'A': return KEY_UP | mod;   case 'B': return KEY_DOWN | mod;
    case 'C': return KEY_RIGHT | mod; case 'D': return KEY_LEFT | mod;
    case 'H': return KEY_HOME | mod; case 'F': return KEY_END | mod;
    case 'P': return KEY_F1 | mod;   case 'Q': return KEY_F2 | mod;
    case 'R': return KEY_F3 | mod;   case 'S': return KEY_F4 | mod;
    case 'Z': return KEY_TAB | K_SHIFT;
    case '~': {
        int k = 0;
        switch (params[0]) {
        case 1: case 7: k = KEY_HOME; break; case 2: k = KEY_INS; break; case 3: k = KEY_DEL; break;
        case 4: case 8: k = KEY_END; break; case 5: k = KEY_PGUP; break; case 6: k = KEY_PGDN; break;
        case 11: k = KEY_F1; break; case 12: k = KEY_F2; break; case 13: k = KEY_F3; break;
        case 14: k = KEY_F4; break; case 15: k = KEY_F5; break; case 17: k = KEY_F6; break;
        case 18: k = KEY_F7; break; case 19: k = KEY_F8; break; case 20: k = KEY_F9; break;
        case 21: k = KEY_F10; break; case 23: k = KEY_F11; break; case 24: k = KEY_F12; break;
        case 200: return KEY_PASTE_BEGIN; case 201: return KEY_PASTE_END;
        default: return KEY_NONE;
        }
        return k | mod;
    }
    case '[': { /* linux console: ESC [ [ A.. */
        int d = getb(20);
        if (d >= 'A' && d <= 'E') return KEY_F1 + (d - 'A');
        return KEY_NONE;
    }
    }
    return KEY_NONE;
}

int key_get(int timeout)
{
    if (resized) { resized = 0; query_size(); scr_invalidate(); return KEY_RESIZE; }
    int c = getb(timeout);
    if (c < 0) return resized ? (resized = 0, query_size(), scr_invalidate(), KEY_RESIZE) : KEY_IDLE;
    if (c == 27) {
        int d = getb(25);
        if (d < 0) return KEY_ESC;
        if (d == '[') return parse_csi();
        if (d == 'O') {
            int e = getb(25);
            switch (e) {
            case 'P': return KEY_F1; case 'Q': return KEY_F2; case 'R': return KEY_F3; case 'S': return KEY_F4;
            case 'A': return KEY_UP; case 'B': return KEY_DOWN; case 'C': return KEY_RIGHT; case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME; case 'F': return KEY_END;
            }
            return KEY_NONE;
        }
        if (d == 27) return KEY_ESC;
        if (d == 127 || d == 8) return K_ALT | KEY_BKSP;
        if (d >= 'A' && d <= 'Z') return K_ALT | K_SHIFT | (d + 32);
        if (d >= 1 && d <= 26 && d != 9 && d != 13) return K_ALT | K_CTRL | (d + 96);
        if (d == 13 || d == 10) return K_ALT | KEY_ENTER;
        if (d == 9) return K_ALT | KEY_TAB;
        return K_ALT | d;
    }
    if (c == 13 || c == 10) return KEY_ENTER;
    if (c == 9) return KEY_TAB;
    if (c == 127 || c == 8) return KEY_BKSP;
    if (c == 0) return K_CTRL | ' ';
    if (c < 32) return K_CTRL | (c + 96);
    if (c >= 0xC0) {   /* UTF-8 lead byte: swallow the sequence and return a replacement */
        int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        while (n--) getb(10);
        return '?';
    }
    return c;
}
