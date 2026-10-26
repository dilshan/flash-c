/* Flash C IDE - the editor: drawing, cursor movement, editing commands */
#include "ide.h"
#include <ctype.h>

extern int ur_l, ur_c;
char find_text[256], repl_text[256];
int find_opts[8];

static int tabw(void) { return cfg.tabsize > 0 ? cfg.tabsize : 8; }
static int col_of(Line *ln, int idx)
{
    int c = 0, t = tabw();
    for (int i = 0; i < idx && i < ln->len; i++) c += ln->s[i] == '\t' ? t - c % t : 1;
    if (idx > ln->len) c += idx - ln->len;
    return c;
}
static int idx_of(Line *ln, int col)
{
    int c = 0, i = 0, t = tabw();
    while (i < ln->len) {
        int cw = ln->s[i] == '\t' ? t - c % t : 1;
        if (c + cw > col) break;
        c += cw; i++;
    }
    return i;
}

static int vw(Win *w) { return w->w - 2; }
static int vh(Win *w) { return w->h - 2; }

void ed_ensure_visible(Win *w)
{
    Buf *b = w->buf;
    if (w->cy >= b->n) w->cy = b->n - 1;
    if (w->cy < 0) w->cy = 0;
    if (w->cx > b->ln[w->cy].len) w->cx = b->ln[w->cy].len;
    int h = vh(w), wd = vw(w);
    if (h < 1) h = 1;
    if (w->cy < w->top) w->top = w->cy;
    if (w->cy >= w->top + h) w->top = w->cy - h + 1;
    if (w->top > b->n - 1) w->top = b->n - 1;
    if (w->top < 0) w->top = 0;
    int col = col_of(&b->ln[w->cy], w->cx);
    if (col < w->left) w->left = col;
    if (col >= w->left + wd) w->left = col - wd + 1;
    if (w->left < 0) w->left = 0;
}

void ed_goto(Win *w, int line, int col)
{
    if (line < 0) line = 0;
    if (line >= w->buf->n) line = w->buf->n - 1;
    w->cy = line; w->cx = col < 0 ? 0 : col;
    if (w->cx > w->buf->ln[line].len) w->cx = w->buf->ln[line].len;
    w->want = col_of(&w->buf->ln[w->cy], w->cx);
    ed_ensure_visible(w);
}

/* ---------- selection / blocks ---------- */
static int pos_cmp(int l1, int c1, int l2, int c2) { return l1 != l2 ? (l1 < l2 ? -1 : 1) : (c1 < c2 ? -1 : c1 > c2 ? 1 : 0); }

int ed_has_block(Win *w)
{
    int a, b, c, d;
    if (w->sel_on) { ed_block_range(w, &a, &b, &c, &d); return pos_cmp(a, b, c, d) != 0; }
    if (w->blk_on) return pos_cmp(w->bl1, w->bc1, w->bl2, w->bc2) != 0;
    return 0;
}
void ed_block_range(Win *w, int *l1, int *c1, int *l2, int *c2)
{
    int a, b, c, d;
    if (w->sel_on) { a = w->sl; b = w->sc; c = w->cy; d = w->cx; }
    else { a = w->bl1; b = w->bc1; c = w->bl2; d = w->bc2; }
    if (pos_cmp(a, b, c, d) > 0) { int t = a; a = c; c = t; t = b; b = d; d = t; }
    *l1 = a; *c1 = b; *l2 = c; *c2 = d;
}

static void sel_clear(Win *w) { w->sel_on = 0; }

void clip_set_system(const char *text, int n)
{
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (n > 60000) return;
    char *out = malloc(n * 4 / 3 + 16), *o = out;
    for (int i = 0; i < n; i += 3) {
        unsigned v = (unsigned char)text[i] << 16;
        if (i + 1 < n) v |= (unsigned char)text[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned char)text[i + 2];
        *o++ = b64[v >> 18]; *o++ = b64[(v >> 12) & 63];
        *o++ = i + 1 < n ? b64[(v >> 6) & 63] : '='; *o++ = i + 2 < n ? b64[v & 63] : '=';
    }
    *o = 0;
    printf("\033]52;c;%s\a", out); fflush(stdout);
    free(out);
}

void clip_set_text(const char *t)
{
    buf_clear(clipbuf);
    int l = 0, c = 0;
    buf_insert(clipbuf, &l, &c, t, -1);
    clipbuf->modified = 0;
    clip_set_system(t, (int)strlen(t));
}

void ed_copy(Win *w, int cut)
{
    if (!ed_has_block(w)) return;
    int l1, c1, l2, c2;
    ed_block_range(w, &l1, &c1, &l2, &c2);
    char *t = buf_get(w->buf, l1, c1, l2, c2);
    clip_set_text(t);
    free(t);
    if (cut) {
        ur_l = w->cy; ur_c = w->cx; buf_group(w->buf);
        buf_delete(w->buf, l1, c1, l2, c2);
        ed_goto(w, l1, c1); sel_clear(w); w->blk_on = 0;
    } else if (!cfg.persist_blocks) sel_clear(w);
}

void ed_paste(Win *w)
{
    int len;
    char *t = buf_all(clipbuf, &len);
    if (!len) { free(t); return; }
    ur_l = w->cy; ur_c = w->cx; buf_group(w->buf);
    if (ed_has_block(w) && cfg.overwrite_blocks) ed_clear_block(w);
    int l = w->cy, c = w->cx;
    buf_insert(w->buf, &l, &c, t, len);
    free(t);
    ed_goto(w, l, c); sel_clear(w);
}

void ed_clear_block(Win *w)
{
    if (!ed_has_block(w)) return;
    int l1, c1, l2, c2;
    ed_block_range(w, &l1, &c1, &l2, &c2);
    ur_l = w->cy; ur_c = w->cx; buf_group(w->buf);
    buf_delete(w->buf, l1, c1, l2, c2);
    ed_goto(w, l1, c1); sel_clear(w); w->blk_on = 0;
}

/* ---------- drawing ---------- */
void ed_draw(Win *w, int active)
{
    Buf *b = w->buf;
    uint8_t fr = active ? pal[P_ED_ACTBORDER] : pal[P_ED_BORDER];
    uint8_t tt = active ? pal[P_ED_ACTTITLE] : pal[P_ED_TITLE];
    int x = w->x, y = w->y, ww = w->w, hh = w->h;
    int style = active ? BOX_DOUBLE : BOX_SINGLE;
    ed_ensure_visible(w);
    scr_box(x, y, ww, hh, style, fr);
    /* title */
    {
        char t[128];
        snprintf(t, sizeof t, " %s ", w->title);
        int tl = (int)strlen(t), maxl = ww - 16;
        if (maxl < 4) maxl = 4;
        if (tl > maxl) { t[maxl] = 0; tl = maxl; }
        scr_str(x + (ww - tl) / 2, y, t, tt);
        if (active) {
            scr_put(x + 1, y, '[', fr); scr_put(x + 2, y, 0x25A0, pal[P_ED_ICON]); scr_put(x + 3, y, ']', fr);
        }
        if (w->num > 0 && w->num < 10) {
            scr_put(x + ww - 8, y, '0' + w->num, fr);
        }
        if (active) {
            scr_put(x + ww - 5, y, '[', fr);
            scr_put(x + ww - 4, y, w->zoomed ? 0x2195 : 0x2191, pal[P_ED_ICON]);
            scr_put(x + ww - 3, y, ']', fr);
        }
    }
    if (w->errline >= 0 && w->buf->version != w->errver) w->errline = -1;   /* editing hides the error bar */
    int h = vh(w), wd = vw(w);
    uint8_t attrs[1024];
    int l1 = 0, c1 = 0, l2 = 0, c2 = 0, hb = ed_has_block(w);
    if (hb) ed_block_range(w, &l1, &c1, &l2, &c2);
    for (int r = 0; r < h; r++) {
        int ln = w->top + r;
        int sy = y + 1 + r;
        scr_fill(x + 1, sy, wd, 1, ' ', pal[P_ED_NORM]);
        if (ln >= b->n) continue;
        Line *L = &b->ln[ln];
        int n = L->len < 1024 ? L->len : 1024;
        syntax_line_attrs(b, ln, attrs, n);
        int dp = w->kind == W_EDIT && w->buf->named ? dbgui_line_pal(w->buf->name, ln + 1) : -1;
        uint8_t base = ln == w->errline ? pal[P_ED_ERRLINE] : dp >= 0 ? pal[dp] : pal[P_ED_NORM];
        if (ln == w->errline || dp >= 0) scr_fill(x + 1, sy, wd, 1, ' ', base);
        int col = 0;
        for (int i = 0; i < L->len; i++) {
            int cw = 1;
            unsigned char ch = L->s[i];
            if (ch == '\t') cw = tabw() - col % tabw();
            uint8_t a = (ln == w->errline || dp >= 0) ? base : (i < 1024 ? attrs[i] : pal[P_ED_NORM]);
            if (hb && pos_cmp(ln, i, l1, c1) >= 0 && pos_cmp(ln, i, l2, c2) < 0) a = pal[P_ED_SEL];
            for (int k = 0; k < cw; k++) {
                int sx = col + k - w->left;
                if (sx >= 0 && sx < wd) scr_put(x + 1 + sx, sy, ch == '\t' ? ' ' : (ch < 32 || ch >= 127 ? (ch >= 128 ? '?' : 0xB7) : ch), a);
            }
            col += cw;
            if (col - w->left > wd) break;
        }
        /* selection that spans the line end */
        if (hb && pos_cmp(ln, L->len, l1, c1) >= 0 && pos_cmp(ln, L->len, l2, c2) < 0) {
            int sx = col - w->left;
            if (sx >= 0 && sx < wd) scr_put(x + 1 + sx, sy, ' ', pal[P_ED_SEL]);
        }
    }
    /* vertical scroll bar */
    if (hh > 4) {
        int sx = x + ww - 1, top = y + 1, bot = y + hh - 2;
        scr_put(sx, top, 0x25B2, pal[P_ED_ARROWS]);
        scr_put(sx, bot, 0x25BC, pal[P_ED_ARROWS]);
        for (int yy = top + 1; yy < bot; yy++) scr_put(sx, yy, 0x2592, pal[P_ED_SCROLL]);
        int span = bot - top - 2;
        if (span > 0) {
            int pos = b->n > 1 ? (int)((long)w->cy * span / (b->n - 1)) : 0;
            scr_put(sx, top + 1 + pos, 0x25A0, pal[P_ED_THUMB]);
        }
    }
    /* horizontal scroll bar and position */
    {
        char pos[32];
        snprintf(pos, sizeof pos, "%s%d:%d", b->modified ? "*" : "", w->cy + 1, col_of(&b->ln[w->cy], w->cx) + 1);
        scr_put(x + 1, y + hh - 1, style == BOX_DOUBLE ? 0x2550 : 0x2500, fr);
        scr_str(x + 2, y + hh - 1, pos, fr);
        int sx = x + 2 + (int)strlen(pos) + 1;
        int ex = x + ww - 2;
        if (ex - sx >= 8) {
            scr_put(sx, y + hh - 1, 0x25C4, pal[P_ED_ARROWS]);
            scr_put(ex, y + hh - 1, 0x25BA, pal[P_ED_ARROWS]);
            for (int xx = sx + 1; xx < ex; xx++) scr_put(xx, y + hh - 1, 0x2592, pal[P_ED_SCROLL]);
            int span = ex - sx - 2;
            int maxcol = 0; (void)maxcol;
            int pos2 = (int)((long)(w->left > 255 ? 255 : w->left) * span / 255);
            if (span > 0) scr_put(sx + 1 + pos2, y + hh - 1, 0x25A0, pal[P_ED_THUMB]);
        }
    }
    if (active) {
        int cc = col_of(&b->ln[w->cy], w->cx) - w->left;
        int cr = w->cy - w->top;
        scr_cursor(x + 1 + cc, y + 1 + cr, 1);
    }
}

/* ---------- editing primitives ---------- */
static void begin_edit(Win *w, int kind)
{
    Buf *b = w->buf;
    ur_l = w->cy; ur_c = w->cx;
    if (kind == 0 || kind != b->last_kind || !cfg.group_undo) buf_group(b);
    b->last_kind = kind;
}

static void set_cursor(Win *w, int l, int c, int extend)
{
    if (extend) {
        if (!w->sel_on) { w->sel_on = 1; w->sl = w->cy; w->sc = w->cx; }
    } else if (w->sel_on && !cfg.persist_blocks) w->sel_on = 0;
    w->cy = l; w->cx = c;
    ed_ensure_visible(w);
    w->want = col_of(&w->buf->ln[w->cy], w->cx);
    w->buf->last_kind = -1;
}

static void insert_str(Win *w, const char *s, int n)
{
    if (ed_has_block(w) && cfg.overwrite_blocks) ed_clear_block(w);
    int l = w->cy, c = w->cx;
    buf_insert(w->buf, &l, &c, s, n);
    w->cy = l; w->cx = c; sel_clear(w);
    ed_ensure_visible(w);
    w->want = col_of(&w->buf->ln[w->cy], w->cx);
}

static int is_wc(int c) { return isalnum(c) || c == '_'; }

static void word_right(Win *w, int *l, int *c)
{
    Buf *b = w->buf; *l = w->cy; *c = w->cx;
    Line *L = &b->ln[*l];
    if (*c >= L->len) { if (*l < b->n - 1) { (*l)++; *c = 0; } return; }
    if (is_wc((unsigned char)L->s[*c])) while (*c < L->len && is_wc((unsigned char)L->s[*c])) (*c)++;
    else if (!isspace((unsigned char)L->s[*c])) (*c)++;
    while (*c < L->len && isspace((unsigned char)L->s[*c])) (*c)++;
}
static void word_left(Win *w, int *l, int *c)
{
    Buf *b = w->buf; *l = w->cy; *c = w->cx;
    if (*c == 0) { if (*l > 0) { (*l)--; *c = b->ln[*l].len; } return; }
    Line *L = &b->ln[*l];
    while (*c > 0 && isspace((unsigned char)L->s[*c - 1])) (*c)--;
    if (*c > 0 && is_wc((unsigned char)L->s[*c - 1])) while (*c > 0 && is_wc((unsigned char)L->s[*c - 1])) (*c)--;
    else if (*c > 0) (*c)--;
}

static void do_enter(Win *w)
{
    begin_edit(w, 0);
    char buf[512]; int n = 1; buf[0] = '\n';
    if (cfg.autoindent) {
        Line *L = &w->buf->ln[w->cy];
        int i = 0;
        while (i < L->len && i < w->cx && (L->s[i] == ' ' || L->s[i] == '\t') && n < 500) buf[n++] = L->s[i++];
    }
    if (!w->insert && w->cx < w->buf->ln[w->cy].len && 0) { }
    insert_str(w, buf, n);
}

static void do_backspace(Win *w)
{
    if (ed_has_block(w) && cfg.overwrite_blocks) { begin_edit(w, 0); ed_clear_block(w); return; }
    Buf *b = w->buf;
    begin_edit(w, 2);
    if (w->cx > 0) {
        Line *L = &b->ln[w->cy];
        int from = w->cx - 1;
        if (cfg.bs_unindent) {
            int allsp = 1;
            for (int i = 0; i < w->cx; i++) if (L->s[i] != ' ') allsp = 0;
            if (allsp) {
                int col = col_of(L, w->cx), t = tabw(), target = ((col - 1) / t) * t;
                if (w->cy > 0) {  /* align with previous non-blank line's indentation when possible */
                    for (int p = w->cy - 1; p >= 0; p--) {
                        Line *P = &b->ln[p]; int k = 0;
                        while (k < P->len && (P->s[k] == ' ' || P->s[k] == '\t')) k++;
                        if (k == P->len) continue;
                        int pc = col_of(P, k);
                        if (pc < col && pc > target) target = pc;
                        break;
                    }
                }
                from = w->cx - (col - target); if (from < 0) from = 0;
                if (from > w->cx - 1) from = w->cx - 1;
            }
        }
        buf_delete(b, w->cy, from, w->cy, w->cx);
        w->cx = from;
    } else if (w->cy > 0) {
        int pl = b->ln[w->cy - 1].len;
        buf_delete(b, w->cy - 1, pl, w->cy, 0);
        w->cy--; w->cx = pl;
    }
    ed_ensure_visible(w); w->want = col_of(&b->ln[w->cy], w->cx);
}

static void do_delete(Win *w)
{
    if (ed_has_block(w)) { begin_edit(w, 0); ed_clear_block(w); return; }
    Buf *b = w->buf;
    begin_edit(w, 3);
    Line *L = &b->ln[w->cy];
    if (w->cx < L->len) buf_delete(b, w->cy, w->cx, w->cy, w->cx + 1);
    else if (w->cy < b->n - 1) buf_delete(b, w->cy, L->len, w->cy + 1, 0);
}

static void do_delete_line(Win *w)
{
    Buf *b = w->buf;
    begin_edit(w, 0);
    if (b->n == 1) buf_delete(b, 0, 0, 0, b->ln[0].len);
    else if (w->cy < b->n - 1) buf_delete(b, w->cy, 0, w->cy + 1, 0);
    else buf_delete(b, w->cy - 1, b->ln[w->cy - 1].len, w->cy, b->ln[w->cy].len);
    w->cx = 0; sel_clear(w);
    ed_ensure_visible(w);
}

static void do_insert_char(Win *w, int ch)
{
    Buf *b = w->buf;
    begin_edit(w, 1);
    char c = (char)ch;
    if (!w->insert && !ed_has_block(w) && w->cx < b->ln[w->cy].len) {
        buf_delete(b, w->cy, w->cx, w->cy, w->cx + 1);
    }
    insert_str(w, &c, 1);
}

static void do_tab(Win *w)
{
    begin_edit(w, 1);
    if (cfg.use_tab) { insert_str(w, "\t", 1); return; }
    int col = col_of(&w->buf->ln[w->cy], w->cx), t = tabw(), n = t - col % t;
    char sp[32]; memset(sp, ' ', n);
    insert_str(w, sp, n);
}

/* Searching ------------------------------------------------------- */
int ed_find_next(Win *w, int dir_override)
{
    Buf *b = w->buf;
    int cs = find_opts[0], whole = find_opts[1], rx = find_opts[2];
    int dir = dir_override >= 0 ? dir_override : find_opts[4];       /* 0 forward 1 backward */
    int scope = find_opts[5], origin = find_opts[6];                 /* scope 1 = selected text, origin 1 = entire */
    if (!find_text[0]) return 0;
    int sl = 0, sc = 0, el = b->n - 1, ec = b->ln[b->n - 1].len;
    if (scope == 1 && ed_has_block(w)) ed_block_range(w, &sl, &sc, &el, &ec);
    int l = w->cy, c = w->cx;
    if (origin == 1) { if (dir == 0) { l = sl; c = sc; } else { l = el; c = ec; } }
    else if (w->sel_on || ed_has_block(w)) {
        int a, bb, cc, d; ed_block_range(w, &a, &bb, &cc, &d);
        if (dir == 0) { l = cc; c = d; } else { l = a; c = bb; }
        if (w->sel_on) { /* continue from the current match */ }
    }
    int ml;
    if (dir == 0) {
        for (; l <= el; l++) {
            Line *L = &b->ln[l];
            int from = c;
            int lim = L->len;
            int p = text_find(L->s, lim, from, find_text, cs, whole, rx, &ml);
            if (p >= 0 && !(l == el && p + ml > ec) && !(l == sl && p < sc)) {
                w->sel_on = 1; w->sl = l; w->sc = p; w->cy = l; w->cx = p + ml;
                w->want = col_of(L, w->cx); ed_ensure_visible(w); return 1;
            }
            c = 0;
        }
    } else {
        for (; l >= sl; l--) {
            Line *L = &b->ln[l];
            int before = (l == w->cy || origin == 1) ? c : L->len + 1;
            if (c > L->len) before = L->len + 1;
            int p = text_find_rev(L->s, L->len, before, find_text, cs, whole, rx, &ml);
            if (p >= 0 && !(l == sl && p < sc)) {
                w->sel_on = 1; w->sl = l; w->sc = p + ml; w->cy = l; w->cx = p;
                w->want = col_of(L, w->cx); ed_ensure_visible(w); return 1;
            }
            c = l > 0 ? b->ln[l - 1].len + 1 : 0;
            if (l > 0) origin = 0, w->cy = l - 1;
        }
    }
    return 0;
}

/* block commands (^K prefix) */
static void kcommand(Win *w, int k)
{
    int c = k & KMASK;
    if (c >= 'A' && c <= 'Z') c += 32;
    switch (c) {
    case 'b': w->bl1 = w->cy; w->bc1 = w->cx; if (!w->blk_on) { w->bl2 = w->cy; w->bc2 = w->cx; } w->blk_on = 1; w->sel_on = 0;
        if (pos_cmp(w->bl1, w->bc1, w->bl2, w->bc2) > 0) { w->bl2 = w->bl1; w->bc2 = w->bc1; } break;
    case 'k': w->bl2 = w->cy; w->bc2 = w->cx; if (!w->blk_on) { w->bl1 = w->cy; w->bc1 = w->cx; } w->blk_on = 1; w->sel_on = 0;
        if (pos_cmp(w->bl1, w->bc1, w->bl2, w->bc2) > 0) { w->bl1 = w->bl2; w->bc1 = w->bc2; } break;
    case 'h': w->blk_on = 0; w->sel_on = 0; break;
    case 't': { int l, c2; word_right(w, &l, &c2); w->blk_on = 1; w->bl1 = w->cy; w->bc1 = w->cx; w->bl2 = l; w->bc2 = c2; break; }
    case 'l': w->blk_on = 1; w->bl1 = w->cy; w->bc1 = 0; w->bl2 = w->cy; w->bc2 = w->buf->ln[w->cy].len; break;
    case 'c': if (ed_has_block(w)) {
        int l1, c1, l2, c2; ed_block_range(w, &l1, &c1, &l2, &c2);
        char *t = buf_get(w->buf, l1, c1, l2, c2);
        begin_edit(w, 0);
        int l = w->cy, cc = w->cx; buf_insert(w->buf, &l, &cc, t, -1); free(t);
        w->cy = l; w->cx = cc; w->sel_on = 0; w->blk_on = 0; ed_ensure_visible(w);
    } break;
    case 'y': begin_edit(w, 0); ed_clear_block(w); break;
    case 'v': if (ed_has_block(w)) {
        int l1, c1, l2, c2; ed_block_range(w, &l1, &c1, &l2, &c2);
        if (pos_cmp(w->cy, w->cx, l1, c1) > 0 && pos_cmp(w->cy, w->cx, l2, c2) < 0) break;
        char *t = buf_get(w->buf, l1, c1, l2, c2);
        int tl = w->cy, tc = w->cx;
        begin_edit(w, 0);
        buf_delete(w->buf, l1, c1, l2, c2);
        if (pos_cmp(tl, tc, l2, c2) >= 0) {
            if (tl == l2) tc = tc - c2 + c1;
            tl -= (l2 - l1);
        }
        buf_insert(w->buf, &tl, &tc, t, -1); free(t);
        w->cy = tl; w->cx = tc; w->sel_on = 0; w->blk_on = 0; ed_ensure_visible(w);
    } break;
    case 'r': {
        char path[512] = "*.c";
        if (file_dialog(" Read Block From File ", "*.*", path, sizeof path, 0)) {
            Buf *tmp = buf_new();
            if (!buf_load(tmp, path)) {
                char *t = buf_all(tmp, NULL);
                begin_edit(w, 0);
                int l = w->cy, cc = w->cx; buf_insert(w->buf, &l, &cc, t, -1);
                w->cy = l; w->cx = cc; free(t); ed_ensure_visible(w);
            } else msgbox(" Error ", "Cannot read file", MB_OK | MB_ERROR);
            buf_free(tmp);
        }
        break;
    }
    case 'w': if (ed_has_block(w)) {
        char path[512] = "";
        if (file_dialog(" Write Block to File ", "*.*", path, sizeof path, 1)) {
            int l1, c1, l2, c2; ed_block_range(w, &l1, &c1, &l2, &c2);
            char *t = buf_get(w->buf, l1, c1, l2, c2);
            FILE *f = fopen(path, "wb");
            if (f) { fputs(t, f); fclose(f); } else msgbox(" Error ", "Cannot create file", MB_OK | MB_ERROR);
            free(t);
        }
    } break;
    case 's': ide_command(cmSaveFile); break;
    case 'd': ide_command(cmSaveFile); break;
    }
}

static void qcommand(Win *w, int k, int shift)
{
    int c = k & KMASK;
    if (c >= 'A' && c <= 'Z') c += 32;
    Buf *b = w->buf;
    switch (c) {
    case 's': set_cursor(w, w->cy, 0, 0); break;
    case 'd': set_cursor(w, w->cy, b->ln[w->cy].len, 0); break;
    case 'e': set_cursor(w, w->top, w->cx, 0); break;
    case 'x': { int l = w->top + vh(w) - 1; if (l >= b->n) l = b->n - 1; set_cursor(w, l, w->cx, 0); break; }
    case 'r': set_cursor(w, 0, 0, 0); break;
    case 'c': set_cursor(w, b->n - 1, b->ln[b->n - 1].len, 0); break;
    case 'f': ide_command(cmFind); break;
    case 'a': ide_command(cmReplaceText); break;
    case 'y': begin_edit(w, 0); buf_delete(b, w->cy, w->cx, w->cy, b->ln[w->cy].len); break;
    case 'b': set_cursor(w, w->bl1, w->bc1, 0); break;
    case 'k': set_cursor(w, w->bl2, w->bc2, 0); break;
    case '[': case ']': {
        Line *L = &b->ln[w->cy];
        if (w->cx >= L->len) break;
        char ch = L->s[w->cx], open = 0, close = 0; int dir = 0;
        if (ch == '(' || ch == '{' || ch == '[') { open = ch; close = ch == '(' ? ')' : ch == '{' ? '}' : ']'; dir = 1; }
        else if (ch == ')' || ch == '}' || ch == ']') { close = ch; open = ch == ')' ? '(' : ch == '}' ? '{' : '['; dir = -1; }
        else break;
        int depth = 0, l = w->cy, i = w->cx;
        for (;;) {
            Line *M = &b->ln[l];
            if (i >= 0 && i < M->len) {
                if (M->s[i] == open) depth += dir; else if (M->s[i] == close) depth -= dir;
                if (!depth) { set_cursor(w, l, i, 0); break; }
            }
            i += dir;
            if (i < 0) { if (--l < 0) break; i = b->ln[l].len - 1; }
            else if (i >= M->len) { if (++l >= b->n) break; i = 0; }
        }
        break;
    }
    }
}

int ed_key(Win *w, int key)
{
    Buf *b = w->buf;
    int base = key & KMASK, shift = key & K_SHIFT, ctrl = key & K_CTRL, alt = key & K_ALT;
    int l, c;

    if (w->kprefix) {
        int p = w->kprefix; w->kprefix = 0;
        if (key == KEY_ESC) return 1;
        if (p == 'k') kcommand(w, base); else qcommand(w, base, shift);
        return 1;
    }
    if (key == KEY_ESC) {                              /* Esc hides the error / warning bar left by the Message window */
        int any = 0;
        for (int i = 0; i < nwins; i++) if (wins[i]->kind == W_EDIT && wins[i]->errline >= 0) { wins[i]->errline = -1; any = 1; }
        if (any) return 1;
    }
    if (ctrl && !alt && !shift && cfg.cmdset == 0) {   /* CUA command set */
        switch (base) {
        case 'z': ide_command(cmUndo); return 1;
        case 'y': ide_command(cmRedo); return 1;
        case 'x': ide_command(cmCut); return 1;
        case 'c': ide_command(cmCopy); return 1;
        case 'v': ide_command(cmPaste); return 1;
        case 'a': if (1) { w->sel_on = 1; w->sl = 0; w->sc = 0; w->cy = b->n - 1; w->cx = b->ln[b->n - 1].len; ed_ensure_visible(w); } return 1;
        }
    }
    if (ctrl && !alt && !shift && (base == 'k' || base == 'q')) { w->kprefix = base; return 1; }

    /* movement */
    int ext = shift != 0;
    switch (base) {
    case KEY_LEFT:
        if (ctrl) { word_left(w, &l, &c); set_cursor(w, l, c, ext); }
        else if (w->cx > 0) set_cursor(w, w->cy, w->cx - 1, ext);
        else if (w->cy > 0) set_cursor(w, w->cy - 1, b->ln[w->cy - 1].len, ext);
        else set_cursor(w, w->cy, w->cx, ext);
        return 1;
    case KEY_RIGHT:
        if (ctrl) { word_right(w, &l, &c); set_cursor(w, l, c, ext); }
        else if (w->cx < b->ln[w->cy].len) set_cursor(w, w->cy, w->cx + 1, ext);
        else if (w->cy < b->n - 1) set_cursor(w, w->cy + 1, 0, ext);
        else set_cursor(w, w->cy, w->cx, ext);
        return 1;
    case KEY_UP: case KEY_DOWN: {
        if (ctrl && !shift) { /* scroll without moving the cursor */
            w->top += base == KEY_UP ? -1 : 1;
            if (w->top < 0) w->top = 0;
            if (w->top > b->n - 1) w->top = b->n - 1;
            if (w->cy < w->top) w->cy = w->top;
            if (w->cy >= w->top + vh(w)) w->cy = w->top + vh(w) - 1;
            if (w->cy >= b->n) w->cy = b->n - 1;
            w->cx = idx_of(&b->ln[w->cy], w->want);
            return 1;
        }
        int nl = w->cy + (base == KEY_UP ? -1 : 1);
        if (nl < 0 || nl >= b->n) { set_cursor(w, w->cy, w->cx, ext); return 1; }
        int want = w->want;
        set_cursor(w, nl, idx_of(&b->ln[nl], want), ext);
        w->want = want;
        return 1;
    }
    case KEY_HOME:
        if (ctrl) set_cursor(w, w->top, 0, ext);
        else {
            Line *L = &b->ln[w->cy]; int i = 0;
            while (i < L->len && (L->s[i] == ' ' || L->s[i] == '\t')) i++;
            set_cursor(w, w->cy, (w->cx == i || i == L->len) ? 0 : i, ext);
        }
        return 1;
    case KEY_END:
        if (ctrl) { int ln = w->top + vh(w) - 1; if (ln >= b->n) ln = b->n - 1; set_cursor(w, ln, b->ln[ln].len, ext); }
        else set_cursor(w, w->cy, b->ln[w->cy].len, ext);
        return 1;
    case KEY_PGUP: case KEY_PGDN: {
        int h = vh(w) - 1; if (h < 1) h = 1;
        if (ctrl) { if (base == KEY_PGUP) set_cursor(w, 0, 0, ext); else set_cursor(w, b->n - 1, b->ln[b->n - 1].len, ext); return 1; }
        int nl = w->cy + (base == KEY_PGUP ? -h : h);
        if (nl < 0) nl = 0; if (nl >= b->n) nl = b->n - 1;
        w->top += base == KEY_PGUP ? -h : h;
        if (w->top < 0) w->top = 0;
        int want = w->want;
        set_cursor(w, nl, idx_of(&b->ln[nl], want), ext);
        w->want = want;
        return 1;
    }
    }

    if (base == KEY_INS) {
        if (ctrl) { ed_copy(w, 0); return 1; }
        if (shift) { ide_command(cmPaste); return 1; }
        w->insert = !w->insert; return 1;
    }
    if (base == KEY_DEL) {
        if (shift) { ed_copy(w, 1); return 1; }
        if (ctrl) { begin_edit(w, 0); ed_clear_block(w); return 1; }
        if (b->readonly) return 1;
        do_delete(w); return 1;
    }
    if (b->readonly) {
        if (key == KEY_ENTER || (base >= 32 && base < 127 && !ctrl && !alt)) return 1;
    }
    if (alt && base == KEY_BKSP) { ide_command(shift ? cmRedo : cmUndo); return 1; }
    if (key == KEY_BKSP) { do_backspace(w); return 1; }
    if (key == KEY_ENTER) { do_enter(w); return 1; }
    if (key == KEY_TAB) { do_tab(w); return 1; }
    if (key == (K_SHIFT | KEY_TAB)) return 1;

    if (ctrl && !alt) {   /* WordStar-style keys */
        switch (base) {
        case 'a': word_left(w, &l, &c); set_cursor(w, l, c, 0); return 1;
        case 'f': word_right(w, &l, &c); set_cursor(w, l, c, 0); return 1;
        case 's': ed_key(w, KEY_LEFT); return 1;
        case 'd': ed_key(w, KEY_RIGHT); return 1;
        case 'e': ed_key(w, KEY_UP); return 1;
        case 'x': ed_key(w, KEY_DOWN); return 1;
        case 'r': ed_key(w, KEY_PGUP); return 1;
        case 'c': ed_key(w, KEY_PGDN); return 1;
        case 'g': do_delete(w); return 1;
        case 't': {
            word_right(w, &l, &c);
            begin_edit(w, 0);
            if (l == w->cy && c > w->cx) buf_delete(b, w->cy, w->cx, l, c);
            return 1;
        }
        case 'y': do_delete_line(w); return 1;
        case 'n': begin_edit(w, 0); { int sl = w->cy, sc = w->cx; buf_insert(b, &sl, &sc, "\n", 1); } return 1;
        case 'v': w->insert = !w->insert; return 1;
        case 'u': ide_command(cmUndo); return 1;
        case 'w': if (w->top > 0) { w->top--; if (w->cy >= w->top + vh(w)) w->cy = w->top + vh(w) - 1; } return 1;
        case 'z': if (w->top < b->n - 1) { w->top++; if (w->cy < w->top) w->cy = w->top; } return 1;
        }
        return 0;
    }
    if (!alt && base >= 32 && base < 127) { if (b->readonly) return 1; do_insert_char(w, base); return 1; }
    if (!alt && base >= 128 && base < 256) { do_insert_char(w, base); return 1; }
    return 0;
}

/* mouse in the interior of an edit window */
void ed_mouse(Win *w)
{
    Buf *b = w->buf;
    int ix = ms.x - (w->x + 1), iy = ms.y - (w->y + 1);
    if (ms.ev == MS_WHEEL_UP || ms.ev == MS_WHEEL_DOWN) {
        w->top += ms.ev == MS_WHEEL_UP ? -3 : 3;
        if (w->top < 0) w->top = 0;
        if (w->top > b->n - 1) w->top = b->n - 1;
        if (w->cy < w->top) w->cy = w->top;
        if (w->cy >= w->top + vh(w)) w->cy = w->top + vh(w) - 1;
        if (w->cy >= b->n) w->cy = b->n - 1;
        w->cx = idx_of(&b->ln[w->cy], w->want);
        return;
    }
    if (ms.ev != MS_PRESS && ms.ev != MS_DRAG) return;
    if (ms.btn != 0) return;
    int ln = w->top + iy;
    if (ln >= b->n) ln = b->n - 1;
    if (ln < 0) ln = 0;
    int idx = idx_of(&b->ln[ln], ix + w->left);
    if (ms.ev == MS_PRESS) {
        w->sel_on = 0;
        w->sl = ln; w->sc = idx;
        w->cy = ln; w->cx = idx;
    } else {
        if (!w->sel_on) { w->sel_on = 1; }
        w->cy = ln; w->cx = idx;
    }
    w->want = col_of(&b->ln[w->cy], w->cx);
    w->buf->last_kind = -1;
    if (ms.ev == MS_DRAG) {
        if (iy < 0 && w->top > 0) w->top--;
        if (iy >= vh(w) && w->top < b->n - 1) w->top++;
    }
    ed_ensure_visible(w);
}
