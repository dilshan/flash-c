/*
 * File:    buf.c
 * Purpose: Flash C IDE - text buffers, undo, syntax highlighting, search
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "ide.h"
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

int ur_l, ur_c;   /* cursor position to restore on undo (set by the editor before each edit) */

static void line_reserve(Line *l, int need)
{
    if (need + 1 > l->cap) {
        int nc = l->cap ? l->cap * 2 : 32;
        while (nc < need + 1) nc *= 2;
        l->s = realloc(l->s, nc); l->cap = nc;
    }
}
static void line_set(Line *l, const char *s, int n)
{
    line_reserve(l, n);
    memcpy(l->s, s, n); l->len = n; l->s[n] = 0;
}
static void lines_reserve(Buf *b, int need)
{
    if (need > b->cap) {
        int nc = b->cap ? b->cap * 2 : 64;
        while (nc < need) nc *= 2;
        b->ln = realloc(b->ln, nc * sizeof(Line));
        memset(b->ln + b->cap, 0, (nc - b->cap) * sizeof(Line));
        b->cap = nc;
    }
}

Buf *buf_new(void)
{
    Buf *b = calloc(1, sizeof *b);
    lines_reserve(b, 16);
    b->n = 1; line_set(&b->ln[0], "", 0);
    return b;
}

static void undo_clear(Buf *b)
{
    for (int i = 0; i < b->nu; i++) free(b->u[i].text);
    b->nu = b->ucur = 0;
}

void buf_free(Buf *b)
{
    if (!b) return;
    for (int i = 0; i < b->cap; i++) free(b->ln[i].s);
    free(b->ln); undo_clear(b); free(b->u); free(b->cst); free(b);
}

void buf_clear(Buf *b)
{
    for (int i = 0; i < b->n; i++) b->ln[i].len = 0, b->ln[i].s ? b->ln[i].s[0] = 0 : 0;
    b->n = 1; undo_clear(b); b->dirty = 0; b->cst_n = 0;
}

int buf_load(Buf *b, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *data = malloc(sz + 1);
    size_t got = fread(data, 1, sz, f);
    fclose(f);
    data[got] = 0;
    for (int i = 0; i < b->cap; i++) b->ln[i].len = 0;
    b->n = 0;
    long s = 0;
    for (long i = 0; i <= (long)got; i++) {
        if (i == (long)got || data[i] == '\n') {
            long e = i;
            if (e > s && data[e - 1] == '\r') e--;
            lines_reserve(b, b->n + 1);
            line_set(&b->ln[b->n++], data + s, (int)(e - s));
            s = i + 1;
        }
    }
    free(data);
    if (!b->n) { b->n = 1; line_set(&b->ln[0], "", 0); }
    undo_clear(b);
    b->modified = 0; b->dirty = 0; b->cst_n = 0;
    snprintf(b->name, sizeof b->name, "%s", path); b->named = 1;
    return 0;
}

int buf_save(Buf *b, const char *path, int backup)
{
    if (backup && file_exists(path)) {
        char bak[620]; snprintf(bak, sizeof bak, "%s", path);
        strcat(bak, "~");                       /* file.c -> file.c~ */
        rename(path, bak);
    }
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    for (int i = 0; i < b->n; i++) {
        fwrite(b->ln[i].s, 1, b->ln[i].len, f);
        if (i < b->n - 1) fputc('\n', f);
    }
    int err = ferror(f);
    fclose(f);
    if (err) return 1;
    b->modified = 0;
    if (path != b->name) snprintf(b->name, sizeof b->name, "%s", path);   /* path may alias the name */
    b->named = 1;
    return 0;
}

char *buf_all(Buf *b, int *len)
{
    char *t = buf_get(b, 0, 0, b->n - 1, b->ln[b->n - 1].len);
    if (len) *len = (int)strlen(t);
    return t;
}

char *buf_get(Buf *b, int l1, int c1, int l2, int c2)
{
    size_t total = 1;
    for (int l = l1; l <= l2; l++) total += b->ln[l].len + 1;
    char *r = malloc(total), *p = r;
    for (int l = l1; l <= l2; l++) {
        int s = l == l1 ? c1 : 0, e = l == l2 ? c2 : b->ln[l].len;
        if (s > b->ln[l].len) s = b->ln[l].len;
        if (e > b->ln[l].len) e = b->ln[l].len;
        if (e > s) { memcpy(p, b->ln[l].s + s, e - s); p += e - s; }
        if (l < l2) *p++ = '\n';
    }
    *p = 0;
    return r;
}

static void touch(Buf *b, int line)
{
    b->modified = 1; b->version++;
    if (line < b->dirty || !b->cst_n) b->dirty = line;
    if (b->cst_n > line + 1) b->cst_n = line + 1;
}

/* raw operations (no undo); *el,*ec receive the end position of an insertion */
static void raw_insert(Buf *b, int l, int c, const char *t, int n, int *el, int *ec)
{
    Line *ln = &b->ln[l];
    if (c > ln->len) c = ln->len;
    char *tail = malloc(ln->len - c + 1);
    int tl = ln->len - c;
    memcpy(tail, ln->s + c, tl);
    ln->len = c; ln->s[c] = 0;
    int cur = l, i = 0;
    while (i <= n) {
        int j = i;
        while (j < n && t[j] != '\n') j++;
        Line *cl = &b->ln[cur];
        line_reserve(cl, cl->len + (j - i));
        memcpy(cl->s + cl->len, t + i, j - i); cl->len += j - i; cl->s[cl->len] = 0;
        if (j >= n) break;
        /* new line after cur */
        lines_reserve(b, b->n + 1);
        memmove(&b->ln[cur + 2], &b->ln[cur + 1], (b->n - cur - 1) * sizeof(Line));
        memset(&b->ln[cur + 1], 0, sizeof(Line));
        b->n++;
        cur++;
        line_set(&b->ln[cur], "", 0);
        i = j + 1;
    }
    Line *cl = &b->ln[cur];
    *el = cur; *ec = cl->len;
    line_reserve(cl, cl->len + tl);
    memcpy(cl->s + cl->len, tail, tl); cl->len += tl; cl->s[cl->len] = 0;
    free(tail);
    touch(b, l);
}

static void raw_delete(Buf *b, int l1, int c1, int l2, int c2)
{
    Line *a = &b->ln[l1], *z = &b->ln[l2];
    if (c1 > a->len) c1 = a->len;
    if (c2 > z->len) c2 = z->len;
    if (l1 == l2) {
        memmove(a->s + c1, a->s + c2, a->len - c2 + 1);
        a->len -= c2 - c1;
    } else {
        int tl = z->len - c2;
        line_reserve(a, c1 + tl);
        memcpy(a->s + c1, z->s + c2, tl); a->len = c1 + tl; a->s[a->len] = 0;
        for (int i = l1 + 1; i <= l2; i++) { free(b->ln[i].s); b->ln[i].s = NULL; b->ln[i].cap = b->ln[i].len = 0; }
        memmove(&b->ln[l1 + 1], &b->ln[l2 + 1], (b->n - l2 - 1) * sizeof(Line));
        int removed = l2 - l1;
        memset(&b->ln[b->n - removed], 0, removed * sizeof(Line));
        b->n -= removed;
    }
    touch(b, l1);
}

static void undo_push(Buf *b, int type, int l, int c, int l2, int c2, char *text)
{
    for (int i = b->ucur; i < b->nu; i++) free(b->u[i].text);
    b->nu = b->ucur;
    if (b->nu == b->capu) { b->capu = b->capu ? b->capu * 2 : 256; b->u = realloc(b->u, b->capu * sizeof(Undo)); }
    Undo *u = &b->u[b->nu++];
    u->type = type; u->l = l; u->c = c; u->l2 = l2; u->c2 = c2; u->text = text; u->group = b->group; u->cl = ur_l; u->cc = ur_c;
    b->ucur = b->nu;
}

void buf_group(Buf *b) { b->group++; }

void buf_insert(Buf *b, int *l, int *c, const char *text, int n)
{
    if (n < 0) n = (int)strlen(text);
    if (!n) return;
    int sl = *l, sc = *c, el, ec;
    if (sc > b->ln[sl].len) sc = b->ln[sl].len;
    raw_insert(b, sl, sc, text, n, &el, &ec);
    undo_push(b, 0, sl, sc, el, ec, strndup(text, n));
    *l = el; *c = ec;
}

void buf_delete(Buf *b, int l1, int c1, int l2, int c2)
{
    if (l1 == l2 && c1 >= c2) return;
    if (c1 > b->ln[l1].len) c1 = b->ln[l1].len;
    if (c2 > b->ln[l2].len) c2 = b->ln[l2].len;
    char *t = buf_get(b, l1, c1, l2, c2);
    raw_delete(b, l1, c1, l2, c2);
    undo_push(b, 1, l1, c1, l2, c2, t);
}

int buf_undo(Buf *b, int *l, int *c)
{
    if (!b->ucur) return 0;
    int g = b->u[b->ucur - 1].group;
    while (b->ucur && b->u[b->ucur - 1].group == g) {
        Undo *u = &b->u[--b->ucur];
        if (u->type == 0) raw_delete(b, u->l, u->c, u->l2, u->c2);
        else { int el, ec; raw_insert(b, u->l, u->c, u->text, (int)strlen(u->text), &el, &ec); }
        *l = u->cl; *c = u->cc;
        if (*l >= b->n) *l = b->n - 1;
    }
    return 1;
}

int buf_redo(Buf *b, int *l, int *c)
{
    if (b->ucur >= b->nu) return 0;
    int g = b->u[b->ucur].group;
    while (b->ucur < b->nu && b->u[b->ucur].group == g) {
        Undo *u = &b->u[b->ucur++];
        if (u->type == 0) { int el, ec; raw_insert(b, u->l, u->c, u->text, (int)strlen(u->text), &el, &ec); *l = el; *c = ec; }
        else { raw_delete(b, u->l, u->c, u->l2, u->c2); *l = u->l; *c = u->c; }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* syntax highlighting                                                 */
/* ------------------------------------------------------------------ */
static const char *kw[] = {
    "auto","break","case","char","const","continue","default","do","double","else","enum","extern","float","for",
    "goto","if","int","long","register","return","short","signed","sizeof","static","struct","switch","typedef",
    "union","unsigned","void","volatile","while","near","far","huge","cdecl","pascal","interrupt","asm", NULL
};
static int is_kw(const char *s, int n)
{
    for (int i = 0; kw[i]; i++) if ((int)strlen(kw[i]) == n && !memcmp(kw[i], s, n)) return 1;
    return 0;
}

/* scan one line from 'state' (1 = inside comment); optionally fills attrs; returns end state */
static int scan_line(const char *s, int n, int state, uint8_t *at)
{
    int i = 0, first = 1;
    while (i < n) {
        if (state) {
            int j = i;
            while (j < n && !(s[j] == '*' && j + 1 < n && s[j + 1] == '/')) j++;
            int e = j < n ? j + 2 : n;
            if (at) for (int k = i; k < e; k++) at[k] = pal[P_ED_COMMENT];
            if (j < n) state = 0;
            i = e; continue;
        }
        unsigned char c = s[i];
        if (c == ' ' || c == '\t') { if (at) at[i] = pal[P_ED_NORM]; i++; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { state = 1; if (at) { at[i] = at[i + 1] = pal[P_ED_COMMENT]; } i += 2; first = 0; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { if (at) for (int k = i; k < n; k++) at[k] = pal[P_ED_COMMENT]; return 0; }
        if (c == '#' && first) {
            int k = i;
            while (k < n) {
                if (s[k] == '/' && k + 1 < n && s[k + 1] == '*') break;
                if (s[k] == '/' && k + 1 < n && s[k + 1] == '/') break;
                if (at) at[k] = pal[P_ED_PRE];
                k++;
            }
            i = k; first = 0; continue;
        }
        first = 0;
        if (isalpha(c) || c == '_') {
            int j = i;
            while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            uint8_t a = is_kw(s + i, j - i) ? pal[P_ED_RESERVED] : pal[P_ED_IDENT];
            if (at) for (int k = i; k < j; k++) at[k] = a;
            i = j; continue;
        }
        if (isdigit(c)) {
            int j = i;
            while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '.')) j++;
            if (at) for (int k = i; k < j; k++) at[k] = pal[P_ED_INTEGER];
            i = j; continue;
        }
        if (c == '"' || c == '\'') {
            int j = i + 1;
            while (j < n && s[j] != c) { if (s[j] == '\\') j++; j++; }
            if (j < n) j++;
            if (j > n) j = n;
            if (at) for (int k = i; k < j; k++) at[k] = pal[P_ED_STRING];
            i = j; continue;
        }
        if (at) at[i] = (c < 32 || c >= 127) ? pal[P_ED_ILLEGAL] : pal[P_ED_SYMBOL];
        i++;
    }
    return state;
}

int syntax_line_state(Buf *b, int line)
{
    if (line <= 0) return 0;
    if (b->cst_n > b->n) b->cst_n = b->n;
    if (b->cst_cap < b->cap + 2) { b->cst_cap = b->cap + 2; b->cst = realloc(b->cst, b->cst_cap); }
    if (b->cst_n == 0) { b->cst[0] = 0; b->cst_n = 1; }
    while (b->cst_n <= line && b->cst_n <= b->n) {
        int i = b->cst_n - 1;
        b->cst[b->cst_n] = scan_line(b->ln[i].s, b->ln[i].len, b->cst[i], NULL);
        b->cst_n++;
    }
    return line < b->cst_n ? b->cst[line] : 0;
}

void syntax_line_attrs(Buf *b, int line, uint8_t *attrs, int n)
{
    for (int i = 0; i < n; i++) attrs[i] = pal[P_ED_NORM];
    if (!cfg.syntax) return;
    int st = syntax_line_state(b, line);
    scan_line(b->ln[line].s, b->ln[line].len < n ? b->ln[line].len : n, st, attrs);
}

/* ------------------------------------------------------------------ */
/* search                                                              */
/* ------------------------------------------------------------------ */
static int eqc(int a, int b, int cs) { return cs ? a == b : tolower(a) == tolower(b); }

static const char *atom_end(const char *re)
{
    if (*re == '\\' && re[1]) return re + 2;
    if (*re == '[') {
        const char *p = re + 1;
        if (*p == '^') p++;
        if (*p == ']') p++;
        while (*p && *p != ']') p++;
        return *p ? p + 1 : p;
    }
    return re + 1;
}
static int atom_match(const char *re, const char *end, int c, int cs)
{
    if (!c) return 0;
    if (*re == '.') return 1;
    if (*re == '\\') {
        switch (re[1]) {
        case 'd': return isdigit(c); case 'w': return isalnum(c) || c == '_'; case 's': return isspace(c);
        case 't': return c == '\t'; case 'n': return c == '\n';
        default: return eqc(c, re[1], cs);
        }
    }
    if (*re == '[') {
        const char *p = re + 1; int neg = 0, ok = 0;
        if (*p == '^') { neg = 1; p++; }
        const char *close = end - 1;
        for (; p < close; p++) {
            if (p + 2 < close && p[1] == '-') {
                int lo = (unsigned char)p[0], hi = (unsigned char)p[2];
                if (cs ? (c >= lo && c <= hi) : (tolower(c) >= tolower(lo) && tolower(c) <= tolower(hi))) ok = 1;
                p += 2;
            } else if (eqc(c, (unsigned char)*p, cs)) ok = 1;
        }
        return ok != neg;
    }
    return eqc(c, (unsigned char)*re, cs);
}

static int rx_here(const char *re, const char *t, const char *tstart, const char *tend, int cs, const char **mend)
{
    if (!*re) { *mend = t; return 1; }
    if (*re == '$' && !re[1]) { if (t == tend) { *mend = t; return 1; } return 0; }
    const char *ae = atom_end(re);
    char q = *ae;
    if (q == '*' || q == '+' || q == '?') {
        int min = q == '+' ? 1 : 0, max = q == '?' ? 1 : 1 << 30, cnt = 0;
        const char *p = t;
        while (cnt < max && p < tend && atom_match(re, ae, (unsigned char)*p, cs)) { p++; cnt++; }
        for (; cnt >= min; cnt--, p--) if (rx_here(ae + 1, p, tstart, tend, cs, mend)) return 1;
        return 0;
    }
    if (t < tend && atom_match(re, ae, (unsigned char)*t, cs)) return rx_here(ae, t + 1, tstart, tend, cs, mend);
    return 0;
}

static int isw(int c) { return isalnum(c) || c == '_'; }

int text_find(const char *s, int len, int from, const char *pat, int cs, int whole, int rx, int *mlen)
{
    int pl = (int)strlen(pat);
    if (!pl) return -1;
    for (int i = from; i <= len; i++) {
        int ml = -1;
        if (rx) {
            const char *re = pat, *end;
            if (*re == '^') { if (i != 0) { if (i > 0) break; } re++; }
            if (rx_here(re, s + i, s, s + len, cs, &end)) ml = (int)(end - (s + i));
            else if (pat[0] == '^') break;
        } else {
            if (i + pl > len) break;
            int k = 0;
            while (k < pl && eqc((unsigned char)s[i + k], (unsigned char)pat[k], cs)) k++;
            if (k == pl) ml = pl;
        }
        if (ml < 0) continue;
        if (ml == 0 && rx) continue;
        if (whole) {
            if (i > 0 && isw((unsigned char)s[i - 1])) continue;
            if (i + ml < len && isw((unsigned char)s[i + ml])) continue;
        }
        *mlen = ml;
        return i;
    }
    return -1;
}

int text_find_rev(const char *s, int len, int before, const char *pat, int cs, int whole, int rx, int *mlen)
{
    int best = -1, bl = 0, from = 0;
    for (;;) {
        int ml, p = text_find(s, len, from, pat, cs, whole, rx, &ml);
        if (p < 0 || p >= before) break;
        best = p; bl = ml; from = p + 1;
    }
    if (best >= 0) *mlen = bl;
    return best;
}
