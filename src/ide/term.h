/* Flash C IDE - terminal layer: screen buffer, keyboard, mouse */
#ifndef TERM_H
#define TERM_H
#include <stdint.h>

/* key codes: low 16 bits = base key, high bits = modifiers */
#define K_SHIFT  0x10000
#define K_CTRL   0x20000
#define K_ALT    0x40000
#define KMASK    0xFFFF
enum {
    KEY_NONE = 0, KEY_ESC = 27, KEY_TAB = 9, KEY_ENTER = 13, KEY_BKSP = 127,
    KEY_UP = 0x1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN,
    KEY_INS, KEY_DEL,
    KEY_F1 = 0x1100, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_MOUSE = 0x1200, KEY_RESIZE, KEY_PASTE_BEGIN, KEY_PASTE_END, KEY_IDLE
};
#define ALT(c)  (K_ALT | (c))
#undef CTRL
#define CTRL(c) (K_CTRL | (c))

typedef struct { int x, y, btn, ev, mods; } MouseState;
enum { MS_PRESS = 1, MS_RELEASE, MS_DRAG, MS_WHEEL_UP, MS_WHEEL_DOWN, MS_MOVE };
extern MouseState ms;

extern int SW, SH;      /* screen size */

void term_init(void);
void term_done(void);
void term_suspend(void);    /* leave the IDE screen (for running programs / user screen) */
void term_resume(void);
int  key_get(int timeout_ms);   /* returns KEY_IDLE on timeout */

void scr_clear(uint8_t attr);
void scr_put(int x, int y, uint32_t ch, uint8_t attr);
int  scr_str(int x, int y, const char *s, uint8_t attr);          /* returns chars drawn */
int  scr_strn(int x, int y, const char *s, int n, uint8_t attr);
int  scr_hotstr(int x, int y, const char *s, uint8_t attr, uint8_t hotattr); /* ~x~ marks hot letter */
void scr_fill(int x, int y, int w, int h, uint32_t ch, uint8_t attr);
void scr_attr(int x, int y, int w, int h, uint8_t attr);
void scr_shadow(int x, int y, int w, int h);                       /* darken region */
uint32_t scr_getch(int x, int y);
uint8_t  scr_getattr(int x, int y);
void scr_cursor(int x, int y, int visible);
void scr_flush(void);
void scr_invalidate(void);
int  hotlen(const char *s);          /* visible length of a ~hot~ string */
void set_truecolor(int on);
void set_screen_lines(int lines);    /* 0 = auto, 25 / 43 / 50 */

/* box drawing */
enum { BOX_SINGLE, BOX_DOUBLE };
void scr_box(int x, int y, int w, int h, int style, uint8_t attr);
#endif
