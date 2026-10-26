/* Flash C - parser, type checker, AST builder */
#include "fci.h"
#include <ctype.h>

extern jmp_buf *recover_jb;
Program PG;

/* ---------- types ---------- */
Type *ty_void, *ty_char, *ty_uchar, *ty_short, *ty_ushort, *ty_int, *ty_uint, *ty_long, *ty_ulong, *ty_float, *ty_double;
static Type *mkty(int kind, int size, int uns)
{
    Type *t = xalloc(sizeof *t);
    t->kind = kind; t->size = size; t->align = size ? size : 1; t->uns = uns; t->complete = 1;
    return t;
}
Type *ptr_to(Type *b) { Type *t = mkty(TY_PTR, 8, 1); t->base = b; return t; }
Type *array_of(Type *b, int len)
{
    Type *t = mkty(TY_ARRAY, len < 0 ? 0 : b->size * len, 0);
    t->base = b; t->len = len; t->align = b->align; return t;
}
int is_int(Type *t) { return t->kind >= TY_CHAR && t->kind <= TY_LONG; }
int is_ptr(Type *t) { return t->kind == TY_PTR; }
int is_flt(Type *t) { return t->kind == TY_FLOAT || t->kind == TY_DOUBLE; }
int is_scalar(Type *t) { return is_int(t) || is_ptr(t) || is_flt(t); }
int is_agg(Type *t) { return t->kind == TY_STRUCT || t->kind == TY_UNION; }

int same_type(Type *a, Type *b)
{
    if (a == b) return 1;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case TY_VOID: return 1;
    case TY_CHAR: case TY_SHORT: case TY_INT: case TY_LONG: return a->uns == b->uns;
    case TY_FLOAT: case TY_DOUBLE: return 1;
    case TY_PTR: return same_type(a->base, b->base);
    case TY_STRUCT: case TY_UNION: return a->tag && a->tag == b->tag && a->size == b->size;
    case TY_ARRAY: return same_type(a->base, b->base) && (a->len == b->len || a->len < 0 || b->len < 0);
    case TY_FUNC: {
        if (!same_type(a->ret, b->ret)) return 0;
        if (a->noproto || b->noproto) return 1;
        if (a->nparams != b->nparams || a->variadic != b->variadic) return 0;
        Param *p = a->params, *q = b->params;
        for (; p && q; p = p->next, q = q->next) if (!same_type(p->ty, q->ty)) return 0;
        return 1;
    }
    default: return 0;
    }
}

const char *type_str(Type *t)
{
    static char buf[8][200]; static int bi; char *b = buf[bi++ & 7];
    switch (t->kind) {
    case TY_VOID: return "void";
    case TY_CHAR: return t->uns ? "unsigned char" : "char";
    case TY_SHORT: return t->uns ? "unsigned short" : "short";
    case TY_INT: return t->uns ? "unsigned int" : "int";
    case TY_LONG: return t->uns ? "unsigned long" : "long";
    case TY_FLOAT: return "float";
    case TY_DOUBLE: return "double";
    case TY_STRUCT: snprintf(b, 200, "struct %s", t->tag ? t->tag : ""); return b;
    case TY_UNION: snprintf(b, 200, "union %s", t->tag ? t->tag : ""); return b;
    case TY_PTR: snprintf(b, 200, "%s *", type_str(t->base)); return b;
    case TY_ARRAY: snprintf(b, 200, "%s[%d]", type_str(t->base), t->len < 0 ? 0 : t->len); return b;
    case TY_FUNC: snprintf(b, 200, "%s (*)()", type_str(t->ret)); return b;
    }
    return "?";
}

/* ---------- scopes ---------- */
typedef struct Tag { char *name; Type *ty; struct Tag *next; } Tag;
typedef struct Scope { Sym *syms; Tag *tags; struct Scope *parent; int is_func_top; } Scope;
static Scope *scope, *gscope;
#define GH 512
static Sym *ghash[GH];
static unsigned gh(const char *n) { return (unsigned)(((uintptr_t)n >> 4) * 2654435761u) % GH; }

static Tok *toks; static int tp;
static int in_rt;               /* parsing the runtime prelude */
static Sym *curfn;
static int fn_frame, fn_ret_seen, brk_depth, cont_depth;
static Node *cur_switch;
static int cur_line_for_err;
typedef struct LabelRef { char *name; int line, defined; struct LabelRef *next; } LabelRef;
static LabelRef *fn_labels;

static Tok *peek(void) { return &toks[tp]; }
static int cur_line(void) { return toks[tp].line; }
static int cur_file(void) { return toks[tp].fileid; }
static Tok *next(void) { Tok *t = &toks[tp]; if (t->k != TK_EOF) tp++; return t; }
static int accept(int k) { if (toks[tp].k == k) { tp++; return 1; } return 0; }

#define ERR(...) fc_error(cur_file(), cur_line(), __VA_ARGS__)
#define ERRAT(t, ...) fc_error((t)->fileid, (t)->line, __VA_ARGS__)
#define WARN(w, ...) do { if (!in_rt && (w)) fc_warn(cur_file(), cur_line(), __VA_ARGS__); } while (0)
#define WARNAT(w, ln, ...) do { if (!in_rt && (w)) fc_warn(cur_file(), (ln), __VA_ARGS__); } while (0)

static int noskip;   /* set when an error leaves the parser at a sensible restart point */

static void expect(int k, const char *msg)
{
    if (toks[tp].k == k) { tp++; return; }
    if (k == P_SEMI) noskip = 1;
    /* report on the line of the previous token for missing terminators, like a classic compiler */
    if (tp > 0 && (k == P_SEMI || k == P_RPAR || k == P_RBRK))
        fc_error(toks[tp - 1].fileid, toks[tp - 1].line, "%s", msg);
    ERR("%s", msg);
}

static void push_scope(void) { Scope *s = xalloc(sizeof *s); s->parent = scope; scope = s; }

static void check_unused(Scope *s, int line)
{
    if (in_rt || !CX->opt.w_unused) return;
    for (Sym *y = s->syms; y; y = y->next) {
        if (y->kind != SY_VAR || y->global || y->used) continue;
        if (y->is_param) fc_warn(cur_file(), line, "Parameter '%s' is never used", y->name);
        else fc_warn(cur_file(), line, "'%s' is declared but never used", y->name);
    }
}
static void pop_scope(int line)
{
    check_unused(scope, line);
    for (Sym *y = scope->syms; y; y = y->next) if (y->kind == SY_VAR && !y->global) y->endline = line;
    scope = scope->parent;
}
static void dbg_note_var(Sym *y)
{
    if (!curfn) return;
    if (curfn->ndbg == curfn->capdbg) { curfn->capdbg = curfn->capdbg ? curfn->capdbg * 2 : 8; curfn->dbgvars = realloc(curfn->dbgvars, curfn->capdbg * sizeof(Sym *)); }
    curfn->dbgvars[curfn->ndbg++] = y;
}

static Sym *find_in(Scope *s, const char *name)
{
    if (s == gscope) {
        for (Sym *y = ghash[gh(name)]; y; y = y->hnext) if (y->name == name) return y;
        return NULL;
    }
    for (Sym *y = s->syms; y; y = y->next) if (y->name == name) return y;
    return NULL;
}
static Sym *lookup(const char *name)
{
    for (Scope *s = scope; s; s = s->parent) {
        Sym *y = find_in(s, name);
        if (y && !(y->hidden && !in_rt)) return y;
    }
    return NULL;
}
Sym *global_lookup(const char *name) { return find_in(gscope, intern(name, (int)strlen(name))); }
Sym *global_syms(void) { return gscope ? gscope->syms : NULL; }

static Sym *add_sym(Scope *s, char *name, Type *ty, int kind)
{
    Sym *y = xalloc(sizeof *y);
    y->name = name; y->ty = ty; y->kind = kind; y->codepos = -1;
    y->line = cur_line(); y->fileid = cur_file(); y->from_rt = in_rt;
    if (s == gscope) { y->hnext = ghash[gh(name)]; ghash[gh(name)] = y; if (in_rt) y->hidden = 1; }
    y->next = s->syms; s->syms = y;
    return y;
}

static Tag *find_tag(const char *name, int cur_only)
{
    for (Scope *s = scope; s; s = s->parent) {
        for (Tag *t = s->tags; t; t = t->next) if (t->name == name) return t;
        if (cur_only) break;
    }
    return NULL;
}

/* ---------- data section ---------- */
static int data_alloc(int size, int align)
{
    if (align < 1) align = 1;
    int off = (PG.ndata + align - 1) & ~(align - 1);
    int need = off + size;
    if (need > PG.capdata) {
        PG.capdata = need * 2 + 1024;
        PG.data = realloc(PG.data, PG.capdata);
    }
    memset(PG.data + PG.ndata, 0, need - PG.ndata);
    PG.ndata = need;
    return off;
}
static int bss_alloc(int size, int align)
{
    if (align < 1) align = 1;
    int off = (PG.bss + align - 1) & ~(align - 1);
    PG.bss = off + (size ? size : 1);
    return off;
}
static void add_reloc(int off, int kind, Sym *fn, long long add)
{
    if (PG.ndrel == PG.capdrel) { PG.capdrel = PG.capdrel ? PG.capdrel * 2 : 64; PG.drel = realloc(PG.drel, PG.capdrel * sizeof *PG.drel); }
    PG.drel[PG.ndrel].off = off; PG.drel[PG.ndrel].kind = kind; PG.drel[PG.ndrel].fn = fn; PG.drel[PG.ndrel].add = add;
    PG.ndrel++;
}

/* ---------- node helpers ---------- */
static void note_fp(void)
{
    /* the program (not the library or a built-in header) uses floating point: the float run-time gets linked */
    if (!in_rt && !file_is_builtin(toks[tp > 0 ? tp - 1 : 0].fileid)) PG.uses_fp = 1;
}

static Node *mk(int k, Type *ty)
{
    Node *n = xalloc(sizeof *n);
    n->k = k; n->ty = ty; n->line = toks[tp > 0 ? tp - 1 : 0].line;
    if (ty && (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE)) note_fp();
    return n;
}
static long long norm(long long v, Type *t)
{
    switch (t->size) {
    case 1: return t->uns ? (long long)(uint8_t)v : (long long)(int8_t)v;
    case 2: return t->uns ? (long long)(uint16_t)v : (long long)(int16_t)v;
    case 4: return t->uns ? (long long)(uint32_t)v : (long long)(int32_t)v;
    }
    return v;
}
static Node *mknum(long long v, Type *t) { Node *n = mk(N_NUM, t); n->val = norm(v, t); return n; }
static Node *mkfnum(double v, Type *t)
{
    Node *n = mk(N_FNUM, t);
    n->fval = t->kind == TY_FLOAT ? (double)(float)v : v;
    return n;
}
static int is_lvalue(Node *n)
{
    return (n->k == N_VAR && n->sym->kind == SY_VAR) || n->k == N_DEREF || n->k == N_MEMBER;
}
static int is_null_const(Node *n) { return n->k == N_NUM && n->val == 0; }

static Node *rv(Node *n)
{
    if (n->ty->kind == TY_ARRAY) {
        Node *a = mk(N_ADDR, ptr_to(n->ty->base)); a->l = n; a->line = n->line; return a;
    }
    if (n->ty->kind == TY_FUNC) {
        Node *a = mk(N_ADDR, ptr_to(n->ty)); a->l = n; return a;
    }
    return n;
}

static Node *conv(Node *n, Type *to)
{
    Type *from = n->ty;
    if (to->kind == TY_VOID) { Node *c = mk(N_CAST, to); c->l = n; return c; }
    if (!is_scalar(to) || !is_scalar(from)) return n;
    if (is_flt(to) || is_flt(from)) {
        if (is_ptr(to) || is_ptr(from)) ERR("Cannot convert '%s' to '%s'", type_str(from), type_str(to));
        if (n->k == N_FNUM) {
            if (is_flt(to)) return mkfnum(n->fval, to);
            double d = n->fval; return mknum(d >= 9.2e18 ? (long long)(unsigned long long)d : (long long)d, to);
        }
        if (n->k == N_NUM && is_flt(to)) return mkfnum(from->uns && from->size == 8 ? (double)(unsigned long long)n->val : (double)n->val, to);
        if (from->kind == to->kind) return n;
        Node *c = mk(N_CAST, to); c->l = n; c->line = n->line; return c;
    }
    if (n->k == N_NUM) {
        if (is_ptr(to)) { Node *r = mknum(n->val, ty_ulong); r->ty = to; return r; }
        Node *r = mknum(n->val, to); return r;
    }
    int fs = from->size, ts = to->size;
    if (fs == ts && (from->uns == to->uns || ts == 8)) { if (from == to) return n; Node *c = mk(N_CAST, to); c->l = n; c->line = n->line; return c; }
    Node *c = mk(N_CAST, to); c->l = n; c->line = n->line; return c;
}

static Node *promote(Node *n)
{
    n = rv(n);
    if (is_int(n->ty) && n->ty->size < 4) return conv(n, ty_int);
    return n;
}
static Type *arith_type(Type *a, Type *b)
{
    if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE) return ty_double;
    if (a->kind == TY_FLOAT || b->kind == TY_FLOAT) return ty_float;
    if (a->size == 8 || b->size == 8) {
        int u = (a->size == 8 && a->uns) || (b->size == 8 && b->uns);
        return u ? ty_ulong : ty_long;
    }
    if ((a->size == 4 && a->uns) || (b->size == 4 && b->uns)) return ty_uint;
    return ty_int;
}


static Node *fold_bin(int op, Node *l, Node *r, Type *ct, Type *rt)
{
    long long a = l->val, b = r->val, v = 0;
    int u = ct->uns;
    switch (op) {
    case B_ADD: v = a + b; break; case B_SUB: v = a - b; break; case B_MUL: v = a * b; break;
    case B_DIV: if (!b) return NULL; v = u ? (long long)((uint64_t)a / (uint64_t)b) : a / b; break;
    case B_MOD: if (!b) return NULL; v = u ? (long long)((uint64_t)a % (uint64_t)b) : a % b; break;
    case B_AND: v = a & b; break; case B_OR: v = a | b; break; case B_XOR: v = a ^ b; break;
    case B_SHL: v = a << (b & 63); break;
    case B_SHR: v = u ? (long long)((uint64_t)a >> (b & 63)) : a >> (b & 63); break;
    case B_EQ: v = a == b; break; case B_NE: v = a != b; break;
    case B_LT: v = u ? (uint64_t)a < (uint64_t)b : a < b; break;
    case B_LE: v = u ? (uint64_t)a <= (uint64_t)b : a <= b; break;
    case B_GT: v = u ? (uint64_t)a > (uint64_t)b : a > b; break;
    case B_GE: v = u ? (uint64_t)a >= (uint64_t)b : a >= b; break;
    }
    return mknum(v, rt);
}

static Node *mkbin(int op, Node *l, Node *r, Type *resty, int uns)
{
    if (l->k == N_FNUM && r->k == N_FNUM) {
        double a = l->fval, b = r->fval; int cmp = 1; double v = 0;
        switch (op) {
        case B_ADD: v = a + b; cmp = 0; break; case B_SUB: v = a - b; cmp = 0; break; case B_MUL: v = a * b; cmp = 0; break;
        case B_DIV: if (b == 0) goto nofold; v = a / b; cmp = 0; break;
        case B_EQ: v = a == b; break; case B_NE: v = a != b; break; case B_LT: v = a < b; break;
        case B_LE: v = a <= b; break; case B_GT: v = a > b; break; case B_GE: v = a >= b; break;
        default: goto nofold;
        }
        return cmp ? mknum((long long)v, resty) : mkfnum(v, resty);
    }
nofold:
    if (l->k == N_NUM && r->k == N_NUM && is_int(resty)) {
        Type *ct = l->ty; ct = (ct->uns || !is_int(ct)) ? ct : ct;
        Node *f = fold_bin(op, l, r, uns ? ty_ulong : ty_long, resty);
        if (f) { f->ty = resty; f->val = norm(f->val, resty); return f; }
    }
    Node *n = mk(N_BIN, resty); n->op = op; n->l = l; n->r = r; n->cmpty_uns = uns; return n;
}

static void ptr_compat_check(Type *to, Type *from, Node *src, const char *ctx_func, int pnum)
{
    (void)ctx_func; (void)pnum;
    if (is_ptr(to) && is_int(from)) {
        if (!is_null_const(src)) WARN(CX->opt.w_pointer, "Non-portable pointer conversion");
    } else if (is_int(to) && is_ptr(from)) {
        WARN(CX->opt.w_pointer, "Non-portable pointer conversion");
    } else if (is_ptr(to) && is_ptr(from)) {
        if (to->base->kind == TY_VOID || from->base->kind == TY_VOID) return;
        if (!same_type(to->base, from->base)) {
            /* signedness-only difference still counts, like the reference compiler */
            WARN(CX->opt.w_pointer, "Suspicious pointer conversion");
        }
    }
}

/* coerce 'n' to type 'to' for assignment/initialisation/argument passing */
static Node *coerce(Node *n, Type *to, int pnum, const char *fname)
{
    n = rv(n);
    Type *from = n->ty;
    if (to->kind == TY_VOID) return n;
    if (is_agg(to) || is_agg(from)) {
        if (is_agg(to) && is_agg(from) && same_type(to, from) && to->members == from->members) return n;
        if (pnum) ERR("Type mismatch in parameter %d in call to '%s'", pnum, fname);
        ERR("Cannot convert '%s' to '%s'", type_str(from), type_str(to));
    }
    if (to->kind == TY_FUNC || to->kind == TY_ARRAY) ERR("Cannot convert '%s' to '%s'", type_str(from), type_str(to));
    if (from->kind == TY_VOID) ERR("Not an allowed type");
    if ((is_flt(to) && is_ptr(from)) || (is_ptr(to) && is_flt(from))) { if (pnum) ERR("Type mismatch in parameter %d in call to '%s'", pnum, fname); ERR("Cannot convert '%s' to '%s'", type_str(from), type_str(to)); }
    ptr_compat_check(to, from, n, fname, pnum);
    return conv(n, to);
}

/* ---------- constants ---------- */
static Node *parse_assign(void);
static Node *parse_expr(void);
static Node *parse_cond(void);
static Node *parse_unary(void);
static Type *parse_declspec(int *storage, int *is_typedef, int *got_type);
static Type *parse_declarator(Type *base, char **name);
static Type *parse_typename(void);
static Node *parse_stmt(void);
static int is_type_start(Tok *t);

static long long const_expr(void)
{
    Node *n = parse_cond();
    if (n->k != N_NUM) ERR("Constant expression required");
    return n->val;
}

/* address constant for global initialisers */
static int addr_const(Node *n, int *kind, Sym **sym, long long *add)
{
    switch (n->k) {
    case N_CAST: if (is_ptr(n->ty) || n->ty->size == 8) return addr_const(n->l, kind, sym, add); return 0;
    case N_STR: *kind = 0; *sym = NULL; *add = n->val; return 1;
    case N_ADDR: {
        Node *o = n->l;
        if (o->k == N_STR) { *kind = 0; *sym = NULL; *add = o->val; return 1; }
        if (o->k == N_VAR && (o->sym->global || o->sym->kind == SY_FUNC)) {
            if (o->sym->kind == SY_FUNC) { *kind = 2; *sym = o->sym; *add = 0; }
            else { *kind = o->sym->bss ? 1 : 3; *sym = o->sym; *add = 0; }
            return 1;
        }
        if (o->k == N_MEMBER && o->l->k == N_VAR && o->l->sym->global) {
            Sym *s = o->l->sym; *kind = 3; *sym = s; *add = o->val; return 1;
        }
        if (o->k == N_DEREF) {  /* &arr[i] -> (arr + i) */
            return addr_const(o->l, kind, sym, add);
        }
        return 0;
    }
    case N_BIN:
        if (n->op == B_ADD || n->op == B_SUB) {
            long long a2;
            if (n->r->k == N_NUM && addr_const(n->l, kind, sym, add)) {
                a2 = n->r->val; *add += n->op == B_ADD ? a2 : -a2; return 1;
            }
            if (n->op == B_ADD && n->l->k == N_NUM && addr_const(n->r, kind, sym, add)) { *add += n->l->val; return 1; }
        }
        return 0;
    }
    return 0;
}

/* ---------- initializers ---------- */
typedef struct InitItem { long long off; Type *ty; Node *e; struct InitItem *next; int bw, bsh, buns; } InitItem;
static Member *init_bf;      /* bit field that receives the next scalar initialiser */
typedef struct { InitItem *head, *tail; long long maxend; } Sink;

static void sink_add(Sink *s, long long off, Type *ty, Node *e)
{
    InitItem *it = xalloc(sizeof *it);
    it->off = off; it->ty = ty; it->e = e;
    if (init_bf) { it->bw = init_bf->bw; it->bsh = init_bf->bsh; it->buns = init_bf->buns; init_bf = NULL; }
    if (s->tail) s->tail->next = it; else s->head = it;
    s->tail = it;
    if (off + ty->size > s->maxend) s->maxend = off + ty->size;
}

static void init_object(Type *ty, long long off, Sink *sk);

static Member *find_member(Type *st, const char *name, long long *off)
{
    for (Member *m = st->members; m; m = m->next) {
        if (m->name == name) { *off += m->off; return m; }
        if (!m->name && is_agg(m->ty)) {
            long long o2 = *off + m->off;
            Member *r = find_member(m->ty, name, &o2);
            if (r) { *off = o2; return r; }
        }
    }
    return NULL;
}

static void init_elems(Type *ty, long long off, Sink *sk, int braced)
{
    if (ty->kind == TY_ARRAY) {
        long long i = 0, maxi = 0;
        Type *el = ty->base;
        while (toks[tp].k != P_RBRC && toks[tp].k != TK_EOF) {
            if (!braced && ty->len >= 0 && i >= ty->len) break;
            if (braced && toks[tp].k == P_LBRK) {
                tp++; i = const_expr(); expect(P_RBRK, "']' expected"); expect(P_ASSIGN, "'=' expected");
            }
            if (ty->len >= 0 && i >= ty->len) ERR("Too many initializers");
            init_object(el, off + i * el->size, sk);
            i++; if (i > maxi) maxi = i;
            if (!accept(P_COMMA)) break;
            if (!braced && toks[tp].k == P_RBRC) break;
        }
        if (ty->len < 0) { ty->len = (int)maxi; ty->size = (int)maxi * el->size; }
        return;
    }
    /* struct / union */
    Member *m = ty->members;
    int first = 1;
    while (toks[tp].k != P_RBRC && toks[tp].k != TK_EOF) {
        if (braced && toks[tp].k == P_DOT) {
            tp++; Tok *nm = next();
            if (nm->k != TK_ID) ERR("Identifier expected");
            long long o = 0; Member *f = find_member(ty, nm->s, &o);
            if (!f) ERRAT(nm, "'%s' is not a member of '%s'", nm->s, type_str(ty));
            expect(P_ASSIGN, "'=' expected");
            init_bf = f->bw ? f : NULL;
            init_object(f->ty, off + o, sk);
            m = f->next; first = 0;
        } else {
            while (m && !m->name && m->bw) m = m->next;          /* unnamed bit fields take no initialiser */
            if (!m || (ty->kind == TY_UNION && !first)) { if (braced) ERR("Too many initializers"); break; }
            init_bf = m->bw ? m : NULL;
            init_object(m->ty, off + m->off, sk);
            m = m->next; first = 0;
        }
        if (!accept(P_COMMA)) break;
        if (!braced && toks[tp].k == P_RBRC) break;
    }
}

static void init_object(Type *ty, long long off, Sink *sk)
{
    Member *bf = init_bf; init_bf = NULL;
    if (toks[tp].k == P_LBRC) {
        tp++;
        if (ty->kind == TY_ARRAY || is_agg(ty)) {
            init_elems(ty, off, sk, 1);
        } else {
            init_bf = bf;
            init_object(ty, off, sk);
            accept(P_COMMA);
        }
        expect(P_RBRC, "Compound statement missing }");
        return;
    }
    if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR && toks[tp].k == TK_STR) {
        Tok *t = next(); int n = t->slen;
        while (toks[tp].k == TK_STR) { Tok *u = next(); char *nb = xalloc(n + u->slen + 1); memcpy(nb, t->s, n); memcpy(nb + n, u->s, u->slen); t = &(Tok){0}; t->s = nb; n += u->slen; }
        if (ty->len < 0) { ty->len = n + 1; ty->size = n + 1; }
        if (n > ty->len) ERR("Too many initializers");
        for (int i = 0; i < n; i++) sink_add(sk, off + i, ty_char, mknum((signed char)(t->s ? t->s[i] : 0), ty_char));
        if (n < ty->len) sink_add(sk, off + n, ty_char, mknum(0, ty_char));
        return;
    }
    if (ty->kind == TY_ARRAY || is_agg(ty)) {
        /* aggregate expression initialiser: struct = struct */
        if (is_agg(ty)) {
            int save = tp; Node *e = parse_assign();
            if (is_agg(e->ty) && same_type(e->ty, ty)) { sink_add(sk, off, ty, e); return; }
            tp = save;
        }
        init_elems(ty, off, sk, 0);   /* brace elision */
        return;
    }
    Node *e = parse_assign();
    e = coerce(e, ty, 0, NULL);
    init_bf = bf;
    sink_add(sk, off, ty, e);
}

static void store_scalar(uint8_t *p, long long v, int size)
{
    for (int i = 0; i < size; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* apply a sink to the global data buffer at 'base' */
static void apply_global_init(Sink *sk, int base)
{
    for (InitItem *it = sk->head; it; it = it->next) {
        Node *e = it->e;
        while (e->k == N_CAST && is_scalar(e->ty) && is_scalar(e->l->ty) && e->l->ty->size >= e->ty->size && e->l->k != N_NUM && !is_ptr(e->ty) == 0) e = e->l;
        int off = base + (int)it->off;
        if (is_agg(it->ty)) ERR("Initializer must be a constant expression");
        if (e->k == N_FNUM) {
            if (it->ty->kind == TY_FLOAT) { float f = (float)e->fval; memcpy(PG.data + off, &f, 4); }
            else { double dd = e->fval; memcpy(PG.data + off, &dd, 8); }
            continue;
        }
        if (it->bw) {
            if (e->k != N_NUM) ERR("Initializer must be a constant expression");
            uint32_t unit, mask = (it->bw == 32 ? 0xFFFFFFFFu : ((1u << it->bw) - 1));
            memcpy(&unit, PG.data + off, 4);
            unit = (unit & ~(mask << it->bsh)) | (((uint32_t)e->val & mask) << it->bsh);
            memcpy(PG.data + off, &unit, 4);
            continue;
        }
        if (e->k == N_NUM) { store_scalar(PG.data + off, e->val, it->ty->size); continue; }
        int kind; Sym *s; long long add;
        if (it->ty->size == 8 && addr_const(e, &kind, &s, &add)) {
            if (kind == 0) { store_scalar(PG.data + off, 0, 8); add_reloc(off, 0, NULL, add); }
            else if (kind == 2) add_reloc(off, 2, s, 0);
            else { add_reloc(off, kind == 1 ? 1 : 4, s, add); }
            continue;
        }
        ERR("Initializer must be a constant expression");
    }
}

/* ---------- expressions ---------- */
static int in_function_for_msg(char *buf, int n)
{
    if (curfn) { snprintf(buf, n, " in function %s", curfn->name); return 1; }
    buf[0] = 0; return 0;
}

static Node *mkvar(Sym *y)
{
    Node *n = mk(N_VAR, y->ty); n->sym = y; y->used = 1;
    if (y->kind == SY_FUNC && curfn && !in_rt == !curfn->from_rt) {
        /* record call-graph edge */
    }
    if (y->kind == SY_FUNC || (y->kind == SY_VAR && y->global)) {
        if (curfn) {
            if (curfn->nrefs == curfn->caprefs) {
                int nc = curfn->caprefs ? curfn->caprefs * 2 : 8;
                Sym **nr = xalloc(nc * sizeof(Sym *));
                if (curfn->nrefs) memcpy(nr, curfn->refs, curfn->nrefs * sizeof(Sym *));
                curfn->refs = nr; curfn->caprefs = nc;
            }
            curfn->refs[curfn->nrefs++] = y;
        } else {
            /* referenced from a global initializer: always keep */
            y->reach = y->reach | 2;
        }
    }
    return n;
}

static Node *parse_string(void)
{
    Tok *t = next();
    int n = t->slen;
    char *buf = xalloc(n + 1); memcpy(buf, t->s, n);
    while (toks[tp].k == TK_STR) {
        Tok *u = next(); char *nb = xalloc(n + u->slen + 1);
        memcpy(nb, buf, n); memcpy(nb + n, u->s, u->slen); n += u->slen; buf = nb;
    }
    int off = data_alloc(n + 1, 1);
    memcpy(PG.data + off, buf, n);
    Node *s = mk(N_STR, array_of(ty_char, n + 1)); s->val = off; s->str = buf; s->slen = n;
    return s;
}

static int is_type_start(Tok *t)
{
    switch (t->k) {
    case K_CHAR: case K_SHORT: case K_INT: case K_LONG: case K_SIGNED: case K_UNSIGNED: case K_VOID:
    case K_STRUCT: case K_UNION: case K_ENUM: case K_CONST: case K_VOLATILE: case K_FLOAT: case K_DOUBLE:
        return 1;
    case K_IGNORE: case K_STATIC: case K_EXTERN: case K_AUTO: case K_REGISTER: case K_TYPEDEF: return 1;
    case TK_ID: { Sym *y = lookup(t->s); return y && y->kind == SY_TYPEDEF; }
    }
    return 0;
}

static Node *parse_call(Node *fn)
{
    Tok *start = &toks[tp - 1];
    Type *ft = fn->ty;
    Node *callee = fn;
    Sym *direct = NULL;
    if (fn->k == N_VAR && fn->sym->kind == SY_FUNC) direct = fn->sym;
    else {
        callee = rv(fn);
        if (is_ptr(callee->ty) && callee->ty->base->kind == TY_FUNC) ft = callee->ty->base;
        else if (callee->ty->kind == TY_FUNC) ft = callee->ty;
        else ERRAT(start, "Function call expected");   /* not callable */
    }
    const char *fname = direct ? direct->name : "function";
    Node *head = NULL, **tail = &head; int argc = 0;
    Param *pp = ft->noproto ? NULL : ft->params;
    if (toks[tp].k != P_RPAR) {
        for (;;) {
            Node *a = parse_assign();
            a = rv(a);
            argc++;
            if (!ft->noproto && pp) {
                a = coerce(a, pp->ty, argc, fname);
                pp = pp->next;
            } else {
                if (a->ty->kind == TY_VOID) ERR("Not an allowed type");
                if (!is_agg(a->ty)) {
                    a = promote(a);
                    if (a->ty->kind == TY_FLOAT) a = conv(a, ty_double);
                }
                if (!ft->noproto && !ft->variadic) ERR("Too many parameters in call to '%s'", fname);
            }
            *tail = a; tail = &a->next;
            if (!accept(P_COMMA)) break;
        }
    }
    expect(P_RPAR, "Function call missing )");
    if (!ft->noproto && pp) ERR("Too few parameters in call to '%s'", fname);
    Node *c = mk(N_CALL, ft->ret);
    c->l = callee; c->sym = direct; c->r = head; c->val = argc;
    if (is_agg(ft->ret)) {
        /* the result is built in a hidden frame slot whose address is passed as an extra first argument */
        int a = ft->ret->align > 0 ? ft->ret->align : 1;
        if (!curfn) ERR("Initializer element is not a constant");
        fn_frame = (fn_frame + ft->ret->size + a - 1) & ~(a - 1);
        c->cmpty_uns = -fn_frame;
    }
    return c;
}

static Node *member_access(Node *base, Tok *nm, int arrow)
{
    Type *st;
    if (arrow) {
        base = rv(base);
        if (!is_ptr(base->ty) || !is_agg(base->ty->base)) ERRAT(nm, "Structure required on left side of -> or ->*");
        Node *d = mk(N_DEREF, base->ty->base); d->l = base; base = d;
    } else if (!is_agg(base->ty)) ERRAT(nm, "Structure required on left side of . or .*");
    st = base->ty;
    if (!st->complete) ERRAT(nm, "Undefined structure '%s'", st->tag ? st->tag : "");
    long long off = 0;
    Member *m = find_member(st, nm->s, &off);
    if (!m) ERRAT(nm, "Undefined structure member '%s'", nm->s);
    Node *n = mk(N_MEMBER, m->ty); n->l = base; n->val = off;
    n->bw = m->bw; n->bsh = m->bsh; n->buns = m->buns;
    return n;
}

static Node *incdec(int kind, Node *lv)
{
    if (!is_lvalue(lv) || lv->ty->kind == TY_ARRAY) ERR("Lvalue required");
    if (!is_scalar(lv->ty)) ERR("Invalid use of %s", kind == N_PREINC || kind == N_POSTINC ? "++" : "--");
    Node *n = mk(kind, lv->ty); n->l = lv;
    n->val = is_ptr(lv->ty) ? (lv->ty->base->size ? lv->ty->base->size : 1) : 1;
    if (lv->k == N_VAR) lv->sym->assigned = 1;
    return n;
}

static Node *parse_postfix(Node *n)
{
    for (;;) {
        Tok *t = peek();
        switch (t->k) {
        case P_LBRK: {
            tp++;
            Node *idx = parse_expr();
            expect(P_RBRK, "Expression syntax");
            Node *a = rv(n), *b = rv(idx);
            if (is_int(a->ty) && is_ptr(b->ty)) { Node *x = a; a = b; b = x; }
            if (!is_ptr(a->ty) || !is_int(b->ty)) ERRAT(t, "Size of the type is unknown or zero");
            if (a->ty->base->kind == TY_VOID || (!a->ty->base->size && a->ty->base->kind != TY_FUNC)) ERRAT(t, "Size of the type is unknown or zero");
            Node *scaled = mkbin(B_MUL, conv(b, ty_long), mknum(a->ty->base->size, ty_long), ty_long, 0);
            Node *sum = mkbin(B_ADD, a, scaled, a->ty, 1);
            Node *d = mk(N_DEREF, a->ty->base); d->l = sum; n = d;
            break;
        }
        case P_LPAR: tp++; n = parse_call(n); break;
        case P_DOT: tp++; { Tok *nm = next(); if (nm->k != TK_ID) ERRAT(nm, "Identifier expected"); n = member_access(n, nm, 0); } break;
        case P_ARROW: tp++; { Tok *nm = next(); if (nm->k != TK_ID) ERRAT(nm, "Identifier expected"); n = member_access(n, nm, 1); } break;
        case P_INC: tp++; n = incdec(N_POSTINC, n); break;
        case P_DEC: tp++; n = incdec(N_POSTDEC, n); break;
        default: return n;
        }
    }
}

static Node *parse_primary(void)
{
    Tok *t = peek();
    switch (t->k) {
    case TK_NUM: {
        tp++;
        if (t->isfloat) return mkfnum(t->dval, t->fsuf ? ty_float : ty_double);
        Type *ty;
        long long v = t->val;
        int dec = t->s && t->s[0] != '0';
        if (t->lng && t->uns) ty = ty_ulong;
        else if (t->lng) ty = (v < 0 || (!dec && 0)) ? ty_ulong : ty_long;
        else if (t->uns) ty = (uint64_t)v > 0xffffffffu ? ty_ulong : ty_uint;
        else if ((uint64_t)v <= 0x7fffffffu) ty = ty_int;
        else if ((uint64_t)v <= 0xffffffffu && !dec) ty = ty_uint;
        else if (v >= 0) ty = ty_long;
        else ty = ty_ulong;
        return mknum(v, ty);
    }
    case TK_CHR: tp++; return mknum(t->val, ty_int);
    case TK_STR: return parse_string();
    case P_LPAR: {
        tp++;
        Node *e = parse_expr();
        expect(P_RPAR, "Expression syntax");
        return e;
    }
    case TK_ID: {
        tp++;
        Sym *y = lookup(t->s);
        if (!y) {
            if (toks[tp].k == P_LPAR) {
                /* implicit declaration: int name() */
                Type *ft = mkty(TY_FUNC, 0, 0); ft->ret = ty_int; ft->noproto = 1; ft->complete = 1;
                y = add_sym(gscope, t->s, ft, SY_FUNC);
                y->storage = SC_EXTERN;
                WARNAT(CX->opt.w_noproto, t->line, "Call to function '%s' with no prototype", t->s);
            } else {
                char fb[80]; in_function_for_msg(fb, sizeof fb);
                ERRAT(t, "Undefined symbol '%s'%s", t->s, fb);
            }
        }
        if (y->kind == SY_TYPEDEF) ERRAT(t, "Expression syntax");
        if (y->kind == SY_ENUM) return mknum(y->val, ty_int);
        return mkvar(y);
    }
    default: break;
    }
    ERR("Expression syntax");
    return NULL;
}

static Node *make_unary(int tk, Tok *t)
{
    Node *o;
    switch (tk) {
    case P_AMP: {
        o = parse_unary();
        if (o->k == N_CAST && 0) {}
        if (o->ty->kind == TY_FUNC || (o->k == N_VAR && o->sym->kind == SY_FUNC)) return rv(o);
        if (!is_lvalue(o)) ERRAT(t, "Must take address of a memory location");
        if (o->k == N_MEMBER && o->bw) ERRAT(t, "Illegal to take address of bit field");
        if (o->k == N_VAR) { o->sym->used = 1; }
        Node *a = mk(N_ADDR, ptr_to(o->ty)); a->l = o; return a;
    }
    case P_STAR: {
        o = rv(parse_unary());
        if (!is_ptr(o->ty)) ERRAT(t, "Cannot dereference a non-pointer");
        if (o->ty->base->kind == TY_VOID) ERRAT(t, "Not an allowed type");
        Node *d = mk(N_DEREF, o->ty->base); d->l = o; return d;
    }
    case P_PLUS: o = promote(parse_unary()); if (!is_int(o->ty) && !is_flt(o->ty)) ERRAT(t, "Illegal use of pointer"); return o;
    case P_MINUS: {
        o = promote(parse_unary());
        if (!is_int(o->ty) && !is_flt(o->ty)) ERRAT(t, "Illegal use of pointer");
        if (o->k == N_FNUM) return mkfnum(-o->fval, o->ty);
        if (o->k == N_NUM) return mknum(-o->val, o->ty);
        Node *n = mk(N_NEG, o->ty); n->l = o; return n;
    }
    case P_TILDE: {
        o = promote(parse_unary());
        if (!is_int(o->ty)) ERRAT(t, is_flt(o->ty) ? "Illegal use of floating point" : "Illegal use of pointer");
        if (o->k == N_NUM) return mknum(~o->val, o->ty);
        Node *n = mk(N_BITNOT, o->ty); n->l = o; return n;
    }
    case P_BANG: {
        o = rv(parse_unary());
        if (!is_scalar(o->ty)) ERRAT(t, "Illegal use of structure");
        if (o->k == N_NUM) return mknum(!o->val, ty_int);
        if (o->k == N_FNUM) return mknum(o->fval == 0, ty_int);
        Node *n = mk(N_NOT, ty_int); n->l = o; return n;
    }
    }
    return NULL;
}

static Node *parse_unary(void)
{
    Tok *t = peek();
    switch (t->k) {
    case P_AMP: case P_STAR: case P_PLUS: case P_MINUS: case P_TILDE: case P_BANG:
        tp++;
        return make_unary(t->k, t);
    case P_INC: case P_DEC:
        tp++;
        return incdec(t->k == P_INC ? N_PREINC : N_PREDEC, parse_unary());
    case K_SIZEOF: {
        tp++;
        long long sz;
        if (toks[tp].k == P_LPAR && is_type_start(&toks[tp + 1])) {
            tp++; Type *ty = parse_typename(); expect(P_RPAR, "Expression syntax");
            sz = ty->size;
            if (ty->kind == TY_FUNC || ty->kind == TY_VOID) ERRAT(t, "Size of the type is unknown or zero");
            if (!ty->complete || (ty->kind == TY_ARRAY && ty->len < 0)) ERRAT(t, "Size of the type is unknown or zero");
        } else {
            int sv_ref = 0; (void)sv_ref;
            Node *e = parse_unary();
            if (e->k == N_MEMBER && e->bw) ERRAT(t, "sizeof may not be applied to a bit field");
            sz = e->ty->size;
            if (e->ty->kind == TY_FUNC) sz = 1;
        }
        return mknum(sz, ty_ulong);
    }
    case P_LPAR:
        if (is_type_start(&toks[tp + 1])) {
            tp++;
            Type *ty = parse_typename();
            expect(P_RPAR, "Expression syntax");
            if (toks[tp].k == P_LBRC) ERR("Compound literals are not supported");
            Node *e = parse_unary();
            e = rv(e);
            if (ty->kind == TY_VOID) { Node *c = mk(N_CAST, ty); c->l = e; return c; }
            if (!is_scalar(ty)) ERRAT(t, "Cannot cast from '%s' to '%s'", type_str(e->ty), type_str(ty));
            if (!is_scalar(e->ty)) ERRAT(t, "Cannot cast from '%s' to '%s'", type_str(e->ty), type_str(ty));
            return conv(e, ty);
        }
        /* fallthrough */
    default:
        return parse_postfix(parse_primary());
    }
}

/* binary operators */
static int binprec(int k)
{
    switch (k) {
    case P_STAR: case P_SLASH: case P_PERCENT: return 11;
    case P_PLUS: case P_MINUS: return 10;
    case P_SHL: case P_SHR: return 9;
    case P_LT: case P_GT: case P_LE: case P_GE: return 8;
    case P_EQ: case P_NE: return 7;
    case P_AMP: return 6; case P_CARET: return 5; case P_BAR: return 4;
    case P_ANDAND: return 3; case P_OROR: return 2;
    }
    return 0;
}
static int tk_to_bop(int k)
{
    switch (k) {
    case P_STAR: return B_MUL; case P_SLASH: return B_DIV; case P_PERCENT: return B_MOD;
    case P_PLUS: return B_ADD; case P_MINUS: return B_SUB; case P_SHL: return B_SHL; case P_SHR: return B_SHR;
    case P_LT: return B_LT; case P_GT: return B_GT; case P_LE: return B_LE; case P_GE: return B_GE;
    case P_EQ: return B_EQ; case P_NE: return B_NE; case P_AMP: return B_AND; case P_CARET: return B_XOR; case P_BAR: return B_OR;
    case P_ADDA: return B_ADD; case P_SUBA: return B_SUB; case P_MULA: return B_MUL; case P_DIVA: return B_DIV;
    case P_MODA: return B_MOD; case P_SHLA: return B_SHL; case P_SHRA: return B_SHR; case P_ANDA: return B_AND;
    case P_XORA: return B_XOR; case P_ORA: return B_OR;
    }
    return -1;
}

static Node *new_binary(int op, Node *l, Node *r, Tok *t)
{
    l = rv(l); r = rv(r);
    if (l->ty->kind == TY_VOID || r->ty->kind == TY_VOID || is_agg(l->ty) || is_agg(r->ty))
        ERRAT(t, "Illegal use of structure");
    int lp = is_ptr(l->ty), rp = is_ptr(r->ty);
    if (is_flt(l->ty) || is_flt(r->ty)) {
        if (lp || rp) ERRAT(t, "Illegal use of pointer");
        if (op == B_MOD || op == B_AND || op == B_OR || op == B_XOR || op == B_SHL || op == B_SHR) ERRAT(t, "Illegal use of floating point");
        Type *ct = arith_type(l->ty, r->ty);
        l = conv(l, ct); r = conv(r, ct);
        if (op >= B_EQ) return mkbin(op, l, r, ty_int, 0);
        if (op == B_DIV && r->k == N_FNUM && r->fval == 0) WARN(CX->opt.w_divzero, "Division by zero");
        return mkbin(op, l, r, ct, 0);
    }
    if (op == B_ADD) {
        if (lp && rp) ERRAT(t, "Invalid pointer addition");
        if (rp) { Node *x = l; l = r; r = x; lp = 1; rp = 0; }
        if (lp) {
            int sz = l->ty->base->size; if (l->ty->base->kind == TY_VOID) ERRAT(t, "Size of the type is unknown or zero");
            if (l->ty->base->kind == TY_FUNC) sz = 1;
            if (!sz) ERRAT(t, "Size of the type is unknown or zero");
            return mkbin(B_ADD, l, mkbin(B_MUL, conv(r, ty_long), mknum(sz, ty_long), ty_long, 0), l->ty, 1);
        }
    }
    if (op == B_SUB) {
        if (lp && rp) {
            if (!same_type(l->ty->base, r->ty->base) && l->ty->base->kind != TY_VOID && r->ty->base->kind != TY_VOID) WARN(CX->opt.w_pointer, "Suspicious pointer conversion");
            int sz = l->ty->base->size ? l->ty->base->size : 1;
            Node *d = mkbin(B_SUB, conv(l, ty_long), conv(r, ty_long), ty_long, 0);
            return mkbin(B_DIV, d, mknum(sz, ty_long), ty_long, 0);
        }
        if (lp) {
            int sz = l->ty->base->size; if (!sz) ERRAT(t, "Size of the type is unknown or zero");
            return mkbin(B_SUB, l, mkbin(B_MUL, conv(r, ty_long), mknum(sz, ty_long), ty_long, 0), l->ty, 1);
        }
        if (rp) ERRAT(t, "Invalid pointer subtraction");
    }
    if (op >= B_EQ) {
        if (lp || rp) {
            if (lp && rp && !same_type(l->ty->base, r->ty->base) && l->ty->base->kind != TY_VOID && r->ty->base->kind != TY_VOID)
                WARN(CX->opt.w_pointer, "Suspicious pointer conversion");
            else if ((lp && is_int(r->ty) && !is_null_const(r)) || (rp && is_int(l->ty) && !is_null_const(l)))
                WARN(CX->opt.w_pointer, "Non-portable pointer comparison");
            return mkbin(op, conv(l, ty_ulong), conv(r, ty_ulong), ty_int, 1);
        }
        Type *ct = arith_type(promote(l)->ty, promote(r)->ty);
        return mkbin(op, conv(promote(l), ct), conv(promote(r), ct), ty_int, ct->uns);
    }
    if (lp || rp) ERRAT(t, "Illegal use of pointer");
    l = promote(l); r = promote(r);
    if (op == B_SHL || op == B_SHR) {
        return mkbin(op, l, conv(r, ty_int), l->ty, l->ty->uns);
    }
    Type *ct = arith_type(l->ty, r->ty);
    if ((op == B_DIV || op == B_MOD) && r->k == N_NUM && r->val == 0) {
        WARN(CX->opt.w_divzero, "Division by zero");
        Node *n = mk(N_BIN, ct); n->op = op; n->l = conv(l, ct); n->r = conv(r, ct); n->cmpty_uns = ct->uns; return n;
    }
    return mkbin(op, conv(l, ct), conv(r, ct), ct, ct->uns);
}

static Node *parse_binary(int minp)
{
    Node *l = parse_unary();
    for (;;) {
        Tok *t = peek();
        int p = binprec(t->k);
        if (!p || p < minp) return l;
        tp++;
        Node *r = parse_binary(p + 1);
        if (t->k == P_ANDAND || t->k == P_OROR) {
            l = rv(l); r = rv(r);
            if (!is_scalar(l->ty) || !is_scalar(r->ty)) ERRAT(t, "Illegal use of structure");
            if (l->k == N_NUM && r->k == N_NUM) l = mknum(t->k == P_ANDAND ? (l->val && r->val) : (l->val || r->val), ty_int);
            else { Node *n = mk(t->k == P_ANDAND ? N_LOGAND : N_LOGOR, ty_int); n->l = l; n->r = r; l = n; }
        } else l = new_binary(tk_to_bop(t->k), l, r, t);
    }
}

static Node *parse_cond(void)
{
    Node *c = parse_binary(2);
    if (toks[tp].k != P_QUEST) return c;
    Tok *t = next();
    c = rv(c);
    if (!is_scalar(c->ty)) ERRAT(t, "Illegal use of structure");
    Node *a = rv(parse_expr());
    expect(P_COLON, "Expression syntax");
    Node *b = rv(parse_cond());
    Type *rt = ty_int;
    if (a->ty->kind == TY_VOID || b->ty->kind == TY_VOID) rt = ty_void;
    else if ((is_flt(a->ty) || is_flt(b->ty)) && is_scalar(a->ty) && is_scalar(b->ty) && !is_ptr(a->ty) && !is_ptr(b->ty)) { rt = arith_type(a->ty, b->ty); a = conv(a, rt); b = conv(b, rt); }
    else if (is_int(a->ty) && is_int(b->ty)) { a = promote(a); b = promote(b); rt = arith_type(a->ty, b->ty); a = conv(a, rt); b = conv(b, rt); }
    else if (is_ptr(a->ty) && is_ptr(b->ty)) { rt = a->ty; if (a->ty->base->kind == TY_VOID) rt = b->ty; }
    else if (is_ptr(a->ty) && is_int(b->ty)) { rt = a->ty; b = conv(b, rt); }
    else if (is_ptr(b->ty) && is_int(a->ty)) { rt = b->ty; a = conv(a, rt); }
    else if (is_agg(a->ty) && same_type(a->ty, b->ty)) rt = a->ty;
    else ERRAT(t, "Type mismatch in conditional expression");
    if (c->k == N_NUM) return c->val ? a : b;
    Node *n = mk(N_COND, rt); n->c = c; n->l = a; n->r = b; return n;
}

static Node *parse_assign(void)
{
    Node *l = parse_cond();
    Tok *t = peek();
    if (t->k == P_ASSIGN) {
        tp++;
        if (!is_lvalue(l)) ERRAT(t, "Lvalue required");
        if (l->ty->kind == TY_ARRAY) ERRAT(t, "Lvalue required");
        Node *r = parse_assign();
        r = coerce(r, l->ty, 0, NULL);
        Node *n = mk(N_ASSIGN, l->ty); n->l = l; n->r = r;
        if (l->k == N_VAR) l->sym->assigned = 1;
        return n;
    }
    int op = tk_to_bop(t->k);
    if (op >= 0 && t->k >= P_MULA && t->k <= P_ORA) {
        tp++;
        if (!is_lvalue(l) || l->ty->kind == TY_ARRAY) ERRAT(t, "Lvalue required");
        Node *r = rv(parse_assign());
        Node *n = mk(N_ASSIGNOP, l->ty); n->op = op; n->l = l;
        if (is_ptr(l->ty)) {
            if (op != B_ADD && op != B_SUB) ERRAT(t, "Illegal use of pointer");
            int sz = l->ty->base->size; if (!sz) ERRAT(t, "Size of the type is unknown or zero");
            if (!is_int(r->ty)) ERRAT(t, "Illegal use of pointer");
            n->cty = ty_long;
            n->r = mkbin(B_MUL, conv(r, ty_long), mknum(sz, ty_long), ty_long, 0);
        } else {
            if (is_flt(l->ty) || is_flt(r->ty)) {
                if (is_ptr(l->ty) || is_ptr(r->ty)) ERRAT(t, "Illegal use of pointer");
                if (!is_scalar(l->ty) || !is_scalar(r->ty)) ERRAT(t, "Illegal use of structure");
                if (op != B_ADD && op != B_SUB && op != B_MUL && op != B_DIV) ERRAT(t, "Illegal use of floating point");
                n->cty = arith_type(l->ty, r->ty);
                n->r = conv(r, n->cty);
            } else if (!is_int(l->ty) || !is_int(r->ty)) ERRAT(t, is_ptr(r->ty) ? "Illegal use of pointer" : "Illegal use of structure");
            else if (op == B_SHL || op == B_SHR) {
                Type *pt = l->ty->size < 4 ? ty_int : l->ty;
                n->cty = pt; n->r = conv(r, ty_int);
            } else {
                n->cty = arith_type(promote(l)->ty, promote(r)->ty);
                n->r = conv(promote(r), n->cty);
            }
        }
        if (l->k == N_VAR) l->sym->assigned = 1;
        return n;
    }
    return l;
}

static Node *parse_expr(void)
{
    Node *n = parse_assign();
    while (toks[tp].k == P_COMMA) {
        tp++;
        Node *r = parse_assign();
        Node *c = mk(N_COMMA, rv(r)->ty); c->l = n; c->r = r; n = c;
    }
    return n;
}

/* ---------- declarations ---------- */
static Type *parse_struct(int is_union)
{
    Tok *kw = &toks[tp - 1];
    char *tag = NULL;
    if (toks[tp].k == TK_ID) tag = next()->s;
    Type *ty = NULL;
    if (tag) {
        Tag *tg = find_tag(tag, toks[tp].k == P_LBRC);
        if (tg) { ty = tg->ty; if (ty->kind != (is_union ? TY_UNION : TY_STRUCT)) ERRAT(kw, "Type mismatch in redeclaration of '%s'", tag); }
    }
    if (!ty) {
        ty = mkty(is_union ? TY_UNION : TY_STRUCT, 0, 0);
        ty->complete = 0; ty->tag = tag;
        if (tag) { Tag *tg = xalloc(sizeof *tg); tg->name = tag; tg->ty = ty; tg->next = scope->tags; scope->tags = tg; }
    }
    if (toks[tp].k != P_LBRC) { if (!tag) ERR("Declaration syntax error"); return ty; }
    tp++;
    if (ty->complete) ERRAT(kw, "Type mismatch in redeclaration of '%s'", tag ? tag : "");
    Member *head = NULL, **tail = &head;
    int off = 0, align = 1, size = 0;
    int unit = 0, bu = 0;          /* open bit-field unit: its offset and the bits already used (0 = none open) */
    while (toks[tp].k != P_RBRC) {
        if (toks[tp].k == TK_EOF) ERR("Declaration terminated incorrectly");
        int st, td, gt;
        Type *bt = parse_declspec(&st, &td, &gt);
        if (!gt) ERR("Declaration syntax error");
        if (toks[tp].k == P_SEMI) {   /* anonymous struct/union member */
            tp++;
            if (is_agg(bt) && !bt->tag) {
                Member *m = xalloc(sizeof *m); m->name = NULL; m->ty = bt;
                if (bt->align > align) align = bt->align;
                bu = 0;
                if (is_union) { m->off = 0; if (bt->size > size) size = bt->size; }
                else { off = (off + bt->align - 1) & ~(bt->align - 1); m->off = off; off += bt->size; size = off; }
                *tail = m; tail = &m->next;
            }
            continue;
        }
        for (;;) {
            char *name = NULL;
            Type *mt = toks[tp].k == P_COLON ? bt : parse_declarator(bt, &name);
            if (toks[tp].k == P_COLON) {                      /* bit field (C89: int, signed int, unsigned int) */
                Tok *ct = &toks[tp++];
                long long w = const_expr();
                if (mt->kind == TY_CHAR || mt->kind == TY_SHORT || mt->kind == TY_LONG) ERRAT(ct, "Bit fields must be signed or unsigned int");
                if (mt->kind != TY_INT) ERRAT(ct, "Bit fields must have integral type");
                if (w < 0 || (w == 0 && name)) ERRAT(ct, "Bit fields must contain at least one bit");
                if (w > 32) ERRAT(ct, "Bit field too large");
                if (name) for (Member *o = head; o; o = o->next) if (o->name == name) ERR("Duplicate member '%s'", name);
                if (4 > align) align = 4;
                if (w == 0) bu = 32;                          /* the next field starts a new unit */
                else {
                    Member *m = xalloc(sizeof *m);
                    m->name = name; m->bw = (int)w; m->buns = mt->uns;
                    m->ty = (mt->uns && w == 32) ? ty_uint : ty_int;      /* narrower unsigned fields promote to int */
                    if (is_union) { m->off = 0; m->bsh = 0; if (size < 4) size = 4; }
                    else {
                        if (bu == 0 || bu + (int)w > 32) { off = (off + 3) & ~3; unit = off; off += 4; size = off; bu = 0; }
                        m->off = unit; m->bsh = bu; bu += (int)w;
                    }
                    if (name) { *tail = m; tail = &m->next; }
                }
                if (!accept(P_COMMA)) break;
                continue;
            }
            if (!name) ERR("Declaration syntax error");
            if (mt->kind == TY_FUNC) ERR("Size of the type is unknown or zero");
            if (!mt->complete || (mt->kind == TY_ARRAY && mt->len < 0)) ERR("Size of the type is unknown or zero");
            for (Member *m = head; m; m = m->next) if (m->name == name) ERR("Duplicate member '%s'", name);
            Member *m = xalloc(sizeof *m);
            m->name = name; m->ty = mt;
            if (mt->align > align) align = mt->align;
            bu = 0;
            if (is_union) { m->off = 0; if (mt->size > size) size = mt->size; }
            else { off = (off + mt->align - 1) & ~(mt->align - 1); m->off = off; off += mt->size; size = off; }
            *tail = m; tail = &m->next;
            if (!accept(P_COMMA)) break;
        }
        expect(P_SEMI, "Declaration missing ;");
    }
    tp++;
    ty->members = head; ty->align = align; ty->size = (size + align - 1) & ~(align - 1); ty->complete = 1;
    if (!head) ty->size = 0;
    return ty;
}

static Type *parse_enum(void)
{
    char *tag = NULL;
    if (toks[tp].k == TK_ID) tag = next()->s;
    if (toks[tp].k != P_LBRC) {
        if (!tag) ERR("Declaration syntax error");
        return ty_int;
    }
    tp++;
    long long v = 0;
    while (toks[tp].k != P_RBRC) {
        Tok *nm = next();
        if (nm->k != TK_ID) ERRAT(nm, "Identifier expected");
        if (accept(P_ASSIGN)) v = const_expr();
        Sym *e = find_in(scope, nm->s);
        if (e) ERRAT(nm, "Multiple declaration for '%s'", nm->s);
        e = add_sym(scope, nm->s, ty_int, SY_ENUM); e->val = v; e->used = 1;
        v++;
        if (!accept(P_COMMA)) break;
    }
    expect(P_RBRC, "Compound statement missing }");
    return ty_int;
}

static Type *parse_declspec(int *storage, int *is_typedef, int *got_type)
{
    int sc = SC_NONE, td = 0;
    int nshort = 0, nlong = 0, nsigned = 0, nunsigned = 0, nchar = 0, nint = 0, nvoid = 0, nfloat = 0, ndouble = 0;
    Type *named = NULL;
    int any = 0;
    for (;;) {
        Tok *t = peek();
        switch (t->k) {
        case K_TYPEDEF: td = 1; tp++; continue;
        case K_STATIC: sc = SC_STATIC; tp++; continue;
        case K_EXTERN: sc = SC_EXTERN; tp++; continue;
        case K_AUTO: case K_REGISTER: tp++; continue;
        case K_CONST: case K_VOLATILE: case K_IGNORE: tp++; continue;
        case K_SHORT: nshort++; any = 1; tp++; continue;
        case K_LONG: nlong++; any = 1; tp++; continue;
        case K_SIGNED: nsigned++; any = 1; tp++; continue;
        case K_UNSIGNED: nunsigned++; any = 1; tp++; continue;
        case K_CHAR: nchar++; any = 1; tp++; continue;
        case K_INT: nint++; any = 1; tp++; continue;
        case K_VOID: nvoid++; any = 1; tp++; continue;
        case K_FLOAT: nfloat++; any = 1; tp++; note_fp(); continue;
        case K_DOUBLE: ndouble++; any = 1; tp++; note_fp(); continue;
        case K_STRUCT: case K_UNION:
            if (any || named) ERR("Too many types in declaration");
            tp++; named = parse_struct(t->k == K_UNION); any = 1; continue;
        case K_ENUM:
            if (any || named) ERR("Too many types in declaration");
            tp++; named = parse_enum(); any = 1; continue;
        case TK_ID: {
            if (any || named) break;
            Sym *y = lookup(t->s);
            if (y && y->kind == SY_TYPEDEF) { named = y->ty; any = 1; tp++; continue; }
            break;
        }
        default: break;
        }
        break;
    }
    *storage = sc; *is_typedef = td; *got_type = any;
    if (named) return named;
    if (!any) return ty_int;
    if (nvoid) return ty_void;
    if (ndouble) return ty_double;
    if (nfloat) return ty_float;
    if (nchar) return nunsigned || (!nsigned && CX->opt.unsigned_char) ? ty_uchar : ty_char;
    if (nshort) return nunsigned ? ty_ushort : ty_short;
    if (nlong) return nunsigned ? ty_ulong : ty_long;
    (void)nint;
    return nunsigned ? ty_uint : ty_int;
}

static Type *parse_typename(void)
{
    int st, td, gt;
    Type *bt = parse_declspec(&st, &td, &gt);
    if (!gt) ERR("Type name expected");
    char *nm = NULL;
    return parse_declarator(bt, &nm);
}

static Type *adjust_param(Type *t)
{
    if (t->kind == TY_ARRAY) return ptr_to(t->base);
    if (t->kind == TY_FUNC) return ptr_to(t);
    return t;
}

typedef struct ParamList { Param *head; int n, variadic, noproto, knr; } ParamList;
static ParamList last_params;    /* params of the most recent function declarator (for definitions) */

static Type *parse_suffixes(Type *base)
{
    if (toks[tp].k == P_LBRK) {
        tp++;
        int len = -1;
        if (toks[tp].k != P_RBRK) {
            long long n = const_expr();
            if (n <= 0) ERR("Size of the type is unknown or zero");
            len = (int)n;
        }
        expect(P_RBRK, "Expression syntax");
        Type *inner = parse_suffixes(base);
        if (inner->kind == TY_FUNC) ERR("Array of functions is not allowed");
        if (!inner->complete && inner->kind != TY_ARRAY) ERR("Size of the type is unknown or zero");
        if (inner->kind == TY_ARRAY && inner->len < 0) ERR("Size of the type is unknown or zero");
        return array_of(inner, len);
    }
    if (toks[tp].k == P_LPAR) {
        tp++;
        Type *ft = mkty(TY_FUNC, 0, 0);
        ft->ret = base; ft->complete = 1;
        ParamList pl = {0};
        Param **tail = &pl.head;
        if (toks[tp].k == P_RPAR) { pl.noproto = 1; }
        else if (toks[tp].k == K_VOID && toks[tp + 1].k == P_RPAR) { tp++; }
        else {
            for (;;) {
                if (toks[tp].k == P_ELLIPSIS) { tp++; pl.variadic = 1; break; }
                if (toks[tp].k == TK_ID && !is_type_start(&toks[tp])) {
                    /* old style identifier list */
                    Param *p = xalloc(sizeof *p); p->name = next()->s; p->ty = ty_int;
                    *tail = p; tail = &p->next; pl.n++; pl.knr = 1;
                } else {
                    int st, td, gt;
                    Type *bt = parse_declspec(&st, &td, &gt);
                    if (!gt) ERR("Declaration syntax error");
                    char *nm = NULL;
                    Type *pt = parse_declarator(bt, &nm);
                    if (pt->kind == TY_VOID && !nm) ERR("Not an allowed type");
                    Param *p = xalloc(sizeof *p); p->name = nm; p->ty = adjust_param(pt);
                    *tail = p; tail = &p->next; pl.n++;
                }
                if (!accept(P_COMMA)) break;
            }
        }
        expect(P_RPAR, "Function call missing )");
        ft->params = pl.head; ft->nparams = pl.n; ft->variadic = pl.variadic; ft->noproto = pl.noproto || pl.knr;
        last_params = pl;
        Type *r = ft;
        if (toks[tp].k == P_LBRK || toks[tp].k == P_LPAR) ERR("Function cannot return a function or array");
        return r;
    }
    return base;
}

static int nested_decl_ahead(void)
{
    if (toks[tp].k != P_LPAR) return 0;
    Tok *n = &toks[tp + 1];
    if (n->k == P_STAR || n->k == P_LPAR || n->k == K_IGNORE) return 1;
    if (n->k == TK_ID && !is_type_start(n)) return 1;
    return 0;
}

static Type *parse_declarator(Type *base, char **name)
{
    while (toks[tp].k == P_STAR) {
        tp++; base = ptr_to(base);
        while (toks[tp].k == K_CONST || toks[tp].k == K_VOLATILE || toks[tp].k == K_IGNORE) tp++;
    }
    while (toks[tp].k == K_IGNORE) tp++;
    if (nested_decl_ahead()) {
        int start = tp + 1, depth = 0, i = tp;
        for (;; i++) {
            if (toks[i].k == P_LPAR) depth++;
            else if (toks[i].k == P_RPAR) { if (--depth == 0) break; }
            else if (toks[i].k == TK_EOF) ERR("Declaration syntax error");
        }
        tp = i + 1;
        base = parse_suffixes(base);
        int save = tp; ParamList sp = last_params;
        tp = start;
        Type *t = parse_declarator(base, name);
        if (toks[tp].k != P_RPAR) ERR("Declaration syntax error");
        tp = save; last_params = sp;
        return t;
    }
    if (toks[tp].k == TK_ID) {
        Sym *y = lookup(toks[tp].s);
        (void)y;
        *name = next()->s;
    }
    return parse_suffixes(base);
}

/* ---------- function bodies & statements ---------- */
static Node *new_stmt(int k) { Node *n = mk(k, NULL); n->line = toks[tp].line; return n; }

static Sym *new_local(char *name, Type *ty)
{
    Sym *y = add_sym(scope, name, ty, SY_VAR);
    int a = ty->align > 0 ? ty->align : 1;
    fn_frame = (fn_frame + ty->size + a - 1) & ~(a - 1);
    if (!ty->size) fn_frame += 1;
    y->off = -fn_frame;
    dbg_note_var(y);
    return y;
}

static void sync_to_stmt_end(void)
{
    int depth = 0;
    while (toks[tp].k != TK_EOF) {
        int k = toks[tp].k;
        if (k == P_LBRC) depth++;
        else if (k == P_RBRC) { if (!depth) return; depth--; if (!depth) { tp++; return; } }
        else if (k == P_SEMI && !depth) { tp++; return; }
        tp++;
    }
}

static Node *parse_block(int newscope);

static Node *local_init_nodes(Sym *y, Sink *sk)
{
    /* zero-fill then assign each item */
    Node *head = NULL, **tail = &head;
    if (is_agg(y->ty) || y->ty->kind == TY_ARRAY) {
        Node *z = new_stmt(S_ZERO); z->sym = y; *tail = z; tail = &z->next;
    }
    for (InitItem *it = sk->head; it; it = it->next) {
        Node *base = mk(N_VAR, y->ty); base->sym = y;
        Node *lv;
        if (it->bw) { lv = mk(N_MEMBER, it->ty); lv->l = base; lv->val = it->off; lv->bw = it->bw; lv->bsh = it->bsh; lv->buns = it->buns; }
        else if (it->off == 0 && same_type(it->ty, y->ty) && !(y->ty->kind == TY_ARRAY)) lv = base;
        else {
            Node *a = mk(N_ADDR, ptr_to(ty_char)); a->l = base;
            Node *sum = mkbin(B_ADD, a, mknum(it->off, ty_long), ptr_to(it->ty), 1);
            Node *d = mk(N_DEREF, it->ty); d->l = sum; lv = d;
        }
        Node *as = mk(N_ASSIGN, it->ty); as->l = lv; as->r = it->e;
        Node *st = new_stmt(S_EXPR); st->l = as; st->line = y->line;
        *tail = st; tail = &st->next;
    }
    Node *b = new_stmt(S_BLOCK); b->l = head; b->val = 1; /* val=1: transparent block (no scope) */
    return b;
}

static void global_define_var(Sym *y, Sink *sk)
{
    int size = y->ty->size;
    y->fileid = cur_file();                      /* the module that defines it */
    if (sk) {
        int off = data_alloc(size ? size : 1, y->ty->align);
        y->bss = 0; y->off = off; y->defined = 1;
        apply_global_init(sk, off);
    } else if (!y->defined && y->storage != SC_EXTERN) {
        y->bss = 1; y->off = bss_alloc(size, y->ty->align); y->defined = 1;
    }
}

static Sym *declare_global(Tok *nmtok, char *name, Type *ty, int kind, int storage)
{
    Sym *y = find_in(gscope, name);
    if (y && y->hidden && !in_rt) {
        if (kind == SY_FUNC && !strcmp(name, "main")) { y->hidden = 0; y->ty = ty; }  /* any main() binds to the start-up code */
        else if (y->kind == kind && same_type(y->ty, ty)) y->hidden = 0;
        else { y->name = intern("<hidden>", 8); y = NULL; }
    }
    if (y) {
        if (y->kind != kind) ERRAT(nmtok, "Multiple declaration for '%s'", name);
        if (kind == SY_FUNC) {
            if (!same_type(y->ty, ty) && !(y->ty->noproto || ty->noproto)) ERRAT(nmtok, "Type mismatch in redeclaration of '%s'", name);
            if (y->ty->noproto && !ty->noproto) y->ty = ty;
        } else if (kind == SY_VAR) {
            if (!same_type(y->ty, ty)) ERRAT(nmtok, "Type mismatch in redeclaration of '%s'", name);
            if (y->ty->kind == TY_ARRAY && y->ty->len < 0 && ty->len >= 0) y->ty = ty;
            if (y->defined && !y->from_rt && !in_rt && storage != SC_EXTERN && y->bss == 0 && 0) {}
        }
        if (storage == SC_STATIC) y->storage = SC_STATIC;
        else if (kind == SY_VAR && y->storage == SC_EXTERN && storage != SC_EXTERN) y->storage = storage;   /* the definition follows an extern declaration */
        return y;
    }
    y = add_sym(gscope, name, ty, kind);
    y->global = 1; y->storage = storage; y->line = nmtok->line; y->fileid = nmtok->fileid;
    return y;
}

static void parse_local_decl(void)
{
    int st, td, gt;
    Tok *first = peek();
    Type *bt = parse_declspec(&st, &td, &gt);
    if (toks[tp].k == P_SEMI) { tp++; return; }
    (void)first;
    for (;;) {
        char *name = NULL;
        Tok *nm = peek();
        Type *ty = parse_declarator(bt, &name);
        if (!name) ERR("Declaration syntax error");
        nm = &toks[tp > 0 ? tp - 1 : 0];
        if (td) {
            Sym *y = add_sym(scope, name, ty, SY_TYPEDEF); y->used = 1;
        } else if (ty->kind == TY_FUNC) {
            declare_global(nm, name, ty, SY_FUNC, SC_EXTERN);
            Sym *g = find_in(gscope, name);
            if (!find_in(scope, name) && scope != gscope) { /* visible via global scope */ }
            (void)g;
        } else if (st == SC_EXTERN) {
            Sym *g = declare_global(nm, name, ty, SY_VAR, SC_EXTERN);
            (void)g;
        } else {
            if (find_in(scope, name)) ERRAT(nm, "Multiple declaration for '%s'", name);
            if (ty->kind == TY_VOID) ERRAT(nm, "Size of the type is unknown or zero");
            if (st == SC_STATIC) {
                char mn[200]; snprintf(mn, sizeof mn, "%s.%d", name, tp);
                Sym *y = add_sym(scope, name, ty, SY_VAR); (void)mn;
                y->global = 1; y->storage = SC_STATIC;
                Sink sk = {0};
                if (accept(P_ASSIGN)) {
                    init_object(y->ty, 0, &sk);
                    global_define_var(y, &sk);
                } else {
                    if (y->ty->kind == TY_ARRAY && y->ty->len < 0) ERRAT(nm, "Size of the type is unknown or zero");
                    global_define_var(y, NULL);
                }
                y->used = 1;
            } else {
                Sink sk = {0};
                int has_init = 0;
                if (toks[tp].k == P_ASSIGN) {
                    tp++; has_init = 1;
                    if (ty->kind == TY_ARRAY && ty->len < 0) ty = array_of(ty->base, -1);
                    if (!is_agg(ty) && ty->kind != TY_ARRAY && toks[tp].k != P_LBRC) {
                        Node *e = parse_assign();
                        e = coerce(e, ty, 0, NULL);
                        sink_add(&sk, 0, ty, e);
                    } else init_object(ty, 0, &sk);
                }
                if (!ty->complete || (ty->kind == TY_ARRAY && ty->len < 0)) ERRAT(nm, "Size of the type is unknown or zero");
                if (ty->kind == TY_VOID) ERRAT(nm, "Size of the type is unknown or zero");
                Sym *y = new_local(name, ty);
                y->line = nm->line;
                if (has_init) {
                    Node *blk = local_init_nodes(y, &sk);
                    /* hang the initialisation on the current statement list via a pending slot */
                    extern Node *pending_init; 
                    Node **pp = &pending_init; while (*pp) pp = &(*pp)->next;
                    *pp = blk;
                    y->assigned = 1;
                }
            }
        }
        if (!accept(P_COMMA)) break;
    }
    expect(P_SEMI, "Declaration missing ;");
}
Node *pending_init;

static int cond_has_assign(Node *n) { return n && n->k == N_ASSIGN; }

static Node *cond_expr(void)
{
    expect(P_LPAR, "Statement missing (");
    Node *c = parse_expr();
    c = rv(c);
    if (!is_scalar(c->ty)) ERR("Illegal use of structure");
    if (cond_has_assign(c)) WARN(CX->opt.w_assign, "Possibly incorrect assignment");
    expect(P_RPAR, "Statement missing )");
    return c;
}

static int has_side_effect(Node *n)
{
    if (!n) return 0;
    switch (n->k) {
    case N_ASSIGN: case N_ASSIGNOP: case N_CALL: case N_PREINC: case N_POSTINC: case N_PREDEC: case N_POSTDEC: return 1;
    case N_COMMA: return has_side_effect(n->l) || has_side_effect(n->r);
    case N_COND: return has_side_effect(n->c) || has_side_effect(n->l) || has_side_effect(n->r);
    case N_CAST: return n->ty->kind == TY_VOID || has_side_effect(n->l);
    case N_LOGAND: case N_LOGOR: return has_side_effect(n->r) || has_side_effect(n->l);
    case N_DEREF: return 0;
    }
    return 0;
}

static Node *parse_stmt(void)
{
    Tok *t = peek();
    Node *n;
    switch (t->k) {
    case P_LBRC: tp++; return parse_block(1);
    case P_SEMI: tp++; return new_stmt(S_NOP);
    case K_IF: {
        tp++; n = new_stmt(S_IF); n->c = cond_expr();
        n->l = parse_stmt();
        if (accept(K_ELSE)) n->r = parse_stmt();
        return n;
    }
    case K_WHILE: {
        tp++; n = new_stmt(S_WHILE); n->c = cond_expr();
        brk_depth++; cont_depth++; n->l = parse_stmt(); brk_depth--; cont_depth--;
        return n;
    }
    case K_DO: {
        tp++; n = new_stmt(S_DO);
        brk_depth++; cont_depth++; n->l = parse_stmt(); brk_depth--; cont_depth--;
        if (!accept(K_WHILE)) ERR("Do statement must have while");
        n->c = cond_expr();
        expect(P_SEMI, "Statement missing ;");
        return n;
    }
    case K_FOR: {
        tp++; n = new_stmt(S_FOR);
        expect(P_LPAR, "Statement missing (");
        push_scope();
        Node *pre = NULL;
        if (toks[tp].k != P_SEMI) {
            if (is_type_start(peek())) {
                Node *save_pending = pending_init; pending_init = NULL;
                parse_local_decl();
                pre = pending_init; pending_init = save_pending;
                n->d = pre;
                goto after_init;
            }
            Node *e = parse_expr(); Node *s = new_stmt(S_EXPR); s->l = e; n->d = s;
        }
        expect(P_SEMI, "Statement missing ;");
    after_init:
        if (toks[tp].k != P_SEMI) { n->c = rv(parse_expr()); if (!is_scalar(n->c->ty)) ERR("Illegal use of structure"); }
        expect(P_SEMI, "Statement missing ;");
        if (toks[tp].k != P_RPAR) n->r = parse_expr();
        expect(P_RPAR, "Statement missing )");
        brk_depth++; cont_depth++; n->l = parse_stmt(); brk_depth--; cont_depth--;
        pop_scope(toks[tp > 0 ? tp - 1 : 0].line);
        return n;
    }
    case K_RETURN: {
        tp++; n = new_stmt(S_RETURN);
        Type *rt = curfn->ty->ret;
        if (toks[tp].k != P_SEMI) {
            Node *e = parse_expr();
            fn_ret_seen = 1;
            if (rt->kind == TY_VOID) { WARN(CX->opt.w_voidret, "Void functions may not return a value"); n->l = rv(e); }
            else n->l = coerce(e, rt, 0, NULL);
        } else if (rt->kind != TY_VOID) { WARN(CX->opt.w_noreturn, "Function should return a value"); fn_ret_seen = 1; }
        expect(P_SEMI, "Statement missing ;");
        return n;
    }
    case K_BREAK:
        tp++; if (!brk_depth && !cur_switch) ERRAT(t, "Misplaced break");
        expect(P_SEMI, "Statement missing ;"); return new_stmt(S_BREAK);
    case K_CONTINUE:
        tp++; if (!cont_depth) ERRAT(t, "Misplaced continue");
        expect(P_SEMI, "Statement missing ;"); return new_stmt(S_CONTINUE);
    case K_GOTO: {
        tp++; Tok *l = next();
        if (l->k != TK_ID) ERRAT(l, "Identifier expected");
        n = new_stmt(S_GOTO); n->str = l->s;
        LabelRef *r = xalloc(sizeof *r); r->name = l->s; r->line = l->line; r->next = fn_labels; fn_labels = r;
        expect(P_SEMI, "Statement missing ;");
        return n;
    }
    case K_SWITCH: {
        tp++; n = new_stmt(S_SWITCH);
        expect(P_LPAR, "Statement missing (");
        Node *c = promote(parse_expr());
        if (!is_int(c->ty)) ERR("Switch selection expression must be of integral type");
        expect(P_RPAR, "Statement missing )");
        n->c = c;
        Node *saved = cur_switch; int sb = brk_depth;
        cur_switch = n;
        n->l = parse_stmt();
        cur_switch = saved; brk_depth = sb;
        return n;
    }
    case K_CASE: {
        tp++;
        if (!cur_switch) ERRAT(t, "Case outside of switch");
        long long v = const_expr();
        expect(P_COLON, "Case statement missing :");
        n = new_stmt(S_CASE); n->val = norm(v, cur_switch->c->ty);
        for (Node *c = cur_switch->cases; c; c = c->r) if (c->k == S_CASE && c->val == n->val) ERRAT(t, "Duplicate case");
        n->r = cur_switch->cases; cur_switch->cases = n;    /* case chain uses ->r, statement chain ->next */
        n->l = parse_stmt();
        return n;
    }
    case K_DEFAULT: {
        tp++;
        if (!cur_switch) ERRAT(t, "Default outside of switch");
        expect(P_COLON, "Default statement missing :");
        n = new_stmt(S_DEFAULT);
        for (Node *c = cur_switch->cases; c; c = c->r) if (c->k == S_DEFAULT) ERRAT(t, "Too many default cases");
        n->r = cur_switch->cases; cur_switch->cases = n;
        n->l = parse_stmt();
        return n;
    }
    case TK_ID:
        if (toks[tp + 1].k == P_COLON && !is_type_start(t)) {
            tp += 2;
            n = new_stmt(S_LABEL); n->str = t->s;
            for (LabelRef *r = fn_labels; r; r = r->next) if (r->name == t->s && r->defined) ERRAT(t, "Duplicate label '%s'", t->s);
            LabelRef *r = xalloc(sizeof *r); r->name = t->s; r->defined = 1; r->line = t->line; r->next = fn_labels; fn_labels = r;
            n->l = parse_stmt();
            return n;
        }
        break;
    default: break;
    }
    n = new_stmt(S_EXPR);
    n->l = parse_expr();
    if (!has_side_effect(n->l)) WARNAT(CX->opt.w_noeffect, t->line, "Code has no effect");
    expect(P_SEMI, "Statement missing ;");
    return n;
}

static Node *parse_block(int newscope)
{
    Node *blk = new_stmt(S_BLOCK);
    Node **tail = &blk->l;
    if (newscope) push_scope();
    jmp_buf jb, *saved = recover_jb;
    Scope *sc_at = scope; int bd = brk_depth, cd = cont_depth; Node *sw = cur_switch;
    while (toks[tp].k != P_RBRC) {
        if (toks[tp].k == TK_EOF) { ERR("Compound statement missing }"); }
        recover_jb = &jb;
        if (setjmp(jb)) {
            scope = sc_at; brk_depth = bd; cont_depth = cd; cur_switch = sw; pending_init = NULL;
            if (noskip) noskip = 0; else sync_to_stmt_end();
            continue;
        }
        if (is_type_start(peek()) && !(toks[tp].k == TK_ID && toks[tp + 1].k == P_COLON)) {
            pending_init = NULL;
            parse_local_decl();
            for (Node *p = pending_init; p;) { Node *nx = p->next; p->next = NULL; *tail = p; tail = &p->next; p = nx; }
            pending_init = NULL;
        } else {
            Node *s = parse_stmt();
            *tail = s; tail = &s->next;
        }
    }
    recover_jb = saved;
    int endline = toks[tp].line;
    tp++;
    if (newscope) pop_scope(endline);
    blk->val = 0;
    blk->cmpty_uns = endline;
    return blk;
}

static void parse_function_body(Sym *f, ParamList *pl, Tok *nm)
{
    curfn = f; fn_frame = 0; fn_ret_seen = 0; brk_depth = cont_depth = 0; cur_switch = NULL; fn_labels = NULL;
    push_scope(); scope->is_func_top = 1;
    int i = 0, poff = 16;
    Param *pa = f->ty->params;
    if (is_agg(f->ty->ret)) poff += 8;                  /* hidden pointer to the caller's result buffer */
    for (; pa; pa = pa->next, i++) {
        int slot = is_agg(pa->ty) ? (int)((pa->ty->size + 7) & ~7L) : 8;
        if (!pa->name) { if (!in_rt) ERRAT(nm, "Parameter name missing"); poff += slot; continue; }
        Sym *y = add_sym(scope, pa->name, pa->ty, SY_VAR);
        y->is_param = 1; y->off = poff; y->assigned = 1;
        dbg_note_var(y);
        poff += slot;
    }
    (void)pl;
    expect(P_LBRC, "Declaration syntax error");
    /* the function body shares the parameter scope */
    Node *body = parse_block(0);
    int endline = body->cmpty_uns;
    if (f->ty->ret->kind != TY_VOID && !fn_ret_seen && strcmp(f->name, "main") != 0)
        WARNAT(CX->opt.w_noreturn, endline, "Function should return a value");
    else if (f->ty->ret->kind != TY_VOID && !fn_ret_seen && !in_rt)
        WARNAT(CX->opt.w_noreturn, endline, "Function should return a value");
    pop_scope(endline);
    for (LabelRef *r = fn_labels; r; r = r->next) {
        if (r->defined) continue;
        int ok = 0;
        for (LabelRef *q = fn_labels; q; q = q->next) if (q->defined && q->name == r->name) ok = 1;
        if (!ok) fc_error_nj(nm->fileid, r->line, "Undefined label '%s'", r->name);
    }
    f->body = body; f->frame = fn_frame; f->defined = 1;
    f->fileid = nm->fileid;                      /* the module that defines it (a header may have declared it first) */
    if (PG.nfuncs == PG.capfuncs) { PG.capfuncs = PG.capfuncs ? PG.capfuncs * 2 : 64; PG.funcs = realloc(PG.funcs, PG.capfuncs * sizeof(Sym *)); }
    PG.funcs[PG.nfuncs++] = f;
    curfn = NULL;
}

static void parse_external_decl(void)
{
    int st, td, gt;
    Tok *start = peek();
    Type *bt = parse_declspec(&st, &td, &gt);
    if (toks[tp].k == P_SEMI) { tp++; return; }
    if (!gt && !td && toks[tp].k != TK_ID && toks[tp].k != P_STAR && toks[tp].k != P_LPAR) ERRAT(start, "Declaration was expected");
    for (;;) {
        char *name = NULL;
        ParamList saved_pl;
        Type *ty = parse_declarator(bt, &name);
        saved_pl = last_params;
        Tok *nm = &toks[tp > 0 ? tp - 1 : 0];
        if (!name) ERR("Declaration syntax error");
        if (td) {
            Sym *old = find_in(gscope, name);
            if (old && old->hidden && !in_rt) {
                if (old->kind == SY_TYPEDEF && same_type(old->ty, ty)) old->hidden = 0;
                else { old->name = intern("<hidden>", 8); old = NULL; }
            }
            if (old && old->kind == SY_TYPEDEF && same_type(old->ty, ty)) { }
            else if (old) ERRAT(nm, "Multiple declaration for '%s'", name);
            else { Sym *y = add_sym(gscope, name, ty, SY_TYPEDEF); y->used = 1; }
        } else if (ty->kind == TY_FUNC) {
            Sym *f = declare_global(nm, name, ty, SY_FUNC, st == SC_STATIC ? SC_STATIC : SC_EXTERN);
            if (!strcmp(name, "__syscall") || !strcmp(name, "__sqrt") || !strcmp(name, "__setjmp") || !strcmp(name, "__longjmp")) f->is_builtin = 1;
            if (toks[tp].k == P_LBRC || (saved_pl.knr && is_type_start(peek()))) {
                if (f->defined && !(f->from_rt && !in_rt)) ERRAT(nm, "Multiple declaration for '%s'", name);
                if (f->from_rt && !in_rt) { f->defined = 0; f->from_rt = 0; f->nrefs = 0; f->ty = ty; f->reach = 0; PG.nfuncs -= 0; 
                    for (int i = 0; i < PG.nfuncs; i++) if (PG.funcs[i] == f) { PG.funcs[i] = PG.funcs[--PG.nfuncs]; break; } }
                f->ty = same_type(f->ty, ty) && !ty->noproto ? ty : (f->ty->noproto || ty->noproto ? ty : f->ty);
                if (saved_pl.knr) {
                    WARN(1, "This style of function definition is now obsolete");
                    while (toks[tp].k != P_LBRC) {
                        int s2, t2, g2; Type *pb = parse_declspec(&s2, &t2, &g2);
                        if (!g2) ERR("Declaration syntax error");
                        for (;;) {
                            char *pn = NULL; Type *pt = adjust_param(parse_declarator(pb, &pn));
                            for (Param *p = ty->params; p; p = p->next) if (p->name == pn) p->ty = pt;
                            if (!accept(P_COMMA)) break;
                        }
                        expect(P_SEMI, "Declaration missing ;");
                    }
                    ty->noproto = 0;
                    f->ty = ty;
                }
                if (!ty->params && !ty->noproto) { }
                f->from_rt = in_rt;
                parse_function_body(f, &saved_pl, nm);
                return;
            }
            (void)0;
        } else {
            if (ty->kind == TY_VOID) ERRAT(nm, "Size of the type is unknown or zero");
            Sym *y = declare_global(nm, name, ty, SY_VAR, st);
            if (toks[tp].k == P_ASSIGN) {
                tp++;
                if (y->defined && y->from_rt == in_rt && y->ty->kind != TY_ARRAY && !(y->bss)) ERRAT(nm, "Multiple declaration for '%s'", name);
                Sink sk = {0};
                Type *vt = y->ty;
                if (vt->kind == TY_ARRAY && vt->len < 0) { vt = array_of(vt->base, -1); }
                if (!is_agg(vt) && vt->kind != TY_ARRAY && toks[tp].k != P_LBRC) {
                    Node *e = coerce(parse_assign(), vt, 0, NULL);
                    sink_add(&sk, 0, vt, e);
                } else init_object(vt, 0, &sk);
                y->ty = vt;
                y->storage = st == SC_STATIC ? SC_STATIC : SC_NONE;
                global_define_var(y, &sk);
            } else if (st != SC_EXTERN) {
                if (y->ty->kind == TY_ARRAY && y->ty->len < 0) { /* extern-like incomplete */ }
                else global_define_var(y, NULL);
            }
            y->used = 1;
        }
        if (!accept(P_COMMA)) break;
    }
    expect(P_SEMI, "Declaration terminated incorrectly");
}

void parse_end_runtime(void) { gscope->tags = NULL; }

void parse_init(void)
{
    free(PG.funcs); free(PG.data); free(PG.drel);
    memset(&PG, 0, sizeof PG);
    memset(ghash, 0, sizeof ghash);
    ty_void = mkty(TY_VOID, 0, 0); ty_void->align = 1;
    ty_char = mkty(TY_CHAR, 1, 0); ty_uchar = mkty(TY_CHAR, 1, 1);
    ty_short = mkty(TY_SHORT, 2, 0); ty_ushort = mkty(TY_SHORT, 2, 1);
    ty_int = mkty(TY_INT, 4, 0); ty_uint = mkty(TY_INT, 4, 1);
    ty_long = mkty(TY_LONG, 8, 0); ty_ulong = mkty(TY_LONG, 8, 1);
    ty_float = mkty(TY_FLOAT, 4, 0); ty_double = mkty(TY_DOUBLE, 8, 0);
    gscope = xalloc(sizeof(Scope)); scope = gscope;
    curfn = NULL; pending_init = NULL;
    data_alloc(8, 8);   /* keep offset 0 unused so that no object has address zero */
}

int parse_unit(TokVec *tv, int is_runtime)
{
    Tok eof; memset(&eof, 0, sizeof eof); eof.k = TK_EOF;
    if (tv->n) { eof.line = tv->v[tv->n - 1].line; eof.fileid = tv->v[tv->n - 1].fileid; }
    tv_push(tv, eof); tv_push(tv, eof);
    toks = tv->v; tp = 0; in_rt = is_runtime;
    int before = CX->errors;
    jmp_buf jb, *saved = recover_jb;
    while (toks[tp].k != TK_EOF) {
        recover_jb = &jb;
        if (setjmp(jb)) {
            scope = gscope; curfn = NULL; pending_init = NULL; brk_depth = cont_depth = 0; cur_switch = NULL;
            /* resynchronise: skip to the end of this declaration / function body */
            if (noskip) noskip = 0; else { sync_to_stmt_end(); if (toks[tp].k == P_RBRC) tp++; }
            continue;
        }
        if (toks[tp].k == P_RBRC) { ERR("Unexpected }"); }
        parse_external_decl();
    }
    recover_jb = saved;
    return CX->errors != before;
}

/* ---------- expression evaluation for the IDE ---------- */
Node *parse_expr_only(TokVec *tv)
{
    Tok eof; memset(&eof, 0, sizeof eof); eof.k = TK_EOF;
    if (tv->n) { eof.line = tv->v[tv->n - 1].line; eof.fileid = tv->v[tv->n - 1].fileid; }
    tv_push(tv, eof); tv_push(tv, eof);
    toks = tv->v; tp = 0; in_rt = 1; curfn = NULL;
    jmp_buf jb, *saved = recover_jb;
    recover_jb = &jb;
    Node *n = NULL;
    if (!setjmp(jb)) {
        n = parse_expr();
        if (toks[tp].k != TK_EOF) ERR("Expression syntax");
    }
    recover_jb = saved;
    return n;
}
