/* Flash C IDE - dialog engine, message boxes, file dialog */
#include "ide.h"
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

static char hint_buf[300];
void status_hint(const char *s) { snprintf(hint_buf, sizeof hint_buf, "%s", s ? s : ""); }
const char *status_hint_get(void) { return hint_buf; }

/* ---------- item creation ---------- */
void dlg_init(Dialog *d, const char *title) { memset(d, 0, sizeof *d); d->title = title; }

DItem *dlg_add(Dialog *d, DItem it)
{
    if (d->n >= 48) return &d->it[47];
    d->it[d->n] = it;
    return &d->it[d->n++];
}
DItem *dlg_static(Dialog *d, int x, int y, const char *text)
{
    DItem it = { .type = D_STATIC, .x = x, .y = y, .text = text };
    return dlg_add(d, it);
}
DItem *dlg_input(Dialog *d, int x, int y, int w, const char *label, char *buf, int cap, const char *hint)
{
    DItem lab = { .type = D_LABEL, .x = x, .y = y, .text = label };
    if (label) dlg_add(d, lab);
    DItem it = { .type = D_INPUT, .x = x, .y = label ? y + 1 : y, .w = w, .buf = buf, .cap = cap, .hint = hint, .selall = 1 };
    if (label) it.link = d->n - 1;
    DItem *r = dlg_add(d, it);
    r->cpos = (int)strlen(buf);
    return r;
}
DItem *dlg_check(Dialog *d, int x, int y, const char *text, int *val, int bit, const char *hint)
{
    DItem it = { .type = D_CHECK, .x = x, .y = y, .text = text, .val = val, .bit = bit, .hint = hint, .w = hotlen(text) + 4 };
    return dlg_add(d, it);
}
DItem *dlg_radio(Dialog *d, int x, int y, const char *text, int *val, int idx, const char *hint)
{
    DItem it = { .type = D_RADIO, .x = x, .y = y, .text = text, .val = val, .bit = idx, .hint = hint, .w = hotlen(text) + 4 };
    return dlg_add(d, it);
}
DItem *dlg_button(Dialog *d, int x, int y, int w, const char *text, int cmd, int def, const char *hint)
{
    DItem it = { .type = D_BUTTON, .x = x, .y = y, .w = w, .h = 2, .text = text, .cmd = cmd, .defbtn = def, .hint = hint };
    return dlg_add(d, it);
}
DItem *dlg_frame(Dialog *d, int x, int y, int w, int h, const char *title)
{
    DItem it = { .type = D_FRAME, .x = x, .y = y, .w = w, .h = h, .text = title };
    return dlg_add(d, it);
}
DItem *dlg_list(Dialog *d, int x, int y, int w, int h, char **items, int n, const char *hint)
{
    DItem it = { .type = D_LIST, .x = x, .y = y, .w = w, .h = h, .items = items, .nitems = n, .hint = hint };
    return dlg_add(d, it);
}

/* ---------- flowing builder ---------- */
void db_start(Dialog *d, const char *title, int width)
{
    dlg_init(d, title);
    d->w = width; d->gx = 2; d->cur_x = 3; d->cur_y = 2; d->bw = width - 19;
}
void db_at(Dialog *d, int gx, int y, int bw) { d->gx = gx; d->cur_x = gx + 1; d->cur_y = y; d->bw = bw; }
void db_group(Dialog *d, const char *title, int nlines)
{
    d->group_start = d->cur_y; d->in_group = 1;
    dlg_frame(d, d->gx, d->cur_y, d->bw, nlines + 2, title);
    d->cur_y++; d->cur_x = d->gx + 2;
}
void db_end_group(Dialog *d)
{
    for (int i = d->n - 1; i >= 0; i--) if (d->it[i].type == D_FRAME) { d->cur_y = d->it[i].y + d->it[i].h; break; }
    d->in_group = 0; d->cur_x = d->gx + 1;
}
DItem *db_check(Dialog *d, const char *text, int *val, int bit, const char *hint)
{ return dlg_check(d, d->cur_x, d->cur_y++, text, val, bit, hint); }
DItem *db_radio(Dialog *d, const char *text, int *val, int idx, const char *hint)
{ return dlg_radio(d, d->cur_x, d->cur_y++, text, val, idx, hint); }
DItem *db_input(Dialog *d, const char *label, char *buf, int cap, int w, const char *hint)
{
    DItem *it = dlg_input(d, d->cur_x, d->cur_y, w, label, buf, cap, hint);
    d->cur_y += label ? 2 : 1;
    return it;
}
DItem *db_num(Dialog *d, const char *label, int *val, int w, const char *hint)
{
    DItem *it = dlg_input(d, d->cur_x, d->cur_y, w, label, "", 0, hint);
    it->buf = it->nbuf; it->cap = 12; it->val = val;
    snprintf(it->nbuf, sizeof it->nbuf, "%d", *val); it->cpos = (int)strlen(it->nbuf);
    d->cur_y += label ? 2 : 1;
    return it;
}
void db_gap(Dialog *d) { d->cur_y++; }
void db_finish(Dialog *d, int help)
{
    int maxy = 0;
    for (int i = 0; i < d->n; i++) {
        int bottom = d->it[i].y + (d->it[i].type == D_FRAME || d->it[i].type == D_LIST ? d->it[i].h : 1);
        if (bottom > maxy) maxy = bottom;
    }
    int h = maxy + 2;
    if (h < 11) h = 11;
    d->h = h;
    int bx = d->w - 15;
    dlg_button(d, bx, 2, 12, "OK", cmOK, 1, "Accept the settings in this dialog box");
    dlg_button(d, bx, 4, 12, "Cancel", cmCancel, 0, "Close the dialog box without making any changes");
    if (help) dlg_button(d, bx, 6, 12, "Help", cmHelp, 0, "View a Help screen about this dialog box");
}

/* ---------- drawing ---------- */
static int focusable(DItem *it) { return !it->disabled && (it->type == D_INPUT || it->type == D_CHECK || it->type == D_RADIO || it->type == D_BUTTON || it->type == D_LIST || (it->type == D_CUSTOM && it->onkey)); }

static int check_state(DItem *it) { return it->bit ? (*it->val & it->bit) != 0 : *it->val != 0; }

static void draw_text_lines(int x, int y, const char *t, uint8_t at)
{
    int cy = y;
    const char *p = t;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        char tmp[300]; if (n > 299) n = 299;
        memcpy(tmp, p, n); tmp[n] = 0;
        scr_hotstr(x, cy, tmp, at, pal[P_DLG_HOT]);
        cy++;
        if (!e) break;
        p = e + 1;
    }
}

static void draw_dialog(Dialog *d)
{
    uint8_t norm = pal[P_DLG_NORM];
    scr_fill(d->x, d->y, d->w, d->h, ' ', norm);
    scr_box(d->x, d->y, d->w, d->h, BOX_DOUBLE, pal[P_DLG_BORDER]);
    if (d->title) {
        char t[128]; snprintf(t, sizeof t, "%s", d->title);
        int tl = (int)strlen(t);
        scr_str(d->x + (d->w - tl) / 2, d->y, t, pal[P_DLG_TITLE]);
    }
    scr_shadow(d->x + d->w, d->y + 1, 2, d->h);
    scr_shadow(d->x + 2, d->y + d->h, d->w, 1);
    int ccx = -1, ccy = -1;
    for (int i = 0; i < d->n; i++) {
        DItem *it = &d->it[i];
        int x = d->x + it->x, y = d->y + it->y;
        int foc = i == d->focus;
        switch (it->type) {
        case D_STATIC: draw_text_lines(x, y, it->text, norm); break;
        case D_LABEL: {
            uint8_t a = (d->focus < d->n && d->it[d->focus].link == i && d->focus != i) ? pal[P_DLG_CHECK] : norm;
            scr_hotstr(x, y, it->text, a, pal[P_DLG_HOT]);
            break;
        }
        case D_FRAME: {
            scr_box(x, y, it->w, it->h, BOX_SINGLE, pal[P_DLG_BORDER]);
            if (it->text) { char t[100]; snprintf(t, sizeof t, " %s ", it->text); scr_str(x + 2, y, t, norm); }
            break;
        }
        case D_INPUT: {
            uint8_t a = pal[P_DLG_INPUT];
            scr_fill(x, y, it->w, 1, ' ', a);
            int len = (int)strlen(it->buf), vis = it->w - 1;
            if (it->cpos < it->scroll) it->scroll = it->cpos;
            if (it->cpos > it->scroll + vis - 1) it->scroll = it->cpos - vis + 1;
            if (it->scroll < 0) it->scroll = 0;
            int shown = len - it->scroll; if (shown > vis) shown = vis;
            if (shown > 0) scr_strn(x, y, it->buf + it->scroll, shown, (foc && it->selall) ? pal[P_DLG_INPUTSEL] : a);
            if (it->scroll > 0) scr_put(x - 1, y, 0x25C4, norm);
            if (len - it->scroll > vis) scr_put(x + it->w, y, 0x25BA, norm);
            if (foc) { ccx = x + it->cpos - it->scroll; ccy = y; }
            break;
        }
        case D_CHECK: {
            int on = check_state(it);
            uint8_t a = it->disabled ? pal[P_DLG_DIS] : (foc ? pal[P_DLG_CHECK] : norm);
            scr_put(x, y, '[', a); scr_put(x + 1, y, on ? 'X' : ' ', a); scr_put(x + 2, y, ']', a);
            scr_hotstr(x + 4, y, it->text, a, it->disabled ? a : pal[P_DLG_HOT]);
            if (foc) { ccx = x + 1; ccy = y; }
            break;
        }
        case D_RADIO: {
            int on = *it->val == it->bit;
            uint8_t a = it->disabled ? pal[P_DLG_DIS] : (foc ? pal[P_DLG_CHECK] : norm);
            scr_put(x, y, '(', a); scr_put(x + 1, y, on ? 0x2022 : ' ', a); scr_put(x + 2, y, ')', a);
            scr_hotstr(x + 4, y, it->text, a, it->disabled ? a : pal[P_DLG_HOT]);
            if (foc) { ccx = x + 1; ccy = y; }
            break;
        }
        case D_BUTTON: {
            uint8_t a = it->disabled ? pal[P_DLG_BTNDIS] : (foc ? pal[P_DLG_BTNSEL] : (it->defbtn ? pal[P_DLG_BTNDEF] : pal[P_DLG_BTN]));
            scr_fill(x, y, it->w, 1, ' ', a);
            int tl = hotlen(it->text);
            scr_hotstr(x + (it->w - tl) / 2, y, it->text, a, it->disabled ? a : (foc ? a : pal[P_DLG_BTNHOT]));
            scr_put(x + it->w, y, 0x2584, 0x70);
            for (int k = 1; k <= it->w; k++) scr_put(x + k, y + 1, 0x2580, 0x70);
            break;
        }
        case D_LIST: {
            scr_fill(x, y, it->w, it->h, ' ', pal[P_DLG_LIST]);
            if (it->sel >= it->nitems) it->sel = it->nitems - 1;
            if (it->sel < 0) it->sel = 0;
            if (it->sel < it->top) it->top = it->sel;
            if (it->sel >= it->top + it->h) it->top = it->sel - it->h + 1;
            for (int r = 0; r < it->h; r++) {
                int k = it->top + r;
                if (k >= it->nitems) break;
                uint8_t a = pal[P_DLG_LIST];
                if (k == it->sel) { a = foc ? pal[P_DLG_LISTFOC] : pal[P_DLG_LISTSEL]; scr_fill(x, y + r, it->w - 1, 1, ' ', a); }
                scr_strn(x + 1, y + r, it->items[k], it->w - 3, a);
            }
            for (int r = 0; r < it->h; r++) scr_put(x + it->w - 1, y + r, 0x2592, pal[P_ED_SCROLL]);
            if (it->nitems > 1 && it->h > 0) {
                int pos = (int)((long)it->sel * (it->h - 1) / (it->nitems - 1));
                scr_put(x + it->w - 1, y + pos, 0x25A0, pal[P_ED_THUMB]);
            }
            break;
        }
        case D_CUSTOM: if (it->draw) it->draw(d, it, foc); break;
        }
    }
    if (ccx >= 0) scr_cursor(ccx, ccy, 1); else scr_cursor(0, 0, 0);
}

static void dlg_hint(Dialog *d)
{
    if (d->focus < d->n && d->it[d->focus].hint) status_hint(d->it[d->focus].hint); else status_hint(NULL);
}

/* ---------- logic ---------- */
static void focus_move(Dialog *d, int dir)
{
    int start = d->focus;
    for (int k = 0; k < d->n; k++) {
        d->focus = (d->focus + dir + d->n) % d->n;
        if (focusable(&d->it[d->focus])) {
            DItem *it = &d->it[d->focus];
            if (it->type == D_RADIO && *it->val != it->bit) {
                /* radio groups take focus on the selected button */
                int found = -1;
                for (int j = 0; j < d->n; j++) if (d->it[j].type == D_RADIO && d->it[j].val == it->val && *it->val == d->it[j].bit) found = j;
                if (found >= 0 && found != d->focus) {
                    /* skip non-selected members of a group we are not entering */
                    int member = 0;
                    for (int j = 0; j < d->n; j++) if (j == start) member = d->it[j].type == D_RADIO && d->it[j].val == it->val;
                    if (!member) { d->focus = found; return; }
                    continue;
                }
            }
            if (it->type == D_INPUT) { it->selall = 1; it->cpos = (int)strlen(it->buf); }
            return;
        }
    }
    d->focus = start;
}

static void commit_numbers(Dialog *d)
{
    for (int i = 0; i < d->n; i++) if (d->it[i].type == D_INPUT && d->it[i].val) *d->it[i].val = atoi(d->it[i].buf);
}

static void press(Dialog *d, DItem *it)
{
    if (it->type == D_BUTTON) {
        if (d->on_cmd && d->on_cmd(d, it->cmd)) return;
        if (it->cmd == cmOK) commit_numbers(d);
        d->result = it->cmd; d->done = 1;
    } else if (it->type == D_CHECK) {
        if (it->bit) *it->val ^= it->bit; else *it->val = !*it->val;
        if (d->on_change) d->on_change(d);
    } else if (it->type == D_RADIO) {
        *it->val = it->bit;
        if (d->on_change) d->on_change(d);
    }
}

static int hot_of(const char *t)
{
    for (; *t; t++) if (*t == '~' && t[1]) return tolower((unsigned char)t[1]);
    return 0;
}

static void input_key(Dialog *d, DItem *it, int key)
{
    int base = key & KMASK, len = (int)strlen(it->buf);
    int ctrl = key & K_CTRL;
    if (base == KEY_LEFT) { if (it->cpos > 0) it->cpos--; it->selall = 0; }
    else if (base == KEY_RIGHT) { if (it->cpos < len) it->cpos++; it->selall = 0; }
    else if (base == KEY_HOME) { it->cpos = 0; it->selall = 0; }
    else if (base == KEY_END) { it->cpos = len; it->selall = 0; }
    else if (base == KEY_BKSP) {
        if (it->selall) { it->buf[0] = 0; it->cpos = 0; it->selall = 0; }
        else if (it->cpos > 0) { memmove(it->buf + it->cpos - 1, it->buf + it->cpos, len - it->cpos + 1); it->cpos--; }
    } else if (base == KEY_DEL) {
        if (it->selall) { it->buf[0] = 0; it->cpos = 0; it->selall = 0; }
        else if (it->cpos < len) memmove(it->buf + it->cpos, it->buf + it->cpos + 1, len - it->cpos);
    } else if (ctrl && base == 'y') { it->buf[0] = 0; it->cpos = 0; it->selall = 0; }
    else if (!ctrl && !(key & K_ALT) && base >= 32 && base < 256) {
        if (it->selall) { it->buf[0] = 0; it->cpos = 0; it->selall = 0; len = 0; }
        if (len < it->cap - 1) {
            memmove(it->buf + it->cpos + 1, it->buf + it->cpos, len - it->cpos + 1);
            it->buf[it->cpos++] = base;
        }
    } else return;
    if (d->on_change) d->on_change(d);
}

static int list_key(Dialog *d, DItem *it, int key)
{
    int base = key & KMASK, old = it->sel;
    if (it->onkey && it->onkey(d, it, key)) return 1;
    switch (base) {
    case KEY_UP: it->sel--; break; case KEY_DOWN: it->sel++; break;
    case KEY_PGUP: it->sel -= it->h - 1; break; case KEY_PGDN: it->sel += it->h - 1; break;
    case KEY_HOME: it->sel = 0; break; case KEY_END: it->sel = it->nitems - 1; break;
    default:
        if (base >= 32 && base < 127 && !(key & (K_CTRL | K_ALT))) {   /* type-ahead */
            for (int k = 1; k <= it->nitems; k++) {
                int j = (it->sel + k) % it->nitems;
                if (tolower((unsigned char)it->items[j][0]) == tolower(base)) { it->sel = j; break; }
            }
            break;
        }
        return 0;
    }
    if (it->sel < 0) it->sel = 0;
    if (it->sel >= it->nitems) it->sel = it->nitems - 1;
    if (it->sel != old || 1) if (d->on_change) d->on_change(d);
    return 1;
}

static void dlg_mouse(Dialog *d)
{
    if (ms.ev != MS_PRESS || ms.btn != 0) {
        if (ms.ev == MS_WHEEL_UP || ms.ev == MS_WHEEL_DOWN) {
            DItem *it = &d->it[d->focus];
            if (it->type == D_LIST) list_key(d, it, ms.ev == MS_WHEEL_UP ? KEY_UP : KEY_DOWN);
        }
        return;
    }
    for (int i = 0; i < d->n; i++) {
        DItem *it = &d->it[i];
        int x = d->x + it->x, y = d->y + it->y;
        if (!focusable(it)) continue;
        int w = it->w, h = it->type == D_LIST ? it->h : (it->type == D_BUTTON ? 1 : 1);
        if (it->type == D_CHECK || it->type == D_RADIO) w = hotlen(it->text) + 4;
        if (ms.x >= x && ms.x < x + w && ms.y >= y && ms.y < y + h) {
            d->focus = i;
            if (it->type == D_BUTTON || it->type == D_CHECK || it->type == D_RADIO) press(d, it);
            else if (it->type == D_INPUT) { it->selall = 0; it->cpos = it->scroll + (ms.x - x); int l = (int)strlen(it->buf); if (it->cpos > l) it->cpos = l; }
            else if (it->type == D_LIST) {
                int k = it->top + (ms.y - y);
                if (k < it->nitems) {
                    static int lastk = -1; static time_t lastt;
                    int dbl = k == lastk && time(NULL) - lastt <= 1;
                    it->sel = k; lastk = k; lastt = time(NULL);
                    if (d->on_change) d->on_change(d);
                    if (dbl) for (int j = 0; j < d->n; j++) if (d->it[j].type == D_BUTTON && d->it[j].defbtn) { press(d, &d->it[j]); break; }
                }
            }
            return;
        }
    }
}

int dlg_run(Dialog *d)
{
    d->x = (SW - d->w) / 2; d->y = (SH - d->h) / 2 - 1;
    if (d->y < 1) d->y = 1;
    d->done = 0; d->result = cmCancel;
    d->focus = 0;
    for (int i = 0; i < d->n; i++) if (focusable(&d->it[i])) { d->focus = i; break; }
    for (int i = 0; i < d->n; i++) if (d->it[i].type == D_INPUT) { d->it[i].cpos = (int)strlen(d->it[i].buf); }
    while (!d->done) {
        dlg_hint(d);
        ide_draw();
        draw_dialog(d);
        scr_flush();
        int key = key_get(1000);
        if (key == KEY_IDLE || key == KEY_NONE || key == KEY_RESIZE) {
            if (key == KEY_RESIZE) { d->x = (SW - d->w) / 2; d->y = (SH - d->h) / 2 - 1; if (d->y < 1) d->y = 1; }
            continue;
        }
        if (key == KEY_MOUSE) { dlg_mouse(d); continue; }
        DItem *f = &d->it[d->focus];
        int base = key & KMASK;
        if (key == KEY_ESC) { if (!d->on_cmd || !d->on_cmd(d, cmCancel)) { d->result = cmCancel; d->done = 1; } continue; }
        if (key == KEY_F1) { for (int i = 0; i < d->n; i++) if (d->it[i].type == D_BUTTON && d->it[i].cmd == cmHelp) press(d, &d->it[i]); continue; }
        if (key == KEY_TAB) { focus_move(d, 1); continue; }
        if (key == (KEY_TAB | K_SHIFT)) { focus_move(d, -1); continue; }
        if (key & K_ALT) {   /* Alt+hotkey */
            int hk = tolower(base), hit = -1;
            for (int i = 0; i < d->n; i++) {
                DItem *it = &d->it[i];
                if (it->disabled || !it->text) continue;
                if (hot_of(it->text) == hk && it->type != D_FRAME && it->type != D_STATIC) {
                    hit = i;
                    if (it->type == D_LABEL) { int found = -1; for (int j = 0; j < d->n; j++) if (d->it[j].link == i && (d->it[j].type == D_INPUT || d->it[j].type == D_LIST)) found = j; hit = found; }
                    break;
                }
            }
            if (hit >= 0) {
                d->focus = hit;
                DItem *it = &d->it[hit];
                if (it->type == D_BUTTON || it->type == D_CHECK || it->type == D_RADIO) press(d, it);
                else if (it->type == D_INPUT) { it->selall = 1; it->cpos = (int)strlen(it->buf); }
            }
            continue;
        }
        switch (f->type) {
        case D_INPUT:
            if (key == KEY_ENTER) {
                for (int i = 0; i < d->n; i++) if (d->it[i].type == D_BUTTON && d->it[i].defbtn && !d->it[i].disabled) { press(d, &d->it[i]); break; }
            } else if (base == KEY_UP && !(key & K_CTRL)) focus_move(d, -1);
            else if (base == KEY_DOWN && !(key & K_CTRL)) focus_move(d, 1);
            else input_key(d, f, key);
            break;
        case D_CHECK:
            if (base == ' ' || key == KEY_ENTER) { if (key == KEY_ENTER && 0) {} press(d, f); }
            else if (base == KEY_UP || base == KEY_LEFT) focus_move(d, -1);
            else if (base == KEY_DOWN || base == KEY_RIGHT) focus_move(d, 1);
            break;
        case D_RADIO: {
            if (base == ' ') press(d, f);
            else if (key == KEY_ENTER) { for (int i = 0; i < d->n; i++) if (d->it[i].type == D_BUTTON && d->it[i].defbtn) { press(d, &d->it[i]); break; } }
            else if (base == KEY_UP || base == KEY_DOWN || base == KEY_LEFT || base == KEY_RIGHT) {
                int dir = (base == KEY_DOWN || base == KEY_RIGHT) ? 1 : -1;
                int cur = d->focus, nxt = -1;
                for (int k = 1; k < d->n; k++) {
                    int j = (cur + dir * k + d->n * 2) % d->n;
                    if (d->it[j].type == D_RADIO && d->it[j].val == f->val) { nxt = j; break; }
                }
                if (nxt >= 0) { d->focus = nxt; *f->val = d->it[nxt].bit; if (d->on_change) d->on_change(d); }
            }
            break;
        }
        case D_BUTTON:
            if (key == KEY_ENTER || base == ' ') press(d, f);
            else if (base == KEY_UP || base == KEY_LEFT) focus_move(d, -1);
            else if (base == KEY_DOWN || base == KEY_RIGHT) focus_move(d, 1);
            break;
        case D_LIST:
            if (key == KEY_ENTER) { for (int i = 0; i < d->n; i++) if (d->it[i].type == D_BUTTON && d->it[i].defbtn && !d->it[i].disabled) { press(d, &d->it[i]); break; } }
            else list_key(d, f, key);
            break;
        case D_CUSTOM:
            if (key == KEY_ENTER) { for (int i = 0; i < d->n; i++) if (d->it[i].type == D_BUTTON && d->it[i].defbtn) { press(d, &d->it[i]); break; } }
            else if (f->onkey) { f->onkey(d, f, key); if (d->on_change) d->on_change(d); }
            break;
        default: break;
        }
        if (key == KEY_ENTER && f->type == D_CHECK) {
            for (int i = 0; i < d->n; i++) if (d->it[i].type == D_BUTTON && d->it[i].defbtn) { press(d, &d->it[i]); break; }
        }
        if (!d->done && (base >= 'a' && base <= 'z' || base >= 'A' && base <= 'Z') && f->type != D_INPUT && f->type != D_LIST && !(key & K_CTRL)) {
            int hk = tolower(base);
            for (int i = 0; i < d->n; i++) {
                DItem *it = &d->it[i];
                if (!it->disabled && it->text && it->type == D_BUTTON && hot_of(it->text) == hk) { press(d, it); break; }
            }
        }
    }
    scr_cursor(0, 0, 0);
    status_hint(NULL);
    return d->result;
}

/* ---------- message boxes ---------- */
int msgbox(const char *title, const char *text, int flags)
{
    Dialog d; dlg_init(&d, title);
    int maxw = 0, lines = 1;
    for (const char *p = text, *s = text; ; p++) {
        if (*p == '\n' || !*p) { int l = (int)(p - s); if (l > maxw) maxw = l; if (*p) lines++; s = p + 1; if (!*p) break; }
    }
    int nb = (flags & MB_YESNOCANCEL) ? 3 : (flags & MB_YESNO) ? 2 : 1;
    int bw = nb * 11 + (nb - 1) * 2;
    d.w = (maxw > bw ? maxw : bw) + 8; if (d.w < 24) d.w = 24;
    d.h = lines + 6;
    dlg_static(&d, (d.w - maxw) / 2, 2, text);
    int by = lines + 3, bx = (d.w - bw) / 2;
    if (flags & MB_YESNOCANCEL) {
        dlg_button(&d, bx, by, 10, "~Y~es", cmYes, 1, NULL);
        dlg_button(&d, bx + 12, by, 10, "~N~o", cmNo, 0, NULL);
        dlg_button(&d, bx + 24, by, 10, "Cancel", cmCancel, 0, NULL);
    } else if (flags & MB_YESNO) {
        dlg_button(&d, bx, by, 10, "~Y~es", cmYes, 1, NULL);
        dlg_button(&d, bx + 12, by, 10, "~N~o", cmNo, 0, NULL);
    } else dlg_button(&d, bx, by, 10, "  OK  ", cmOK, 1, NULL);
    return dlg_run(&d);
}

void infobox(const char *title, const char *fmt, ...)
{
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    msgbox(title, buf, MB_OK);
}

int inputbox(const char *title, const char *label, char *buf, int cap, const char *hint)
{
    Dialog d; dlg_init(&d, title);
    d.w = 58; d.h = 9;
    dlg_input(&d, 3, 2, 50, label, buf, cap, hint);
    dlg_button(&d, 16, 6, 10, "OK", cmOK, 1, NULL);
    dlg_button(&d, 30, 6, 10, "Cancel", cmCancel, 0, NULL);
    return dlg_run(&d) == cmOK;
}

/* ---------- file dialog ---------- */
typedef struct FD {
    char dir[512];
    char **names; int n, cap;
    char *isdir;
    char mask[128];
    struct stat st; int have_st;
    char info_name[256];
    int save;
} FD;

static int cmpnames(const void *a, const void *b)
{
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    int dx = x[strlen(x) - 1] == '/', dy = y[strlen(y) - 1] == '/';
    if (!strcmp(x, "../")) return -1; if (!strcmp(y, "../")) return 1;
    if (dx != dy) return dy - dx;
    return strcasecmp(x, y);
}

static int mask_match(const char *mask, const char *name)
{
    char m[128]; snprintf(m, sizeof m, "%s", mask);
    for (char *t = strtok(m, ";, "); t; t = strtok(NULL, ";, ")) if (!fnmatch(t, name, FNM_CASEFOLD)) return 1;
    return 0;
}

static void fd_scan(FD *f)
{
    for (int i = 0; i < f->n; i++) free(f->names[i]);
    f->n = 0;
    DIR *dp = opendir(f->dir);
    if (!dp) return;
    struct dirent *e;
    while ((e = readdir(dp))) {
        if (!strcmp(e->d_name, ".")) continue;
        char path[1100]; snprintf(path, sizeof path, "%s/%s", f->dir, e->d_name);
        struct stat st; if (stat(path, &st)) continue;
        int isd = S_ISDIR(st.st_mode);
        if (!isd && !mask_match(f->mask, e->d_name)) continue;
        if (e->d_name[0] == '.' && strcmp(e->d_name, "..")) continue;
        if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 64; f->names = realloc(f->names, f->cap * sizeof(char *)); }
        char *nm = malloc(strlen(e->d_name) + 2);
        sprintf(nm, "%s%s", e->d_name, isd ? "/" : "");
        f->names[f->n++] = nm;
    }
    closedir(dp);
    qsort(f->names, f->n, sizeof(char *), cmpnames);
}

static void fd_info_draw(Dialog *d, DItem *it, int foc)
{
    FD *f = d->user;
    int x = d->x + it->x, y = d->y + it->y;
    uint8_t a = pal[P_DLG_INFO];
    char line[300];
    snprintf(line, sizeof line, " %s", f->dir);
    scr_fill(x, y, it->w, 1, ' ', a);
    scr_strn(x, y, line, it->w, a);
    scr_fill(x, y + 1, it->w, 1, ' ', a);
    if (f->info_name[0]) {
        char path[900]; snprintf(path, sizeof path, "%s/%s", f->dir, f->info_name);
        struct stat st;
        if (!stat(path, &st)) {
            char tm[40]; strftime(tm, sizeof tm, "%b %d,%Y  %I:%M%p", localtime(&st.st_mtime));
            char nm[200]; snprintf(nm, sizeof nm, "%s", f->info_name);
            scr_strn(x + 1, y + 1, nm, 26, a);
            if (!S_ISDIR(st.st_mode)) { char sz[24]; snprintf(sz, sizeof sz, "%ld", (long)st.st_size); scr_str(x + 28 - (int)strlen(sz), y + 1, sz, a); }
            scr_str(x + 30, y + 1, tm, a);
        }
    }
}

static void fd_change(Dialog *d)
{
    FD *f = d->user;
    DItem *list = NULL;
    for (int i = 0; i < d->n; i++) if (d->it[i].type == D_LIST) list = &d->it[i];
    if (!list) return;
    if (d->focus < d->n && d->it[d->focus].type == D_LIST && list->nitems) {
        const char *s = list->items[list->sel];
        snprintf(f->info_name, sizeof f->info_name, "%s", s);
        int l = (int)strlen(f->info_name);
        if (l && f->info_name[l - 1] == '/') f->info_name[l - 1] = 0;
        for (int i = 0; i < d->n; i++) if (d->it[i].type == D_INPUT) {
            if (s[strlen(s) - 1] != '/') { snprintf(d->it[i].buf, d->it[i].cap, "%s", s); d->it[i].cpos = (int)strlen(s); d->it[i].selall = 0; }
        }
    }
}

static int fd_cmd(Dialog *d, int cmd)
{
    FD *f = d->user;
    if (cmd == cmCancel || cmd == cmHelp) return 0;
    DItem *in = NULL, *list = NULL;
    for (int i = 0; i < d->n; i++) { if (d->it[i].type == D_INPUT) in = &d->it[i]; if (d->it[i].type == D_LIST) list = &d->it[i]; }
    char *t = in->buf;
    /* focused list item that is a directory */
    if (d->it[d->focus].type == D_LIST && list->nitems) {
        const char *s = list->items[list->sel];
        if (s[strlen(s) - 1] == '/') {
            char nd[1100];
            if (!strcmp(s, "../")) { snprintf(nd, sizeof nd, "%s/..", f->dir); }
            else snprintf(nd, sizeof nd, "%s/%.*s", f->dir, (int)strlen(s) - 1, s);
            char *rp = realpath(nd, NULL);
            if (rp) { snprintf(f->dir, sizeof f->dir, "%s", rp); free(rp); }
            fd_scan(f); list->items = f->names; list->nitems = f->n; list->sel = list->top = 0; f->info_name[0] = 0;
            return 1;
        }
    }
    if (!*t) return 1;
    if (strpbrk(t, "*?")) {   /* a wildcard becomes the new mask */
        char *sl = strrchr(t, '/');
        if (sl) { *sl = 0; char nd[1100]; snprintf(nd, sizeof nd, t[0] == '/' ? "%s" : "%s/%s", t[0] == '/' ? t : f->dir, t); char *rp = realpath(t[0] ? nd : "/", NULL); if (rp) { snprintf(f->dir, sizeof f->dir, "%s", rp); free(rp); } snprintf(f->mask, sizeof f->mask, "%s", sl + 1); }
        else snprintf(f->mask, sizeof f->mask, "%s", t);
        fd_scan(f); list->items = f->names; list->nitems = f->n; list->sel = list->top = 0;
        snprintf(t, in->cap, "%s", f->mask); in->cpos = (int)strlen(t);
        return 1;
    }
    char path[1100];
    if (t[0] == '/') snprintf(path, sizeof path, "%s", t); else snprintf(path, sizeof path, "%s/%s", f->dir, t);
    struct stat st;
    if (!stat(path, &st) && S_ISDIR(st.st_mode)) {
        char *rp = realpath(path, NULL);
        if (rp) { snprintf(f->dir, sizeof f->dir, "%s", rp); free(rp); }
        fd_scan(f); list->items = f->names; list->nitems = f->n; list->sel = list->top = 0;
        snprintf(t, in->cap, "%s", f->mask); in->cpos = (int)strlen(t);
        return 1;
    }
    if (!f->save && stat(path, &st)) {
        /* opening a file that does not exist creates a new edit window, like a classic IDE */
    }
    snprintf(f->info_name, sizeof f->info_name, "%s", path);   /* result path */
    return 0;
}

int file_dialog(const char *title, const char *mask, char *out, int outcap, int save)
{
    FD f; memset(&f, 0, sizeof f);
    f.save = save;
    if (!getcwd(f.dir, sizeof f.dir)) strcpy(f.dir, ".");
    snprintf(f.mask, sizeof f.mask, "%s", mask);
    char name[256];
    if (save && out[0] && !strpbrk(out, "*?")) snprintf(name, sizeof name, "%s", out);
    else snprintf(name, sizeof name, "%s", mask);
    fd_scan(&f);
    Dialog d; dlg_init(&d, title);
    d.w = 64; d.h = 21; d.user = &f;
    d.on_change = fd_change; d.on_cmd = fd_cmd;
    dlg_input(&d, 3, 1, 36, "~N~ame", name, sizeof name, "Enter directory path and file-name mask");
    DItem lab = { .type = D_LABEL, .x = 3, .y = 4, .text = "~F~iles" };
    dlg_add(&d, lab);
    DItem *l = dlg_list(&d, 3, 5, 36, 10, f.names, f.n, "Use cursor keys to examine files; Enter loads the file");
    l->link = d.n - 2;
    dlg_button(&d, 44, 2, 14, save ? "~S~ave" : "~O~pen", cmOpen, 1, save ? "Save the file with the specified name" : "Open the specified file into a new Edit window");
    dlg_button(&d, 44, 4, 14, "~R~eplace", cmReplace, 0, "Load the specified file into the active Edit window");
    dlg_button(&d, 44, 6, 14, "Cancel", cmCancel, 0, "Close the dialog box without making any changes");
    dlg_button(&d, 44, 8, 14, "Help", cmHelp, 0, "View a Help screen about this dialog box");
    if (save) { d.it[d.n - 3].disabled = 1; d.it[d.n - 3].type = D_STATIC; d.it[d.n - 3].text = ""; }
    DItem info = { .type = D_CUSTOM, .x = 2, .y = 17, .w = 60, .h = 2, .draw = fd_info_draw };
    dlg_add(&d, info);
    int r;
    do {
        r = dlg_run(&d);
    } while (r == cmHelp);
    int ok = (r == cmOpen || r == cmReplace);
    if (ok) snprintf(out, outcap, "%s", f.info_name);
    for (int i = 0; i < f.n; i++) free(f.names[i]);
    free(f.names);
    return ok ? (r == cmReplace ? 2 : 1) : 0;
}
