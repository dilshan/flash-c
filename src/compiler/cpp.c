/* Flash C - lexer and preprocessor */
#include "fci.h"
#include <ctype.h>
#include <time.h>

void tv_push(TokVec *t, Tok x)
{
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 256;
        t->v = realloc(t->v, t->cap * sizeof(Tok));
        if (!t->v) { fprintf(stderr, "out of memory\n"); exit(2); }
    }
    t->v[t->n++] = x;
}

/* ---------- punctuators ---------- */
static const struct { const char *s; int k; } puncts[] = {
    {"...", P_ELLIPSIS}, {"<<=", P_SHLA}, {">>=", P_SHRA},
    {"->", P_ARROW}, {"++", P_INC}, {"--", P_DEC}, {"<<", P_SHL}, {">>", P_SHR}, {"<=", P_LE}, {">=", P_GE},
    {"==", P_EQ}, {"!=", P_NE}, {"&&", P_ANDAND}, {"||", P_OROR}, {"*=", P_MULA}, {"/=", P_DIVA},
    {"%=", P_MODA}, {"+=", P_ADDA}, {"-=", P_SUBA}, {"&=", P_ANDA}, {"^=", P_XORA}, {"|=", P_ORA}, {"##", P_HASHHASH},
    {"(", P_LPAR}, {")", P_RPAR}, {"[", P_LBRK}, {"]", P_RBRK}, {"{", P_LBRC}, {"}", P_RBRC}, {";", P_SEMI},
    {",", P_COMMA}, {".", P_DOT}, {"&", P_AMP}, {"*", P_STAR}, {"+", P_PLUS}, {"-", P_MINUS}, {"~", P_TILDE},
    {"!", P_BANG}, {"/", P_SLASH}, {"%", P_PERCENT}, {"<", P_LT}, {">", P_GT}, {"^", P_CARET}, {"|", P_BAR},
    {"?", P_QUEST}, {":", P_COLON}, {"=", P_ASSIGN}, {"#", P_HASH}, {NULL, 0}
};
static const char *kwnames[] = {
    "auto","break","case","char","const","continue","default","do","double","else","enum","extern",
    "float","for","goto","if","int","long","register","return","short","signed","sizeof","static",
    "struct","switch","typedef","union","unsigned","void","volatile","while", NULL
};
static const char *ignorekw[] = {"near","far","huge","cdecl","pascal","interrupt","_near","_far","_huge",
    "_cdecl","_pascal","__near","__far","__huge","__cdecl","__pascal","inline","__inline","restrict",
    "__restrict", NULL};

const char *tok_spell(Tok *t)
{
    static char buf[4][600]; static int bi;
    char *b = buf[bi++ & 3];
    switch (t->k) {
    case TK_EOF: return "end of file";
    case TK_ID: return t->s;
    case TK_NUM: case TK_CHR: return t->s ? t->s : "number";
    case TK_STR: {
        int j = 0;
        if (t->wide) b[j++] = 'L';
        b[j++] = '"';
        for (int i = 0; i < t->slen && j < 590; i++) {
            unsigned char c = t->wide ? (unsigned char)((int *)t->s)[i] : t->s[i];
            if (c == '"' || c == '\\') b[j++] = '\\';
            if (c == '\n') { b[j++] = '\\'; c = 'n'; }
            b[j++] = c;
        }
        b[j++] = '"'; b[j] = 0; return b;
    }
    default: break;
    }
    for (int i = 0; puncts[i].s; i++) if (puncts[i].k == t->k) return puncts[i].s;
    if (t->k >= K_AUTO && t->k <= K_WHILE) return kwnames[t->k - K_AUTO];
    return "?";
}

static void kw_convert(TokVec *tv, int from)
{
    /* the intern table is reset with the arena, so look the names up each time */
    char *k2[40]; int nk = 0;
    for (; kwnames[nk]; nk++) k2[nk] = intern(kwnames[nk], (int)strlen(kwnames[nk]));
    char *i2[24]; int ni = 0;
    for (; ignorekw[ni]; ni++) i2[ni] = intern(ignorekw[ni], (int)strlen(ignorekw[ni]));
    for (int i = from; i < tv->n; i++) {
        Tok *t = &tv->v[i];
        if (t->k != TK_ID) continue;
        for (int j = 0; j < nk; j++) if (t->s == k2[j]) { t->k = K_AUTO + j; goto next; }
        if (CX->opt.ansi_keywords && !file_is_builtin(t->fileid)) continue;   /* ANSI only: the extended keywords are plain identifiers */
        for (int j = 0; j < ni; j++) if (t->s == i2[j]) { t->k = K_IGNORE; goto next; }
    next:;
    }
}

/* ---------- lexer ---------- */
static int esc_char(const char *b, int len, int *i, int fileid, int line)
{
    int c = b[(*i)++];
    switch (c) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'a': return 7;
    case 'b': return 8; case 'f': return 12; case 'v': return 11; case 'e': return 27;
    case '\\': return '\\'; case '\'': return '\''; case '"': return '"'; case '?': return '?';
    case 'x': {
        int v = 0, n = 0;
        while (*i < len && isxdigit((unsigned char)b[*i])) { int d = b[*i]; v = v * 16 + (isdigit(d) ? d - '0' : (tolower(d) - 'a' + 10)); (*i)++; n++; }
        if (!n) fc_error_nj(fileid, line, "Numeric constant too large");
        return v & 0xff;
    }
    default:
        if (c >= '0' && c <= '7') {
            int v = c - '0', n = 1;
            while (n < 3 && *i < len && b[*i] >= '0' && b[*i] <= '7') { v = v * 8 + (b[(*i)++] - '0'); n++; }
            return v & 0xff;
        }
        return c;
    }
}

static int lex_raw(const char *b, int len, int fileid, TokVec *out);

/* ANSI trigraph sequences are replaced before anything else is looked at */
int lex_buffer(const char *b, int len, int fileid, TokVec *out)
{
    int has = 0;
    for (int i = 0; i + 2 < len; i++) if (b[i] == '?' && b[i + 1] == '?') { has = 1; break; }
    if (!has) return lex_raw(b, len, fileid, out);
    char *c = malloc(len + 1); int n = 0;
    for (int i = 0; i < len; i++) {
        if (b[i] == '?' && i + 2 < len && b[i + 1] == '?') {
            const char *p = strchr("=(/)'<!>-", b[i + 2]);
            if (p && b[i + 2]) { c[n++] = "#[\\]^{|}~"[p - "=(/)'<!>-"]; i += 2; continue; }
        }
        c[n++] = b[i];
    }
    int r = lex_raw(c, n, fileid, out);
    free(c);
    return r;
}

static int lex_raw(const char *b, int len, int fileid, TokVec *out)
{
    int i = 0, line = 1, bol = 1, spc = 0;
    int bad = 0;
    while (i < len) {
        unsigned char c = b[i];
        if (c == '\n') { line++; i++; bol = 1; spc = 1; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v' || c == 26) { i++; spc = 1; continue; }
        if (c == '\\' && i + 1 < len && (b[i + 1] == '\n' || (b[i + 1] == '\r' && i + 2 < len && b[i + 2] == '\n'))) {
            i += b[i + 1] == '\n' ? 2 : 3; line++; spc = 1; continue;
        }
        if (c == '/' && i + 1 < len && b[i + 1] == '*') {
            int start = line, depth = 1; i += 2;
            while (i < len && depth) {
                if (b[i] == '\n') line++;
                if (b[i] == '*' && i + 1 < len && b[i + 1] == '/') { depth--; i += 2; continue; }
                if (CX->opt.nested_comments && b[i] == '/' && i + 1 < len && b[i + 1] == '*') { depth++; i += 2; continue; }
                i++;
            }
            if (depth) { fc_error_nj(fileid, start, "Unexpected end of file in comment started on line %d", start); bad = 1; }
            spc = 1; continue;
        }
        if (c == '/' && i + 1 < len && b[i + 1] == '/') {
            if (CX->opt.ansi_keywords && !file_is_builtin(fileid)) { fc_error_nj(fileid, line, "// comments are not allowed with ANSI keywords"); bad = 1; }
            while (i < len && b[i] != '\n') i++;
            spc = 1; continue;
        }
        Tok t; memset(&t, 0, sizeof t);
        t.line = line; t.fileid = fileid; t.bol = bol; t.spc = spc;
        bol = 0; spc = 0;
        if ((isalpha(c) || c == '_' || c == '$') && !(c == 'L' && i + 1 < len && (b[i + 1] == '\'' || b[i + 1] == '"'))) {
            int s = i;
            while (i < len && (isalnum((unsigned char)b[i]) || b[i] == '_' || b[i] == '$')) i++;
            int n = i - s;
            if (CX->opt.ident_len > 0 && n > CX->opt.ident_len) n = CX->opt.ident_len;
            t.k = TK_ID; t.s = intern(b + s, n); t.slen = n;
            tv_push(out, t); continue;
        }
        if (isdigit(c) || (c == '.' && i + 1 < len && isdigit((unsigned char)b[i + 1]))) {
            int s = i, isf = 0; long long v = 0;
            if (c == '0' && i + 1 < len && (b[i + 1] == 'x' || b[i + 1] == 'X')) {
                i += 2;
                while (i < len && isxdigit((unsigned char)b[i])) { int d = b[i++]; v = v * 16 + (isdigit(d) ? d - '0' : tolower(d) - 'a' + 10); }
            } else if (c == '0' && i + 1 < len && (b[i + 1] == 'b' || b[i + 1] == 'B')) {
                i += 2; while (i < len && (b[i] == '0' || b[i] == '1')) v = v * 2 + (b[i++] - '0');
            } else {
                int j = i;
                while (j < len && (isalnum((unsigned char)b[j]) || b[j] == '.' || ((b[j] == '+' || b[j] == '-') && (b[j - 1] == 'e' || b[j - 1] == 'E') && !(b[s] == '0' && j > s + 1 && (b[s+1]=='x'))))) {
                    if (b[j] == '.' || b[j] == 'e' || b[j] == 'E') isf = 1;
                    j++;
                }
                if (isf) i = j;
                else if (c == '0') { while (i < len && b[i] >= '0' && b[i] <= '7') v = v * 8 + (b[i++] - '0'); }
                else while (i < len && isdigit((unsigned char)b[i])) v = v * 10 + (b[i++] - '0');
            }
            if (!isf) while (i < len && (b[i] == 'u' || b[i] == 'U' || b[i] == 'l' || b[i] == 'L')) {
                if (b[i] == 'u' || b[i] == 'U') t.uns = 1; else t.lng = 1; i++;
            }
            t.k = TK_NUM; t.val = v; t.isfloat = isf; t.s = intern(b + s, i - s); t.slen = i - s;
            if (isf) {
                char tmp[128]; int tl = i - s < 127 ? i - s : 127;
                memcpy(tmp, b + s, tl); tmp[tl] = 0;
                while (tl > 0 && (tmp[tl - 1] == 'f' || tmp[tl - 1] == 'F' || tmp[tl - 1] == 'l' || tmp[tl - 1] == 'L')) { if (tmp[tl - 1] == 'f' || tmp[tl - 1] == 'F') t.fsuf = 1; tmp[--tl] = 0; }
                t.dval = strtod(tmp, NULL);
            }
            tv_push(out, t); continue;
        }
        if (c == 'L' && i + 1 < len && (b[i + 1] == '\'' || b[i + 1] == '"')) {      /* wide character / string */
            int q = b[i + 1], s = i; i += 2;
            int *w = malloc((len - i + 2) * sizeof(int)); int n = 0;
            while (i < len && b[i] != q && b[i] != '\n') {
                int ch;
                if (b[i] == '\\') {
                    i++;
                    if (i < len && b[i] == '\n') { i++; line++; continue; }
                    ch = esc_char(b, len, &i, fileid, line);
                    if (ch < 0) ch &= 0xff;
                } else {
                    unsigned char u = b[i++];
                    ch = u;
                    int extra = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;      /* UTF-8 source text */
                    if (extra) { ch = u & (0x3F >> extra); while (extra-- && i < len && ((unsigned char)b[i] & 0xC0) == 0x80) ch = (ch << 6) | ((unsigned char)b[i++] & 0x3F); }
                }
                w[n++] = ch;
            }
            if (i >= len || b[i] != q) { fc_error_nj(fileid, line, "Unterminated string or character constant"); bad = 1; }
            else i++;
            t.wide = 1;
            if (q == '\'') { t.k = TK_CHR; t.val = n ? w[n - 1] : 0; t.s = intern(b + s, i - s); t.slen = i - s; free(w); }
            else { t.k = TK_STR; w[n] = 0; t.s = (char *)w; t.slen = n; }
            tv_push(out, t); continue;
        }
        if (c == '\'') {
            i++; long long v = 0; int n = 0;
            int s = i - 1;
            while (i < len && b[i] != '\'' && b[i] != '\n') {
                int ch;
                if (b[i] == '\\') { i++; ch = esc_char(b, len, &i, fileid, line); }
                else ch = (unsigned char)b[i++];
                v = (v << 8) | (ch & 0xff); n++;
            }
            if (i >= len || b[i] != '\'') { fc_error_nj(fileid, line, "Unterminated string or character constant"); bad = 1; }
            else i++;
            if (n == 1 && !CX->opt.unsigned_char) v = (signed char)v;
            t.k = TK_CHR; t.val = v; t.s = intern(b + s, i - s); t.slen = i - s;
            tv_push(out, t); continue;
        }
        if (c == '"') {
            i++; char *buf = malloc(len - i + 2); int n = 0;
            while (i < len && b[i] != '"' && b[i] != '\n') {
                if (b[i] == '\\') {
                    i++;
                    if (i < len && b[i] == '\n') { i++; line++; continue; }
                    buf[n++] = (char)esc_char(b, len, &i, fileid, line);
                } else buf[n++] = b[i++];
            }
            if (i >= len || b[i] != '"') { fc_error_nj(fileid, line, "Unterminated string or character constant"); bad = 1; }
            else i++;
            t.k = TK_STR; t.s = xstrndup(buf, n); t.slen = n; free(buf);
            tv_push(out, t); continue;
        }
        int found = 0;
        for (int p = 0; puncts[p].s; p++) {
            int n = (int)strlen(puncts[p].s);
            if (i + n <= len && !memcmp(b + i, puncts[p].s, n)) { t.k = puncts[p].k; i += n; found = 1; break; }
        }
        if (!found) {
            fc_error_nj(fileid, line, "Illegal character '%c' (0x%X)", c >= 32 && c < 127 ? c : '?', c);
            i++; bad = 1; continue;
        }
        tv_push(out, t);
    }
    return bad;
}

/* ---------- macros ---------- */
typedef struct Macro {
    char *name; int func, nparams, variadic, busy;
    char **params; TokVec body; struct Macro *next; int builtin;
} Macro;
#define MBUCK 1024
static Macro *mtab[MBUCK];
static char *s_FILE, *s_LINE, *s_DATE, *s_TIME, *s_defined, *s_VA_ARGS;
static char str_date[16], str_time[16];

static unsigned mh(const char *n) { return (unsigned)(((uintptr_t)n >> 4) * 2654435761u) % MBUCK; }
static Macro *mfind(const char *n)
{
    for (Macro *m = mtab[mh(n)]; m; m = m->next) if (m->name == n) return m;
    return NULL;
}
static void mundef(const char *n)
{
    for (Macro **p = &mtab[mh(n)]; *p; p = &(*p)->next) if ((*p)->name == n) { *p = (*p)->next; return; }
}

static int depth_inc;
static void process(TokVec *src, TokVec *out, const char *dir, int depth);

void pp_init(void)
{
    memset(mtab, 0, sizeof mtab);
    pp_npragmas = 0;
    s_FILE = intern("__FILE__", 8); s_LINE = intern("__LINE__", 8);
    s_defined = intern("defined", 7); s_VA_ARGS = intern("__VA_ARGS__", 11);
    Macro *m;
    m = xalloc(sizeof *m); m->name = s_FILE; m->builtin = 1; m->next = mtab[mh(m->name)]; mtab[mh(m->name)] = m;
    m = xalloc(sizeof *m); m->name = s_LINE; m->builtin = 1; m->next = mtab[mh(m->name)]; mtab[mh(m->name)] = m;
    s_DATE = intern("__DATE__", 8); s_TIME = intern("__TIME__", 8);
    m = xalloc(sizeof *m); m->name = s_DATE; m->builtin = 1; m->next = mtab[mh(m->name)]; mtab[mh(m->name)] = m;
    m = xalloc(sizeof *m); m->name = s_TIME; m->builtin = 1; m->next = mtab[mh(m->name)]; mtab[mh(m->name)] = m;
    {
        static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        time_t now = time(NULL); struct tm *lt = localtime(&now);
        snprintf(str_date, sizeof str_date, "%s %2d %d", mon[lt->tm_mon], lt->tm_mday, lt->tm_year + 1900);
        snprintf(str_time, sizeof str_time, "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
    }
    pp_define_cmdline("__FLASHC__", "0x0100");
    pp_define_cmdline("__STDC__", "1");
    pp_define_cmdline("__linux__", "1");
    pp_define_cmdline("__x86_64__", "1");
}

static Macro *msnap[MBUCK];
void pp_snapshot(void) { memcpy(msnap, mtab, sizeof mtab); }
void pp_restore(void) { memcpy(mtab, msnap, sizeof mtab); }

void pp_undef_cmdline(const char *name) { mundef(intern(name, (int)strlen(name))); }

void pp_define_cmdline(const char *name, const char *val)
{
    Macro *m = xalloc(sizeof *m);
    m->name = intern(name, (int)strlen(name));
    int fid = file_register("<command line>");
    lex_buffer(val ? val : "1", (int)strlen(val ? val : "1"), fid, &m->body);
    mundef(m->name);
    m->next = mtab[mh(m->name)]; mtab[mh(m->name)] = m;
}

static void tv_copy(TokVec *dst, TokVec *src) { for (int i = 0; i < src->n; i++) tv_push(dst, src->v[i]); }

static void stringify(TokVec *arg, Tok *like, TokVec *out)
{
    char *buf = malloc(4096); int n = 0;
    for (int i = 0; i < arg->n; i++) {
        Tok *t = &arg->v[i];
        if (i && t->spc && n < 4000) buf[n++] = ' ';
        const char *sp = t->k == TK_STR ? tok_spell(t) : tok_spell(t);
        int l = (int)strlen(sp);
        if (n + l < 4090) { memcpy(buf + n, sp, l); n += l; }
    }
    Tok r = *like; r.k = TK_STR; r.s = xstrndup(buf, n); r.slen = n; r.bol = 0;
    free(buf);
    tv_push(out, r);
}

static void expand_seq(Tok *v, int n, int *pos, TokVec *out, int depth);

static void expand_all(TokVec *in, TokVec *out, int depth)
{
    int pos = 0;
    while (pos < in->n) expand_seq(in->v, in->n, &pos, out, depth);
}

static int paste(TokVec *out, Tok *rhs)
{
    if (!out->n) { tv_push(out, *rhs); return 0; }
    Tok *l = &out->v[out->n - 1];
    char buf[512];
    snprintf(buf, sizeof buf, "%s%s", tok_spell(l), tok_spell(rhs));
    TokVec tmp = {0};
    int saveerr = CX->errors;
    lex_buffer(buf, (int)strlen(buf), l->fileid, &tmp);
    CX->errors = saveerr;
    if (tmp.n >= 1) {
        Tok nt = tmp.v[0]; nt.line = l->line; nt.fileid = l->fileid; nt.spc = l->spc; nt.bol = 0;
        out->v[out->n - 1] = nt;
    }
    free(tmp.v);
    return 0;
}

static void subst(Macro *m, TokVec *args, Tok *call, TokVec *out, int depth)
{
    TokVec *exp = xalloc(sizeof(TokVec) * (m->nparams + 1));
    int *done = xalloc(sizeof(int) * (m->nparams + 1));
    for (int i = 0; i < m->body.n; i++) {
        Tok *t = &m->body.v[i];
        int pi = -1;
        if (t->k == TK_ID) for (int p = 0; p < m->nparams; p++) if (m->params[p] == t->s) { pi = p; break; }
        if (t->k == P_HASH && m->func && i + 1 < m->body.n) {
            Tok *nx = &m->body.v[i + 1]; int q = -1;
            if (nx->k == TK_ID) for (int p = 0; p < m->nparams; p++) if (m->params[p] == nx->s) q = p;
            if (q >= 0) { stringify(&args[q], t, out); i++; continue; }
        }
        if (t->k == P_HASHHASH && i + 1 < m->body.n) {
            Tok *nx = &m->body.v[i + 1]; int q = -1;
            if (nx->k == TK_ID) for (int p = 0; p < m->nparams; p++) if (m->params[p] == nx->s) q = p;
            if (q >= 0) {
                if (args[q].n) { paste(out, &args[q].v[0]); for (int k = 1; k < args[q].n; k++) tv_push(out, args[q].v[k]); }
            } else paste(out, nx);
            i++; continue;
        }
        if (pi >= 0) {
            int next_paste = i + 1 < m->body.n && m->body.v[i + 1].k == P_HASHHASH;
            int prev_paste = i > 0 && m->body.v[i - 1].k == P_HASHHASH;
            if (next_paste || prev_paste) { tv_copy(out, &args[pi]); }
            else {
                if (!done[pi]) { expand_all(&args[pi], &exp[pi], depth + 1); done[pi] = 1; }
                for (int k = 0; k < exp[pi].n; k++) { Tok x = exp[pi].v[k]; if (!k) x.spc = t->spc; tv_push(out, x); }
            }
            continue;
        }
        Tok x = *t; x.line = call->line; x.fileid = call->fileid; x.bol = 0;
        tv_push(out, x);
    }
}

static void expand_seq(Tok *v, int n, int *pos, TokVec *out, int depth)
{
    Tok *t = &v[*pos];
    Macro *m;
    if (t->k != TK_ID || !(m = mfind(t->s)) || m->busy || depth > 200) { tv_push(out, *t); (*pos)++; return; }
    if (m->builtin) {
        Tok r = *t; r.bol = 0;
        if (m->name == s_LINE) { char b[16]; snprintf(b, 16, "%d", t->line); r.k = TK_NUM; r.val = t->line; r.s = intern(b, (int)strlen(b)); }
        else if (m->name == s_DATE) { r.k = TK_STR; r.s = xstrdup(str_date); r.slen = (int)strlen(r.s); }
        else if (m->name == s_TIME) { r.k = TK_STR; r.s = xstrdup(str_time); r.slen = (int)strlen(r.s); }
        else { r.k = TK_STR; r.s = xstrdup(file_name(t->fileid)); r.slen = (int)strlen(r.s); }
        tv_push(out, r); (*pos)++; return;
    }
    TokVec *args = NULL;
    int p = *pos + 1;
    if (m->func) {
        if (p >= n || v[p].k != P_LPAR) { tv_push(out, *t); (*pos)++; return; }
        p++;
        args = xalloc(sizeof(TokVec) * (m->nparams + 2));
        int ai = 0, dep = 0, closed = 0;
        if (m->nparams == 0 && p < n && v[p].k == P_RPAR) { p++; closed = 1; }
        while (!closed && p < n) {
            Tok *a = &v[p];
            if (a->k == P_LPAR) dep++;
            else if (a->k == P_RPAR) { if (!dep) { closed = 1; p++; break; } dep--; }
            else if (a->k == P_COMMA && !dep && !(m->variadic && ai >= m->nparams - 1)) { ai++; p++; continue; }
            if (ai <= m->nparams) tv_push(&args[ai], *a);
            p++;
        }
        if (!closed) { fc_error_nj(t->fileid, t->line, "Unterminated macro argument list"); (*pos) = n; return; }
        int got = ai + 1;
        if (m->nparams == 0) got = 0;
        if (got < m->nparams && !(m->variadic && got == m->nparams - 1))
            fc_error_nj(t->fileid, t->line, "Too few arguments in macro call '%s'", m->name);
        else if (got > m->nparams)
            fc_error_nj(t->fileid, t->line, "Too many arguments in macro call '%s'", m->name);
    }
    TokVec body = {0};
    subst(m, args, t, &body, depth);
    if (body.n) body.v[0].spc = t->spc;
    m->busy = 1;
    {
        int bp = 0;
        while (bp < body.n) expand_seq(body.v, body.n, &bp, out, depth + 1);
    }
    m->busy = 0;
    free(body.v);
    *pos = p;
}

/* ---------- #if evaluation ---------- */
typedef struct { Tok *v; int n, p; int fileid, line; } EV;
static long long ev_cond(EV *e);
static long long ev_prim(EV *e)
{
    if (e->p >= e->n) { fc_error_nj(e->fileid, e->line, "Expression syntax"); return 0; }
    Tok *t = &e->v[e->p++];
    if (t->k == TK_NUM && t->isfloat) { fc_error_nj(e->fileid, e->line, "Illegal use of floating point"); return 0; }
    if (t->k == TK_NUM || t->k == TK_CHR) return t->val;
    if (t->k == TK_ID) return 0;
    if (t->k == P_LPAR) {
        long long r = ev_cond(e);
        if (e->p < e->n && e->v[e->p].k == P_RPAR) e->p++; else fc_error_nj(e->fileid, e->line, "Expression syntax");
        return r;
    }
    if (t->k == P_MINUS) return -ev_prim(e);
    if (t->k == P_PLUS) return ev_prim(e);
    if (t->k == P_BANG) return !ev_prim(e);
    if (t->k == P_TILDE) return ~ev_prim(e);
    fc_error_nj(e->fileid, e->line, "Expression syntax");
    return 0;
}
static int prec(int k)
{
    switch (k) {
    case P_STAR: case P_SLASH: case P_PERCENT: return 10;
    case P_PLUS: case P_MINUS: return 9;
    case P_SHL: case P_SHR: return 8;
    case P_LT: case P_GT: case P_LE: case P_GE: return 7;
    case P_EQ: case P_NE: return 6;
    case P_AMP: return 5; case P_CARET: return 4; case P_BAR: return 3;
    case P_ANDAND: return 2; case P_OROR: return 1;
    }
    return 0;
}
static long long ev_bin(EV *e, int minp)
{
    long long l = ev_prim(e);
    while (e->p < e->n) {
        int k = e->v[e->p].k, pr = prec(k);
        if (!pr || pr < minp) break;
        e->p++;
        long long r = ev_bin(e, pr + 1);
        switch (k) {
        case P_STAR: l *= r; break;
        case P_SLASH: if (!r) { fc_error_nj(e->fileid, e->line, "Division by zero"); r = 1; } l /= r; break;
        case P_PERCENT: if (!r) { fc_error_nj(e->fileid, e->line, "Division by zero"); r = 1; } l %= r; break;
        case P_PLUS: l += r; break; case P_MINUS: l -= r; break;
        case P_SHL: l <<= r; break; case P_SHR: l >>= r; break;
        case P_LT: l = l < r; break; case P_GT: l = l > r; break; case P_LE: l = l <= r; break; case P_GE: l = l >= r; break;
        case P_EQ: l = l == r; break; case P_NE: l = l != r; break;
        case P_AMP: l &= r; break; case P_CARET: l ^= r; break; case P_BAR: l |= r; break;
        case P_ANDAND: l = l && r; break; case P_OROR: l = l || r; break;
        }
    }
    return l;
}
static long long ev_cond(EV *e)
{
    long long c = ev_bin(e, 1);
    if (e->p < e->n && e->v[e->p].k == P_QUEST) {
        e->p++;
        long long a = ev_cond(e);
        if (e->p < e->n && e->v[e->p].k == P_COLON) e->p++; else fc_error_nj(e->fileid, e->line, "Expression syntax");
        long long b = ev_cond(e);
        return c ? a : b;
    }
    return c;
}
static long long eval_if(Tok *v, int n, int fileid, int line)
{
    TokVec pre = {0}, ex = {0};
    for (int i = 0; i < n; i++) {
        if (v[i].k == TK_ID && v[i].s == s_defined) {
            int j = i + 1, paren = 0;
            if (j < n && v[j].k == P_LPAR) { paren = 1; j++; }
            Tok r = v[i]; r.k = TK_NUM; r.val = (j < n && v[j].k == TK_ID && mfind(v[j].s)) ? 1 : 0; r.s = NULL;
            tv_push(&pre, r);
            j++; if (paren && j < n && v[j].k == P_RPAR) j++;
            i = j - 1; continue;
        }
        tv_push(&pre, v[i]);
    }
    expand_all(&pre, &ex, 0);
    EV e = { ex.v, ex.n, 0, fileid, line };
    long long r = ev_cond(&e);
    if (e.p < e.n) fc_error_nj(fileid, line, "Expression syntax");
    free(pre.v); free(ex.v);
    return r;
}

/* ---------- file reading & directives ---------- */
static char *read_whole(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f); b[n] = 0; *len = (int)n;
    return b;
}
static void dirname_of(const char *path, char *out, int cap)
{
    const char *sl = strrchr(path, '/');
    if (!sl) { snprintf(out, cap, "."); return; }
    int n = (int)(sl - path); if (n >= cap) n = cap - 1;
    memcpy(out, path, n); out[n] = 0;
}

typedef struct Cond { int parent_skip, taken, seen_else; } Cond;

static void do_include(Tok *dir, Tok *v, int n, TokVec *out, const char *cdir, int depth)
{
    char name[512]; int angle = 0;
    TokVec expanded = {0};
    if (n >= 1 && v[0].k == TK_ID) {                  /* #include MACRO: expand it first */
        TokVec raw = {0};
        for (int k = 0; k < n; k++) tv_push(&raw, v[k]);
        expand_all(&raw, &expanded, 0);
        free(raw.v);
        v = expanded.v; n = expanded.n;
    }
    if (n >= 1 && v[0].k == TK_STR) snprintf(name, sizeof name, "%s", v[0].s);
    else if (n >= 1 && v[0].k == P_LT) {
        angle = 1; name[0] = 0; int j = 1;
        for (; j < n && v[j].k != P_GT; j++) {
            if (j > 1 && v[j].spc) strcat(name, " ");
            strncat(name, tok_spell(&v[j]), sizeof name - strlen(name) - 1);
        }
        if (j >= n) { fc_error_nj(dir->fileid, dir->line, "Illegal character in include file name"); return; }
    } else { fc_error_nj(dir->fileid, dir->line, "Include filename expected"); return; }
    if (depth > 40) { fc_error_nj(dir->fileid, dir->line, "Include files nested too deeply"); return; }
    char path[1100]; char *text = NULL; int len = 0; const char *shown = name;
    if (!angle) {
        snprintf(path, sizeof path, "%s/%s", cdir, name);
        text = read_whole(path, &len);
        if (!text) { snprintf(path, sizeof path, "%s", name); text = read_whole(path, &len); }
    }
    for (int i = 0; !text && i < CX->opt.nincdir; i++) {
        snprintf(path, sizeof path, "%s/%s", CX->opt.incdirs[i], name);
        text = read_whole(path, &len);
    }
    int builtin = 0;
    if (!text) {
        const char *b = fc_builtin_header(name);
        if (b) { text = strdup(b); len = (int)strlen(b); builtin = 1; snprintf(path, sizeof path, "%s", name); }
    }
    if (!text) { fc_error_nj(dir->fileid, dir->line, "Unable to open include file '%s'", shown); return; }
    int fid = file_register(path);
    if (builtin) file_set_builtin(fid);
    TokVec sub = {0};
    lex_buffer(text, len, fid, &sub);
    free(text);
    char d2[1024]; dirname_of(path, d2, sizeof d2);
    process(&sub, out, d2, depth + 1);
    CX->lines += 0;
    free(sub.v); free(expanded.v);
}

PragmaFn pp_pragmas[64];
int pp_npragmas;

/* #pragma startup fn [priority]   /   #pragma exit fn [priority]  (priority 0..255, default 100) */
static void do_pragma(Tok *v, int n, int fileid, int line)
{
    if (n < 2 || v[0].k != TK_ID) return;
    int is_exit = !strcmp(v[0].s, "exit");
    if (!is_exit && strcmp(v[0].s, "startup")) return;       /* argsused, warn, option ... are accepted and ignored */
    if (v[1].k != TK_ID) { fc_error_nj(fileid, line, "Function name expected in #pragma %s", v[0].s); return; }
    if (pp_npragmas >= 64) { fc_error_nj(fileid, line, "Too many #pragma startup/exit functions"); return; }
    int prio = 100;
    if (n >= 3 && v[2].k == TK_NUM) prio = (int)v[2].val;
    if (prio < 0 || prio > 255) { fc_error_nj(fileid, line, "Priority out of range in #pragma %s", v[0].s); return; }
    PragmaFn *p = &pp_pragmas[pp_npragmas++];
    p->name = v[1].s; p->prio = prio; p->is_exit = is_exit; p->fileid = fileid; p->line = line;
}

static void do_define(Tok *v, int n)
{
    if (n < 1 || v[0].k != TK_ID) { fc_error_nj(v[0].fileid, v[0].line, "Macro name expected"); return; }
    Macro *m = xalloc(sizeof *m);
    m->name = v[0].s;
    int i = 1;
    if (i < n && v[i].k == P_LPAR && !v[i].spc) {
        m->func = 1; i++;
        char *ps[64];
        while (i < n && v[i].k != P_RPAR) {
            if (v[i].k == TK_ID && m->nparams < 64) ps[m->nparams++] = v[i].s;
            else if (v[i].k == P_ELLIPSIS) { m->variadic = 1; ps[m->nparams++] = s_VA_ARGS; }
            i++;
            if (i < n && v[i].k == P_COMMA) i++;
        }
        i++;
        m->params = xalloc(sizeof(char *) * (m->nparams + 1));
        memcpy(m->params, ps, sizeof(char *) * m->nparams);
    }
    for (; i < n; i++) { Tok x = v[i]; x.bol = 0; tv_push(&m->body, x); }
    if (m->body.n) m->body.v[0].spc = 0;
    mundef(m->name);
    m->next = mtab[mh(m->name)]; mtab[mh(m->name)] = m;
}

static void process(TokVec *src, TokVec *out, const char *cdir, int depth)
{
    Cond stack[64]; int sp = 0;
    int skipping = 0;
    int i = 0;
    int last_line = 0;
    while (i < src->n) {
        Tok *t = &src->v[i];
        if (t->line > last_line) last_line = t->line;
        if (t->k == P_HASH && t->bol) {
            int j = i + 1;
            while (j < src->n && !src->v[j].bol) j++;
            Tok *d = &src->v[i + 1];
            int dn = j - i - 1;
            char *name = (dn > 0 && (d->k == TK_ID || (d->k >= K_AUTO && d->k <= K_WHILE))) ? (char *)tok_spell(d) : "";
            Tok *args = d + 1; int an = dn > 0 ? dn - 1 : 0;
            int line = t->line, fid = t->fileid;
            if (!strcmp(name, "if") || !strcmp(name, "ifdef") || !strcmp(name, "ifndef")) {
                int cond = 0;
                if (sp >= 63) { fc_error_nj(fid, line, "Too many nested #if"); sp = 62; }
                if (!skipping) {
                    if (name[2] == 0) cond = eval_if(args, an, fid, line) != 0;
                    else {
                        if (an < 1 || args[0].k != TK_ID) fc_error_nj(fid, line, "Macro name expected");
                        else cond = mfind(args[0].s) != NULL;
                        if (!strcmp(name, "ifndef")) cond = !cond;
                    }
                }
                stack[sp].parent_skip = skipping; stack[sp].taken = cond; stack[sp].seen_else = 0;
                sp++;
                skipping = skipping || !cond;
            } else if (!strcmp(name, "elif")) {
                if (!sp) fc_error_nj(fid, line, "Misplaced elif directive");
                else {
                    Cond *c = &stack[sp - 1];
                    if (c->parent_skip || c->taken) skipping = 1;
                    else { int cond = eval_if(args, an, fid, line) != 0; c->taken = cond; skipping = !cond; }
                }
            } else if (!strcmp(name, "else")) {
                if (!sp) fc_error_nj(fid, line, "Misplaced else directive");
                else {
                    Cond *c = &stack[sp - 1];
                    c->seen_else = 1;
                    skipping = c->parent_skip || c->taken; c->taken = 1;
                }
            } else if (!strcmp(name, "endif")) {
                if (!sp) fc_error_nj(fid, line, "Misplaced endif directive");
                else { sp--; skipping = stack[sp].parent_skip; }
            } else if (!skipping) {
                if (!strcmp(name, "include")) do_include(t, args, an, out, cdir, depth);
                else if (!strcmp(name, "define")) { if (an) do_define(args, an); else fc_error_nj(fid, line, "Macro name expected"); }
                else if (!strcmp(name, "undef")) { if (an && args[0].k == TK_ID) mundef(args[0].s); }
                else if (!strcmp(name, "error")) {
                    char msg[300] = "";
                    for (int k = 0; k < an; k++) { if (k) strcat(msg, " "); strncat(msg, tok_spell(&args[k]), sizeof msg - strlen(msg) - 1); }
                    fc_error_nj(fid, line, "Error directive: %s", msg);
                }
                else if (!strcmp(name, "pragma")) do_pragma(args, an, fid, line);
                else if (!strcmp(name, "line") || !strcmp(name, "") ) { }
                else fc_error_nj(fid, line, "Invalid preprocessing directive '%s'", name);
            }
            i = j;
            continue;
        }
        if (skipping) { i++; continue; }
        expand_seq(src->v, src->n, &i, out, 0);
    }
    if (sp) fc_error_nj(src->n ? src->v[src->n - 1].fileid : 0, last_line, "Unexpected end of file in conditional statement");
    if (depth == 0 || 1) CX->lines += last_line;
}

int pp_run_text(const char *name, const char *text, TokVec *out)
{
    int fid = file_register(name);
    TokVec src = {0};
    int before = CX->errors;
    lex_buffer(text, (int)strlen(text), fid, &src);
    char dir[1024]; dirname_of(name, dir, sizeof dir);
    int from = out->n;
    process(&src, out, dir, 0);
    free(src.v);
    kw_convert(out, from);
    return CX->errors != before;
}

int pp_run(const char *path, TokVec *out)
{
    int len;
    char *text = read_whole(path, &len);
    if (!text) { fc_error_nj(0, 0, "Unable to open '%s'", path); return 1; }
    int fid = file_register(path);
    TokVec src = {0};
    int before = CX->errors;
    if (CX->progress) CX->progress(CX->user, path, CX->lines);
    lex_buffer(text, len, fid, &src);
    free(text);
    char dir[1024]; dirname_of(path, dir, sizeof dir);
    int from = out->n;
    process(&src, out, dir, 0);
    free(src.v);
    kw_convert(out, from);
    return CX->errors != before;
}
