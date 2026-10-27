/*
 * File:    wm.c
 * Purpose: Flash C IDE - window manager, list windows, status line, desktop
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "ide.h"
#include "dbg.h"
#include <ctype.h>
#include <time.h>
#include <unistd.h>

Win *wins[MAXWIN];
int nwins;
Msg *msgs;
int nmsgs, capmsgs, msgsel;
Buf *clipbuf;
char project_name[256];
char **proj_files;
ProjItem *proj_opts;
int nproj, proj_sel;
char **watches;
int nwatch;
char *notes_text;
char **outlines;
int noutlines;
int drag_mode, drag_dx, drag_dy;

/* ---------- messages / output ---------- */
void msg_clear(void)
{
    nmsgs = 0;
    msgsel = 0;
}

/* Adds a line to the Message window; header lines are file titles without a position. */
void msg_add(int type, const char *file, int line, const char *text, int header)
{
    if (nmsgs == capmsgs) {
        capmsgs = capmsgs ? capmsgs * 2 : 64;
        msgs = realloc(msgs, capmsgs * sizeof(Msg));
    }
    Msg *m = &msgs[nmsgs++];
    m->type = type;
    m->line = line;
    m->header = header;
    snprintf(m->file, sizeof m->file, "%s", file ? file : "");
    snprintf(m->text, sizeof m->text, "%s", text ? text : "");
}

static int
    out_open; /* the last line of the Output window is still being written (no new-line yet) */
static int out_esc; /* inside an escape sequence: 1 after ESC, 2 inside CSI */

/* Empties the Output window. */
void out_clear(void)
{
    for (int i = 0; i < noutlines; i++) {
        free(outlines[i]);
    }
    noutlines = 0;
    out_open = out_esc = 0;
}

/* Appends a complete line to the Output window. */
void out_add(const char *s)
{
    outlines = realloc(outlines, (noutlines + 1) * sizeof(char *));
    outlines[noutlines++] = strdup(s);
}

/* Appends program output to the Output window. The bytes may come in any pieces; a line that has
   not been finished yet is shown at once and extended by later calls. Escape sequences are dropped,
   \r is ignored and \b erases the previous character. */
void out_feed(const char *data, int len)
{
    for (int i = 0; i < len; i++) {
        unsigned char ch = data[i];
        if (out_esc == 1) {
            out_esc = ch == '[' ? 2 : 0;
            continue;
        }
        if (out_esc == 2) {
            if (ch >= 0x40 && ch <= 0x7e) {
                out_esc = 0;
            }
            continue;
        }
        if (ch == 27) {
            out_esc = 1;
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (!out_open) {
                out_add("");
            }
            out_open = 0;
            continue;
        }
        if (!out_open) {
            out_add("");
            out_open = 1;
        }
        char *line = outlines[noutlines - 1];
        size_t used = strlen(line);
        if (ch == '\b') {
            if (used) {
                line[used - 1] = 0;
            }
            continue;
        }
        if (used >= 590) {
            continue; /* absurdly long lines are cut */
        }
        line = realloc(line, used + 2);
        line[used] = ch;
        line[used + 1] = 0;
        outlines[noutlines - 1] = line;
    }
}

/* ---------- window list ---------- */
Win *win_active(void)
{
    return nwins ? wins[nwins - 1] : NULL;
}

/* Returns the first window of the given kind, or NULL. */
Win *win_find_kind(int kind)
{
    for (int i = 0; i < nwins; i++) {
        if (wins[i]->kind == kind) {
            return wins[i];
        }
    }
    return NULL;
}

/* Returns the number of editor windows. */
int win_edit_count(void)
{
    int n = 0;
    for (int i = 0; i < nwins; i++) {
        if (wins[i]->kind == W_EDIT) {
            n++;
        }
    }
    return n;
}

/* Returns the window shown with the number n (Alt+n), or NULL. */
Win *win_by_num(int n)
{
    for (int i = 0; i < nwins; i++) {
        if (wins[i]->num == n) {
            return wins[i];
        }
    }
    return NULL;
}

/* Brings a window to the front. */
void win_activate(Win *w)
{
    int i;
    for (i = 0; i < nwins; i++) {
        if (wins[i] == w) {
            break;
        }
    }
    if (i == nwins) {
        return;
    }
    memmove(&wins[i], &wins[i + 1], (nwins - i - 1) * sizeof(Win *));
    wins[nwins - 1] = w;
}

/* Returns the lowest unused window number (1-9), or 0 if all are taken. */
static int free_num(void)
{
    for (int n = 1; n < 10; n++) {
        if (!win_by_num(n)) {
            return n;
        }
    }
    return 0;
}

/* Returns the height of the Message window, or 0 when it is closed. */
static int msg_height(void)
{
    Win *m = win_find_kind(W_MSG);
    return m ? m->h : 0;
}

/* Creates a window of the given kind and frame and puts it on top of the stack. */
static Win *win_alloc(int kind, int x, int y, int w, int h)
{
    if (nwins >= MAXWIN) {
        return NULL;
    }
    Win *n = calloc(1, sizeof *n);
    n->kind = kind;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    n->error_line = -1;
    n->insert = cfg.insert;
    wins[nwins++] = n;
    return n;
}

/* Sets the title of an editor window to its file name, relative to the current directory. */
void win_set_title(Win *w)
{
    if (w->kind == W_EDIT) {
        const char *n = w->buf->name;
        char cwd[512];
        if (getcwd(cwd, sizeof cwd)) {
            size_t l = strlen(cwd);
            if (!strncmp(n, cwd, l) && n[l] == '/') {
                n += l + 1;
            }
        }
        snprintf(w->title, sizeof w->title, "%s", n);
    }
}

/* Opens an empty editor window; without a name it is called noname00.c, noname01.c, ... */
Win *win_new_edit(const char *name)
{
    int mh = msg_height();
    Win *w = win_alloc(W_EDIT, 0, 1, SW, SH - 2 - mh);
    if (!w) {
        return NULL;
    }
    w->buf = buf_new();
    w->num = free_num();
    if (name) {
        snprintf(w->buf->name, sizeof w->buf->name, "%s", name);
        w->buf->named = 1;
    } else {
        for (int i = 0; i < 100; i++) {
            char t[64];
            snprintf(t, sizeof t, "noname%02d%s", i, cfg.defext);
            int used = 0;
            for (int k = 0; k < nwins; k++) {
                if (wins[k]->kind == W_EDIT && !strcmp(wins[k]->buf->name, t)) {
                    used = 1;
                }
            }
            if (!used && !file_exists(t)) {
                snprintf(w->buf->name, sizeof w->buf->name, "%s", t);
                break;
            }
        }
        w->buf->named = 0;
    }
    w->insert = cfg.insert;
    win_set_title(w);
    return w;
}

/*
 * Opens a file in an editor window, or activates the window that already shows it.
 * Returns NULL if the file cannot be read.
 */
Win *win_open_file(const char *path)
{
    char *rp = realpath(path, NULL);
    const char *use = rp ? rp : path;
    for (int i = 0; i < nwins; i++) {
        if (wins[i]->kind != W_EDIT) {
            continue;
        }
        char *r2 = realpath(wins[i]->buf->name, NULL);
        int same = !strcmp(wins[i]->buf->name, use) || (r2 && !strcmp(r2, use));
        free(r2);
        if (same) {
            Win *hit = wins[i];
            win_activate(hit);
            free(rp);
            return hit;
        }
    }
    Win *w = win_new_edit(use);
    if (!w) {
        free(rp);
        return NULL;
    }
    if (file_exists(use)) {
        if (buf_load(w->buf, use)) {
            msgbox(" Error ", "Cannot read the file", MB_OK | MB_ERROR);
        }
    } else {
        w->buf->named = 1;
    }
    win_set_title(w);
    free(rp);
    return w;
}

/* Returns the single window of a special kind (Message, Output, ...), creating it if needed. */
static Win *new_special(int kind, const char *title, int x, int y, int w, int h)
{
    Win *n = win_find_kind(kind);
    if (n) {
        win_activate(n);
        return n;
    }
    n = win_alloc(kind, x, y, w, h);
    if (!n) {
        return NULL;
    }
    snprintf(n->title, sizeof n->title, "%s", title);
    return n;
}

/*
 * Shrinks the full-screen editor windows when the Message window opens (open = 1) and
 * restores them when it closes.
 */
void wm_relayout_msg(int open)
{
    Win *m = win_find_kind(W_MSG);
    if (open && m) {
        for (int i = 0; i < nwins; i++) {
            Win *w = wins[i];
            if (w->kind == W_EDIT && !w->zoomed && w->y == 1 && w->h == SH - 2) {
                w->h = SH - 2 - m->h;
            }
        }
    } else if (!open) {
        int mh = m ? m->h : 0;
        for (int i = 0; i < nwins; i++) {
            Win *w = wins[i];
            if (w->kind == W_EDIT && w->y == 1 && w->h == SH - 2 - mh) {
                w->h = SH - 2;
            }
        }
    }
}

/* Opens or activates one of the special windows (Message, Output, Watch, Project, Register). */
Win *win_get_special(int kind)
{
    switch (kind) {
    case W_MSG: {
        Win *e = win_find_kind(W_MSG);
        if (e) {
            win_activate(e);
            return e;
        }
        int h = SH > 30 ? 10 : 8;
        Win *w = new_special(W_MSG, "Message", 0, SH - 1 - h, SW, h);
        wm_relayout_msg(1);
        return w;
    }
    case W_OUT:
        return new_special(W_OUT, "Output", 0, SH - 1 - 10, SW, 10);
    case W_WATCH:
        return new_special(W_WATCH, "Watch", 0, SH - 1 - 7, SW / 2, 7);
    case W_PROJ:
        return new_special(W_PROJ, "Project", SW - 40, 2, 40, 10);
    case W_REG:
        return new_special(W_REG, "Register", SW - 30, 2, 30, 14);
    }
    return NULL;
}

/* Closes a window and releases everything it owns. */
void win_close(Win *w)
{
    int i;
    for (i = 0; i < nwins; i++) {
        if (wins[i] == w) {
            break;
        }
    }
    if (i == nwins) {
        return;
    }
    int was_msg = w->kind == W_MSG;
    memmove(&wins[i], &wins[i + 1], (nwins - i - 1) * sizeof(Win *));
    nwins--;
    if (w->kind == W_EDIT) {
        buf_free(w->buf);
    }
    if (w->kind == W_HELP) {
        help_free(w);
        free(w->data);
    }
    free(w);
    if (was_msg) {
        wm_relayout_msg(0);
    }
}

/* Closes every window. */
void win_close_all(void)
{
    while (nwins) {
        win_close(wins[nwins - 1]);
    }
}

/* Toggles between the full-screen and the normal size of a window. */
void win_zoom(Win *w)
{
    if (w->zoomed) {
        w->x = w->saved_x;
        w->y = w->saved_y;
        w->w = w->saved_w;
        w->h = w->saved_h;
        w->zoomed = 0;
    } else {
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->w;
        w->saved_h = w->h;
        w->zoomed = 1;
        w->x = 0;
        w->y = 1;
        w->w = SW;
        w->h = SH - 2;
    }
}

/* Arranges the windows side by side without overlap (Window|Tile). */
void win_tile(void)
{
    Win *list[MAXWIN];
    int n = 0;
    for (int i = 0; i < nwins; i++) {
        list[n++] = wins[i];
    }
    if (!n) {
        return;
    }
    int cols = 1;
    while (cols * cols < n) {
        cols++;
    }
    int rows = (n + cols - 1) / cols;
    int cw = SW / cols, rh = (SH - 2) / rows;
    for (int i = 0; i < n; i++) {
        Win *w = list[i];
        int r = i / cols, c = i % cols;
        w->zoomed = 0;
        w->x = c * cw;
        w->y = 1 + r * rh;
        w->w = (c == cols - 1) ? SW - c * cw : cw;
        w->h = (r == rows - 1) ? SH - 2 - r * rh : rh;
    }
}

/* Arranges the windows in an overlapping staircase (Window|Cascade). */
void win_cascade(void)
{
    int k = 0;
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        w->zoomed = 0;
        w->x = k;
        w->y = 1 + k;
        w->w = SW - 6;
        w->h = SH - 2 - 6;
        k = (k + 1) % 7;
        if (w->x + w->w > SW) {
            w->w = SW - w->x;
        }
    }
}

/* Adjusts the windows after the terminal changed size. */
void wm_resize(void)
{
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        if (w->zoomed || (w->kind == W_EDIT && w->x == 0 && w->y == 1)) {
            w->x = 0;
            w->w = SW;
            w->h = SH - 2 - (w->kind == W_EDIT ? msg_height() : 0);
        }
        if (w->kind == W_MSG || w->kind == W_OUT || w->kind == W_WATCH) {
            w->w = w->kind == W_WATCH ? SW / 2 : SW;
            w->y = SH - 1 - w->h;
        }
        if (w->x + w->w > SW) {
            w->x = SW - w->w;
            if (w->x < 0) {
                w->x = 0;
                w->w = SW;
            }
        }
        if (w->y + w->h > SH - 1) {
            w->y = SH - 1 - w->h;
            if (w->y < 1) {
                w->y = 1;
                w->h = SH - 2;
            }
        }
    }
}

/* Returns the topmost window covering screen position (x, y), or NULL. */
Win *win_at(int x, int y)
{
    for (int i = nwins - 1; i >= 0; i--) {
        Win *w = wins[i];
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) {
            return w;
        }
    }
    return NULL;
}

/* ---------- list windows ---------- */
static void frame(Win *w, int active, uint8_t fill, uint8_t border, uint8_t actborder)
{
    uint8_t fr = active ? actborder : border;
    scr_fill(w->x, w->y, w->w, w->h, ' ', fill);
    scr_box(w->x, w->y, w->w, w->h, active ? BOX_DOUBLE : BOX_SINGLE, fr);
    char t[128];
    snprintf(t, sizeof t, " %s ", w->title);
    int tl = (int)strlen(t);
    if (tl > w->w - 16) {
        tl = w->w - 16;
        if (tl < 0) {
            tl = 0;
        }
        t[tl] = 0;
    }
    scr_str(w->x + (w->w - tl) / 2, w->y, t, fr);
    if (active) {
        scr_put(w->x + 1, w->y, '[', fr);
        scr_put(w->x + 2, w->y, 0x25A0, pal[P_ED_ICON]);
        scr_put(w->x + 3, w->y, ']', fr);
        scr_put(w->x + w->w - 5, w->y, '[', fr);
        scr_put(w->x + w->w - 4, w->y, w->zoomed ? 0x2195 : 0x2191, pal[P_ED_ICON]);
        scr_put(w->x + w->w - 3, w->y, ']', fr);
    }
}

/* Returns the number of rows in a list window (messages, watches, project, ...). */
static int lw_count(Win *w)
{
    switch (w->kind) {
    case W_MSG:
        return nmsgs;
    case W_WATCH:
        return nwatch;
    case W_PROJ:
        return nproj;
    case W_OUT:
        return noutlines;
    case W_REG:
        return dbg_state() == DS_STOPPED ? dbg_nregs() : 1;
    default:
        return 0;
    }
}

/* Formats row i of a list window into out. */
static void lw_text(Win *w, int i, char *out, int n)
{
    out[0] = 0;
    switch (w->kind) {
    case W_MSG: {
        Msg *m = &msgs[i];
        if (m->header) {
            snprintf(out, n, "%s", m->text);
        } else if (m->type == 0) {
            snprintf(out, n, "Warning %s %d: %s", m->file, m->line, m->text);
        } else if (m->type == 1) {
            snprintf(out, n, "Error %s %d: %s", m->file, m->line, m->text);
        } else if (m->type == 2) {
            snprintf(out, n, "Fatal %s %d: %s", m->file, m->line, m->text);
        } else if (m->type == 3) {
            snprintf(out, n, "Linker Error: %s", m->text);
        } else if (m->type == 4) {
            snprintf(out, n, "%s %d: %s", m->file, m->line, m->text);
        } else {
            snprintf(out, n, "%s", m->text);
        }
        break;
    }
    case W_WATCH: {
        char val[300];
        int k = dbgui_eval(watches[i], val, sizeof val);
        if (k == 0 || k == -1) {
            snprintf(out, n, "%s: %s", watches[i], val);
        } else {
            long long v;
            char err[128];
            if (!fc_eval_expr(watches[i], &v, err, sizeof err)) {
                snprintf(out, n, "%s: %lld", watches[i], v);
            } else {
                snprintf(out, n, "%s: %s", watches[i], "Undefined symbol");
            }
        }
        break;
    }
    case W_PROJ: {
        const char *p = proj_files[i];
        const char *sl = strrchr(p, '/');
        snprintf(out, n, " %-24s %s", sl ? sl + 1 : p, "");
        char sz[40] = "";
        FILE *f = fopen(p, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            snprintf(sz, sizeof sz, "%ld", ftell(f));
            fclose(f);
        }
        snprintf(out, n, " %-20s %8s", sl ? sl + 1 : p, sz);
        break;
    }
    case W_OUT:
        snprintf(out, n, "%s", outlines[i]);
        break;
    case W_REG:
        if (dbg_reg_text(i, out, n) < 0) {
            out[0] = 0;
        }
        break;
    }
}

/* Draws a list window with its frame, rows, selection and scroll bar. */
static void lw_draw(Win *w, int active)
{
    int out = w->kind == W_OUT;
    uint8_t norm =
        out ? pal[P_OUT_NORM]
            : (w->kind == W_MSG || w->kind == W_WATCH || w->kind == W_PROJ || w->kind == W_REG
                   ? pal[P_MSG_NORM]
                   : pal[P_MSG_NORM]);
    uint8_t sel = pal[P_MSG_SEL];
    if (w->kind == W_MSG || w->kind == W_WATCH || w->kind == W_PROJ || w->kind == W_REG) {
        frame(w, active, norm, pal[P_MSG_BORDER], pal[P_MSG_ACTBORDER]);
    } else {
        frame(w, active, norm, pal[P_ED_BORDER], pal[P_ED_ACTBORDER]);
    }
    int h = w->h - 2, wd = w->w - 2;
    int cnt = lw_count(w);
    int *selp = w->kind == W_MSG ? &msgsel : (w->kind == W_PROJ ? &proj_sel : &w->sel);
    if (*selp >= cnt) {
        *selp = cnt - 1;
    }
    if (*selp < 0) {
        *selp = 0;
    }
    if (w->kind != W_OUT) {
        if (*selp < w->top) {
            w->top = *selp;
        }
        if (*selp >= w->top + h) {
            w->top = *selp - h + 1;
        }
    } else if (w->top > cnt - h) {
        w->top = cnt - h;
    }
    if (w->top < 0) {
        w->top = 0;
    }
    for (int r = 0; r < h; r++) {
        int i = w->top + r;
        if (i >= cnt) {
            break;
        }
        char t[600];
        lw_text(w, i, t, sizeof t);
        uint8_t a = norm;
        if (w->kind == W_MSG && !msgs[i].header && msgs[i].type >= 1 && msgs[i].type <= 3) {
            a = pal[P_MSG_NORM];
        }
        if (i == *selp && w->kind != W_OUT) {
            a = sel;
            scr_fill(w->x + 1, w->y + 1 + r, wd, 1, ' ', a);
        } else if (w->kind == W_OUT) {
        }
        scr_strn(w->x + 1, w->y + 1 + r, t, wd, a);
    }
    if (w->kind == W_MSG && cnt == 0) {
        scr_str(w->x + 2, w->y + 1, "No messages", norm);
    }
    if (w->kind == W_WATCH && cnt == 0) {
        scr_str(w->x + 2, w->y + 1, "Use Debug|Watches|Add watch to add an expression", norm);
    }
    if (w->kind == W_PROJ && cnt == 0) {
        scr_str(w->x + 2, w->y + 1, "Empty - use Project|Add item", norm);
    }
    /* scroll bar */
    if (w->h > 4) {
        int sx = w->x + w->w - 1;
        scr_put(sx, w->y + 1, 0x25B2, pal[P_ED_ARROWS]);
        scr_put(sx, w->y + w->h - 2, 0x25BC, pal[P_ED_ARROWS]);
        for (int yy = w->y + 2; yy < w->y + w->h - 2; yy++) {
            scr_put(sx, yy, 0x2592, pal[P_ED_SCROLL]);
        }
        int span = w->h - 5;
        if (span > 0 && cnt > 1) {
            scr_put(sx, w->y + 2 + (int)((long)*selp * span / (cnt - 1)), 0x25A0, pal[P_ED_THUMB]);
        }
    }
}

extern void msg_goto(int idx, int focus_editor);

/* Handles a key press in a list window (selection, Enter, Delete, ...). */
void list_win_key(Win *w, int key)
{
    int base = key & KMASK, cnt = lw_count(w);
    int *selp = w->kind == W_MSG ? &msgsel : (w->kind == W_PROJ ? &proj_sel : &w->sel);
    int h = w->h - 3;
    if (h < 1) {
        h = 1;
    }
    if (w->kind == W_OUT) {
        if (base == KEY_UP && w->top > 0) {
            w->top--;
        }
        if (base == KEY_DOWN) {
            w->top++;
        }
        if (base == KEY_PGUP) {
            w->top -= h;
        }
        if (base == KEY_PGDN) {
            w->top += h;
        }
        return;
    }
    switch (base) {
    case KEY_UP:
        if (*selp > 0) {
            (*selp)--;
        }
        break;
    case KEY_DOWN:
        if (*selp < cnt - 1) {
            (*selp)++;
        }
        break;
    case KEY_PGUP:
        *selp -= h;
        if (*selp < 0) {
            *selp = 0;
        }
        break;
    case KEY_PGDN:
        *selp += h;
        if (*selp > cnt - 1) {
            *selp = cnt - 1;
        }
        break;
    case KEY_HOME:
        *selp = 0;
        break;
    case KEY_END:
        *selp = cnt - 1;
        break;
    case KEY_ENTER:
        if (w->kind == W_MSG) {
            msg_goto(msgsel, 1);
        } else if (w->kind == W_PROJ && nproj) {
            win_open_file(proj_files[proj_sel]);
        } else if (w->kind == W_WATCH) {
            ide_command(cmEditWatch);
        }
        break;
    case ' ':
        if (w->kind == W_MSG) {
            msg_goto(msgsel, 0);
        }
        break;
    case KEY_INS:
        if (w->kind == W_PROJ) {
            ide_command(cmAddItem);
        } else if (w->kind == W_WATCH) {
            ide_command(cmAddWatch);
        }
        break;
    case KEY_DEL:
        if (w->kind == W_PROJ) {
            ide_command(cmDelItem);
        } else if (w->kind == W_WATCH) {
            ide_command(cmDelWatch);
        }
        break;
    }
    if (w->kind == W_MSG && cfg.src_tracking == 1 &&
        (base == KEY_UP || base == KEY_DOWN || base == KEY_PGUP || base == KEY_PGDN)) {
        msg_goto(msgsel, 0);
    }
}

/* ---------- status line ---------- */
typedef struct {
    const char *key, *label;
    int code;
} SItem;

static const SItem st_edit[] = {{"F1", "Help", KEY_F1},
                                {"F2", "Save", KEY_F2},
                                {"F3", "Open", KEY_F3},
                                {"Alt-F9", "Compile", K_ALT | KEY_F9},
                                {"F9", "Make", KEY_F9},
                                {"F10", "Menu", KEY_F10},
                                {0}};
static const SItem st_msg[] = {{"F1", "Help", KEY_F1},
                               {"Space", "View source", ' '},
                               {"Enter", "Edit source", KEY_ENTER},
                               {"Alt-F7", "Prev Msg", K_ALT | KEY_F7},
                               {"Alt-F8", "Next Msg", K_ALT | KEY_F8},
                               {0}};
static const SItem st_help[] = {{"F1", "Help on help", KEY_F1},
                                {"Alt-F1", "Previous topic", K_ALT | KEY_F1},
                                {"Shift-F1", "Help index", K_SHIFT | KEY_F1},
                                {"Esc", "Close help", KEY_ESC},
                                {0}};
static const SItem st_list[] = {{"F1", "Help", KEY_F1},     {"Ins", "Add", KEY_INS},
                                {"Del", "Delete", KEY_DEL}, {"Enter", "Edit", KEY_ENTER},
                                {"F10", "Menu", KEY_F10},   {0}};

/* Returns the status-line items that apply to the active window. */
static const SItem *status_set(void)
{
    Win *w = win_active();
    if (!w) {
        return st_edit;
    }
    switch (w->kind) {
    case W_MSG:
        return st_msg;
    case W_HELP:
        return st_help;
    case W_PROJ:
    case W_WATCH:
        return st_list;
    default:
        return st_edit;
    }
}

/* Draws the status line at the bottom of the screen. */
void statusline_draw(void)
{
    scr_fill(0, SH - 1, SW, 1, ' ', pal[P_STATUS]);
    extern const char *status_hint_get(void);
    const char *h = status_hint_get();
    if (h && *h) {
        scr_strn(1, SH - 1, h, SW - 2, pal[P_STATUS]);
        return;
    }
    const SItem *s = status_set();
    int x = 1;
    for (int i = 0; s[i].key; i++) {
        x += scr_str(x, SH - 1, s[i].key, pal[P_STATUS_HOT]);
        x += scr_str(x, SH - 1, " ", pal[P_STATUS]);
        x += scr_str(x, SH - 1, s[i].label, pal[P_STATUS]);
        x += 2;
    }
}

/* Handles a mouse click on the status line at column mx; returns the command or 0. */
int statusline_click(int mx)
{
    extern const char *status_hint_get(void);
    const char *h = status_hint_get();
    if (h && *h) {
        return 0;
    }
    const SItem *s = status_set();
    int x = 1;
    for (int i = 0; s[i].key; i++) {
        int w = (int)strlen(s[i].key) + 1 + (int)strlen(s[i].label);
        if (mx >= x && mx < x + w) {
            return s[i].code;
        }
        x += w + 2;
    }
    return 0;
}

/* The desktop is the dark-shade block (CP437 0xB2) in light gray on blue, as in the reference IDE:
   about three quarters of every cell is light gray, which gives the pale blue-gray texture of the
   original. */
#define DESKTOP_CHAR 0x2593

/* ---------- drawing everything ---------- */
void ide_draw(void)
{
    scr_cursor(0, 0, 0);
    scr_fill(0, 1, SW, SH - 2, DESKTOP_CHAR, pal[P_DESKTOP]);
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        int active = i == nwins - 1;
        switch (w->kind) {
        case W_EDIT:
        case W_CLIP:
        case W_NOTES:
            ed_draw(w, active);
            break;
        case W_HELP:
            help_draw(w, active);
            break;
        default:
            lw_draw(w, active);
            break;
        }
    }
    if (!nwins) {
        /* nothing open: show a hint like a fresh desktop */
    }
    menubar_draw();
    statusline_draw();
}

/* ---------- mouse ---------- */
static void win_resize_to(Win *w, int nw, int nh)
{
    if (nw < 16) {
        nw = 16;
    }
    if (nh < 5) {
        nh = 5;
    }
    if (w->x + nw > SW) {
        nw = SW - w->x;
    }
    if (w->y + nh > SH - 1) {
        nh = SH - 1 - w->y;
    }
    w->w = nw;
    w->h = nh;
    w->zoomed = 0;
}

/* Handles mouse events on the desktop: activating, dragging, resizing and closing windows. */
void wm_mouse(void)
{
    static int last_click_x = -1, last_click_y = -1;
    static time_t last_click_t;
    Win *act = win_active();
    if (ms.ev == MS_RELEASE) {
        if (drag_mode == 4 && act && act->kind == W_HELP) {
            help_mouse_release(act);
        }
        drag_mode = 0;
        return;
    }
    if (ms.ev == MS_DRAG && drag_mode && act) {
        if (drag_mode == 1) {
            act->x = ms.x - drag_dx;
            act->y = ms.y - drag_dy;
            if (act->y < 1) {
                act->y = 1;
            }
            if (act->y > SH - 3) {
                act->y = SH - 3;
            }
            if (act->x < -(act->w - 8)) {
                act->x = -(act->w - 8);
            }
            if (act->x > SW - 8) {
                act->x = SW - 8;
            }
            act->zoomed = 0;
        } else if (drag_mode == 2) {
            win_resize_to(act, ms.x - act->x + 1, ms.y - act->y + 1);
        } else if (drag_mode == 4 && act->kind == W_HELP) {
            help_mouse_drag(act, ms.x, ms.y);
        } else if (drag_mode == 3 &&
                   (act->kind == W_EDIT || act->kind == W_CLIP || act->kind == W_NOTES)) {
            ed_mouse(act);
        }
        return;
    }
    if (ms.ev == MS_WHEEL_UP || ms.ev == MS_WHEEL_DOWN) {
        Win *w = win_at(ms.x, ms.y);
        if (!w) {
            return;
        }
        if (w->kind == W_EDIT || w->kind == W_CLIP || w->kind == W_NOTES) {
            ed_mouse(w);
        } else if (w->kind == W_HELP) {
            help_scroll(w, ms.ev == MS_WHEEL_UP ? -3 : 3);
        } else {
            list_win_key(w, ms.ev == MS_WHEEL_UP ? KEY_UP : KEY_DOWN);
        }
        return;
    }
    if (ms.ev != MS_PRESS || ms.btn != 0) {
        if (ms.ev == MS_PRESS && ms.btn == 2 && cfg.mouse_right) {
            static const int cmds[] = {
                0,          cmHelpTopic, cmFind,    cmSearchAgain, cmReplaceText, cmGotoCursor,
                cmToggleBP, cmInspect,   cmEvalMod, cmAddWatch};
            if (cfg.mouse_right < 10 && cmds[cfg.mouse_right]) {
                ide_command(cmds[cfg.mouse_right]);
            }
        }
        return;
    }
    if (ms.y == 0) {
        for (int i = 0; i < NBAR; i++) {
            if (ms.x >= bar_x[i] && ms.x < bar_x[i] + bar_w[i]) {
                int c = menu_run(i);
                if (c) {
                    ide_command(c);
                }
                return;
            }
        }
        return;
    }
    if (ms.y == SH - 1) {
        int k = statusline_click(ms.x);
        if (k) {
            extern void ide_inject_key(int);
            ide_inject_key(k);
        }
        return;
    }
    Win *w = win_at(ms.x, ms.y);
    if (!w) {
        return;
    }
    if (w != act) {
        win_activate(w);
        return;
    }
    int dbl = last_click_x == ms.x && last_click_y == ms.y && time(NULL) - last_click_t <= 1;
    last_click_x = ms.x;
    last_click_y = ms.y;
    last_click_t = time(NULL);
    if (ms.y == w->y) {
        if (ms.x >= w->x + 1 && ms.x <= w->x + 3) {
            ide_command(cmCloseWin);
            return;
        }
        if (ms.x >= w->x + w->w - 5 && ms.x <= w->x + w->w - 3) {
            win_zoom(w);
            return;
        }
        if (dbl) {
            win_zoom(w);
            return;
        }
        drag_mode = 1;
        drag_dx = ms.x - w->x;
        drag_dy = ms.y - w->y;
        return;
    }
    if (ms.y == w->y + w->h - 1 && ms.x == w->x + w->w - 1) {
        drag_mode = 2;
        return;
    }
    int isedit = w->kind == W_EDIT || w->kind == W_CLIP || w->kind == W_NOTES;
    if (ms.x == w->x + w->w - 1) { /* vertical scroll bar */
        if (w->kind == W_HELP) {
            help_bar_click(w, ms.y);
            return;
        }
        if (isedit) {
            int key = 0;
            if (ms.y == w->y + 1) {
                key = KEY_UP;
            } else if (ms.y == w->y + w->h - 2) {
                key = KEY_DOWN;
            } else if (ms.y < w->y + w->h / 2) {
                key = KEY_PGUP;
            } else {
                key = KEY_PGDN;
            }
            ed_key(w, key);
        } else {
            list_win_key(w, ms.y <= w->y + 1
                                ? KEY_UP
                                : (ms.y >= w->y + w->h - 2
                                       ? KEY_DOWN
                                       : (ms.y < w->y + w->h / 2 ? KEY_PGUP : KEY_PGDN)));
        }
        return;
    }
    if (ms.y == w->y + w->h - 1) { /* horizontal scroll bar */
        if (isedit) {
            if (ms.x < w->x + w->w / 2) {
                w->left -= 8;
                if (w->left < 0) {
                    w->left = 0;
                }
            } else {
                w->left += 8;
            }
        }
        return;
    }
    if (isedit) {
        drag_mode = 3;
        ed_mouse(w);
        return;
    }
    if (w->kind == W_HELP) {
        drag_mode = 4;
        help_mouse_press(w, ms.x, ms.y);
        return;
    }
    /* list windows */
    int row = w->top + (ms.y - w->y - 1);
    int cnt = lw_count(w);
    if (row >= 0 && row < cnt && w->kind != W_OUT) {
        int *selp = w->kind == W_MSG ? &msgsel : (w->kind == W_PROJ ? &proj_sel : &w->sel);
        *selp = row;
        if (dbl) {
            list_win_key(w, KEY_ENTER);
        } else if (w->kind == W_MSG && cfg.src_tracking == 1) {
            msg_goto(msgsel, 0);
        }
    }
}
