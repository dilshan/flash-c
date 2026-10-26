/*
 * File:    dbgeval.c
 * Purpose: Flash C - debugger expression evaluator, value formatting, frame
 *          descriptions and registers
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "dbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <math.h>
#include <sys/ptrace.h>
#include <sys/user.h>

int dbg_read_memory(unsigned long addr, void *buf, size_t n);
int dbg_write_memory(unsigned long addr, const void *buf, size_t n);
const FcDFunc *dbg_cur_func(void);
int dbg_cur_line(void);
unsigned long dbg_cur_fp(void);
const FcDebug *dbg_info(void);

typedef struct EV { FcDType *ty; int lv; unsigned long addr; long long i; double f; int bw, bsh, buns; } EV;   /* bw > 0: bit field in the 32-bit unit at addr */

static FcDType t_void = { FCT_VOID, 0, 0, 0 }, t_char = { FCT_CHAR, 1, 0, 0 }, t_uchar = { FCT_CHAR, 1, 1, 0 },
    t_short = { FCT_SHORT, 2, 0, 0 }, t_ushort = { FCT_SHORT, 2, 1, 0 }, t_int = { FCT_INT, 4, 0, 0 }, t_uint = { FCT_INT, 4, 1, 0 },
    t_long = { FCT_LONG, 8, 0, 0 }, t_ulong = { FCT_LONG, 8, 1, 0 }, t_float = { FCT_FLOAT, 4, 0, 0 }, t_double = { FCT_DOUBLE, 8, 0, 0 };
static FcDType pool[64]; static int npool;
static FcDType *ptr_to(FcDType *b)
{
    for (int i = 0; i < npool; i++) if (pool[i].kind == FCT_PTR && pool[i].base == b) return &pool[i];
    if (npool >= 64) npool = 0;
    FcDType *t = &pool[npool++];
    memset(t, 0, sizeof *t); t->kind = FCT_PTR; t->size = 8; t->base = b;
    return t;
}

static jmp_buf evj;
static char everr[160];
static void evfail(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(everr, sizeof everr, fmt, ap); va_end(ap);
    longjmp(evj, 1);
}

static const char *src; static int pos;
static void skip(void) { while (src[pos] == ' ' || src[pos] == '\t') pos++; }
static int accept_op(const char *op)
{
    skip();
    size_t n = strlen(op);
    if (strncmp(src + pos, op, n)) return 0;
    /* do not take "=" of "==", "&" of "&&", "<" of "<<" or "<=" ... */
    char c = src[pos + n];
    if (!strcmp(op, "=") && c == '=') return 0;
    if (!strcmp(op, "&") && (c == '&' || c == '=')) return 0;
    if (!strcmp(op, "|") && (c == '|' || c == '=')) return 0;
    if ((!strcmp(op, "<") || !strcmp(op, ">")) && (c == '=' || c == src[pos])) return 0;
    if ((!strcmp(op, "+") || !strcmp(op, "-")) && c == src[pos]) return 0;
    if (!strcmp(op, "-") && c == '>') return 0;
    if ((!strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%") || !strcmp(op, "^") || !strcmp(op, "+") || !strcmp(op, "-")) && c == '=') return 0;
    if (!strcmp(op, "!") && c == '=') return 0;
    pos += (int)n;
    return 1;
}

static int is_flt(FcDType *t) { return t->kind == FCT_FLOAT || t->kind == FCT_DOUBLE; }
static int is_int(FcDType *t) { return t->kind >= FCT_CHAR && t->kind <= FCT_LONG; }
static int is_ptr(FcDType *t) { return t->kind == FCT_PTR || t->kind == FCT_ARRAY; }
static int is_agg(FcDType *t) { return t->kind == FCT_STRUCT || t->kind == FCT_UNION || t->kind == FCT_ARRAY; }

static void load(EV *v)
{
    if (!v->lv) return;
    if (is_agg(v->ty) || v->ty->kind == FCT_FUNC) return;
    if (v->bw) {
        unsigned u;
        if (dbg_read_memory(v->addr, &u, 4)) evfail("Cannot access memory at 0x%lX", v->addr);
        unsigned long long x = ((unsigned long long)u << (64 - v->bsh - v->bw));
        v->i = v->buns ? (long long)(x >> (64 - v->bw)) : ((long long)x >> (64 - v->bw));
        v->lv = 2; return;
    }
    unsigned char b[16] = { 0 };
    int n = v->ty->size;
    if (n < 1 || n > 8) return;
    if (dbg_read_memory(v->addr, b, n)) evfail("Cannot access memory at 0x%lX", v->addr);
    if (v->ty->kind == FCT_FLOAT) { float f; memcpy(&f, b, 4); v->f = f; v->lv = 2; }
    else if (v->ty->kind == FCT_DOUBLE) { memcpy(&v->f, b, 8); v->lv = 2; }
    else {
        long long x = 0;
        memcpy(&x, b, n);
        if (!v->ty->uns && n < 8) { int sh = 64 - 8 * n; x = (x << sh) >> sh; }
        v->i = x; v->lv = 2;
    }
}
/* lv: 0 rvalue, 1 lvalue not yet loaded, 2 lvalue already loaded (value valid, address valid) */
static void rvalue(EV *v)
{
    if (v->lv == 1) load(v);
    if (v->ty->kind == FCT_ARRAY) { v->ty = ptr_to(v->ty->base); v->i = (long long)v->addr; v->lv = 0; }
    else if (v->ty->kind == FCT_FUNC) { v->ty = ptr_to(v->ty); v->i = (long long)v->addr; v->lv = 0; }
}
static double asd(EV *v) { return is_flt(v->ty) ? v->f : (v->ty->uns ? (double)(unsigned long long)v->i : (double)v->i); }
static long long asi(EV *v) { return is_flt(v->ty) ? (long long)v->f : v->i; }

static void mkint(EV *v, long long x, FcDType *t) { memset(v, 0, sizeof *v); v->ty = t; v->i = x; }
static void mkflt(EV *v, double x, FcDType *t) { memset(v, 0, sizeof *v); v->ty = t; v->f = x; }

/* ---- name lookup */
static int lookup(const char *name, EV *out)
{
    const FcDFunc *f = dbg_cur_func();
    int line = dbg_cur_line();
    if (f) {
        const FcDVar *best = NULL;
        for (int i = 0; i < f->nvars; i++) {
            const FcDVar *v = &f->vars[i];
            if (strcmp(v->name, name)) continue;
            if (!v->is_param && (line < v->line0 || line > v->line1)) continue;
            best = v;                      /* the last declaration that is in scope shadows earlier ones */
        }
        if (best) { memset(out, 0, sizeof *out); out->ty = best->ty; out->lv = 1; out->addr = dbg_cur_fp() + best->off; return 1; }
    }
    const FcDebug *d = dbg_info();
    if (!d) return 0;
    for (int i = 0; i < d->nglobals; i++) if (!strcmp(d->globals[i].name, name)) {
        memset(out, 0, sizeof *out); out->ty = d->globals[i].ty; out->lv = 1; out->addr = d->globals[i].addr; return 1;
    }
    for (int i = 0; i < d->nfuncs; i++) if (!strcmp(d->funcs[i].name, name)) {
        static FcDType ft; ft.kind = FCT_FUNC; ft.size = 1; ft.base = d->funcs[i].ret;
        memset(out, 0, sizeof *out); out->ty = &ft; out->lv = 1; out->addr = d->funcs[i].addr; return 1;
    }
    return 0;
}

/* ---- type names for casts */
static int word_at(const char *w)
{
    skip();
    size_t n = strlen(w);
    return !strncmp(src + pos, w, n) && !isalnum((unsigned char)src[pos + n]) && src[pos + n] != '_';
}
static int type_start(void)
{
    static const char *kw[] = { "unsigned", "signed", "int", "char", "short", "long", "float", "double", "void", "struct", "union", 0 };
    for (int i = 0; kw[i]; i++) if (word_at(kw[i])) return 1;
    return 0;
}
static FcDType *find_tag(const char *tag, int kind)
{
    const FcDebug *d = dbg_info();
    (void)tag; (void)kind; (void)d;
    return NULL;
}
static FcDType *parse_type(void)
{
    int uns = 0, sgn = 0, lng = 0, sht = 0, chr = 0, intk = 0, flt = 0, dbl = 0, vd = 0;
    FcDType *base = NULL;
    for (;;) {
        skip();
        if (word_at("unsigned")) { pos += 8; uns = 1; }
        else if (word_at("signed")) { pos += 6; sgn = 1; }
        else if (word_at("int")) { pos += 3; intk = 1; }
        else if (word_at("char")) { pos += 4; chr = 1; }
        else if (word_at("short")) { pos += 5; sht = 1; }
        else if (word_at("long")) { pos += 4; lng++; }
        else if (word_at("float")) { pos += 5; flt = 1; }
        else if (word_at("double")) { pos += 6; dbl = 1; }
        else if (word_at("void")) { pos += 4; vd = 1; }
        else if (word_at("struct") || word_at("union")) {
            int isu = src[pos] == 'u'; pos += isu ? 5 : 6; skip();
            char tag[64]; int n = 0;
            while ((isalnum((unsigned char)src[pos]) || src[pos] == '_') && n < 63) tag[n++] = src[pos++];
            tag[n] = 0;
            base = find_tag(tag, isu);
            if (!base) evfail("Undefined structure '%s'", tag);
        } else break;
    }
    (void)sgn; (void)intk;
    if (!base) {
        if (vd) base = &t_void;
        else if (dbl) base = &t_double;
        else if (flt) base = &t_float;
        else if (chr) base = uns ? &t_uchar : &t_char;
        else if (sht) base = uns ? &t_ushort : &t_short;
        else if (lng) base = uns ? &t_ulong : &t_long;
        else base = uns ? &t_uint : &t_int;
    }
    while (accept_op("*")) base = ptr_to(base);
    return base;
}

/* ---- conversions */
static void convert(EV *v, FcDType *to)
{
    rvalue(v);
    if (is_flt(to)) { double d = asd(v); mkflt(v, to->kind == FCT_FLOAT ? (double)(float)d : d, to); return; }
    long long x = asi(v);
    int n = to->size;
    if (to->kind == FCT_PTR || n >= 8) { mkint(v, x, to); return; }
    if (n > 0) { int sh = 64 - 8 * n; x = to->uns ? (long long)(((unsigned long long)x << sh) >> sh) : (x << sh) >> sh; }
    mkint(v, x, to);
}

static FcDType *arith_type(FcDType *a, FcDType *b)
{
    if (a->kind == FCT_DOUBLE || b->kind == FCT_DOUBLE) return &t_double;
    if (is_flt(a) || is_flt(b)) return &t_double;
    int ua = a->uns && a->size >= 4, ub = b->uns && b->size >= 4;
    if (a->size == 8 || b->size == 8) return (a->size == 8 && ua) || (b->size == 8 && ub) ? &t_ulong : &t_long;
    return (ua || ub) ? &t_uint : &t_int;
}

static void expr_assign(EV *v);

static void binop(const char *op, EV *a, EV *b)
{
    rvalue(a); rvalue(b);
    int cmp = !strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=");
    if (is_ptr(a->ty) || is_ptr(b->ty)) {
        if (!strcmp(op, "+") || !strcmp(op, "-")) {
            if (is_ptr(a->ty) && is_ptr(b->ty) && op[0] == '-') {
                long sz = a->ty->base && a->ty->base->size > 0 ? a->ty->base->size : 1;
                mkint(a, (a->i - b->i) / sz, &t_long); return;
            }
            if (is_ptr(b->ty) && op[0] == '+') { EV t = *a; *a = *b; *b = t; }
            if (!is_ptr(a->ty) || !is_int(b->ty)) evfail("Illegal pointer arithmetic");
            long sz = a->ty->base && a->ty->base->size > 0 ? a->ty->base->size : 1;
            FcDType *pt = a->ty;
            mkint(a, a->i + (op[0] == '+' ? 1 : -1) * b->i * sz, pt); return;
        }
        if (!cmp) evfail("Illegal use of pointer");
    }
    if (cmp) {
        int r;
        if (is_flt(a->ty) || is_flt(b->ty)) {
            double x = asd(a), y = asd(b);
            r = !strcmp(op, "==") ? x == y : !strcmp(op, "!=") ? x != y : !strcmp(op, "<") ? x < y : !strcmp(op, ">") ? x > y : !strcmp(op, "<=") ? x <= y : x >= y;
        } else {
            FcDType *t = arith_type(a->ty, b->ty);
            long long x = a->i, y = b->i;
            if (t->uns) { unsigned long long ux = (unsigned long long)x, uy = (unsigned long long)y;
                if (t->size == 4) { ux = (unsigned)ux; uy = (unsigned)uy; }
                r = !strcmp(op, "==") ? ux == uy : !strcmp(op, "!=") ? ux != uy : !strcmp(op, "<") ? ux < uy : !strcmp(op, ">") ? ux > uy : !strcmp(op, "<=") ? ux <= uy : ux >= uy; }
            else r = !strcmp(op, "==") ? x == y : !strcmp(op, "!=") ? x != y : !strcmp(op, "<") ? x < y : !strcmp(op, ">") ? x > y : !strcmp(op, "<=") ? x <= y : x >= y;
        }
        mkint(a, r, &t_int); return;
    }
    FcDType *t = arith_type(a->ty, b->ty);
    if (is_flt(t)) {
        double x = asd(a), y = asd(b), r;
        if (!strcmp(op, "+")) r = x + y; else if (!strcmp(op, "-")) r = x - y; else if (!strcmp(op, "*")) r = x * y;
        else if (!strcmp(op, "/")) { if (y == 0) evfail("Divide by zero"); r = x / y; }
        else evfail("Illegal use of floating point");
        mkflt(a, r, t); return;
    }
    long long x = a->i, y = b->i, r = 0;
    if (t->size == 4) { if (t->uns) { x = (unsigned)x; y = (unsigned)y; } else { x = (int)x; y = (int)y; } }
    if (!strcmp(op, "+")) r = x + y; else if (!strcmp(op, "-")) r = x - y; else if (!strcmp(op, "*")) r = x * y;
    else if (!strcmp(op, "/") || !strcmp(op, "%")) {
        if (y == 0) evfail("Divide by zero");
        if (t->uns) r = op[0] == '/' ? (long long)((unsigned long long)x / (unsigned long long)y) : (long long)((unsigned long long)x % (unsigned long long)y);
        else r = op[0] == '/' ? x / y : x % y;
    }
    else if (!strcmp(op, "&")) r = x & y; else if (!strcmp(op, "|")) r = x | y; else if (!strcmp(op, "^")) r = x ^ y;
    else if (!strcmp(op, "<<")) r = x << (y & 63);
    else if (!strcmp(op, ">>")) r = t->uns ? (long long)((unsigned long long)x >> (y & 63)) : x >> (y & 63);
    mkint(a, r, t);
    convert(a, t);
}

/* ---- parser */
static void expr_cond(EV *v);
static void expr_unary(EV *v);

static void number(EV *v)
{
    skip();
    const char *s = src + pos; char *e;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { unsigned long long x = strtoull(s, &e, 16); mkint(v, (long long)x, x > 0x7fffffffULL ? &t_long : &t_int); pos += (int)(e - s); }
    else {
        const char *q = s; int fl = 0;
        while (isdigit((unsigned char)*q)) q++;
        if (*q == '.' || *q == 'e' || *q == 'E') fl = 1;
        if (fl) { double d = strtod(s, &e); mkflt(v, d, &t_double); pos += (int)(e - s); }
        else { unsigned long long x = strtoull(s, &e, s[0] == '0' ? 8 : 10); mkint(v, (long long)x, x > 0x7fffffffULL ? &t_long : &t_int); pos += (int)(e - s); }
    }
    while (src[pos] == 'l' || src[pos] == 'L' || src[pos] == 'u' || src[pos] == 'U' || src[pos] == 'f' || src[pos] == 'F') pos++;
}

static void expr_primary(EV *v)
{
    skip();
    char c = src[pos];
    if (c == '(') {
        pos++;
        expr_assign(v);
        if (!accept_op(")")) evfail("Syntax error: ) expected");
    } else if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)src[pos + 1]))) number(v);
    else if (c == '\'') {
        pos++;
        int ch = (unsigned char)src[pos++];
        if (ch == '\\') { int e = src[pos++]; ch = e == 'n' ? 10 : e == 't' ? 9 : e == 'r' ? 13 : e == '0' ? 0 : e; }
        if (src[pos] != '\'') evfail("Character constant too long");
        pos++;
        mkint(v, ch, &t_int);
    } else if (isalpha((unsigned char)c) || c == '_') {
        char name[80]; int n = 0;
        while ((isalnum((unsigned char)src[pos]) || src[pos] == '_') && n < 79) name[n++] = src[pos++];
        name[n] = 0;
        if (!lookup(name, v)) evfail("Undefined symbol '%s'", name);
    } else evfail("Syntax error in expression");
    for (;;) {
        skip();
        if (src[pos] == '[') {
            pos++;
            EV i; expr_assign(&i);
            if (!accept_op("]")) evfail("Syntax error: ] expected");
            rvalue(&i);
            if (!is_int(i.ty)) evfail("Array index must be an integer");
            if (v->ty->kind == FCT_ARRAY) { FcDType *el = v->ty->base; v->addr += i.i * el->size; v->ty = el; v->lv = 1; }
            else { rvalue(v); if (v->ty->kind != FCT_PTR) evfail("Not an array or pointer"); FcDType *el = v->ty->base; if (!el || el->size <= 0) evfail("Cannot dereference a void pointer"); v->addr = (unsigned long)(v->i + i.i * el->size); v->ty = el; v->lv = 1; }
        } else if (src[pos] == '.' || (src[pos] == '-' && src[pos + 1] == '>')) {
            int arrow = src[pos] == '-';
            pos += arrow ? 2 : 1;
            skip();
            char name[80]; int n = 0;
            while ((isalnum((unsigned char)src[pos]) || src[pos] == '_') && n < 79) name[n++] = src[pos++];
            name[n] = 0;
            if (arrow) { rvalue(v); if (v->ty->kind != FCT_PTR || !v->ty->base) evfail("Structure required on left side of ->"); v->addr = (unsigned long)v->i; v->ty = v->ty->base; v->lv = 1; }
            if (v->ty->kind != FCT_STRUCT && v->ty->kind != FCT_UNION) evfail("Structure required on left side of . or ->");
            int found = 0;
            for (int i = 0; i < v->ty->nmem; i++) if (!strcmp(v->ty->mem[i].name, name)) {
                v->addr += v->ty->mem[i].off; v->bw = v->ty->mem[i].bw; v->bsh = v->ty->mem[i].bsh; v->buns = v->ty->mem[i].buns;
                v->ty = v->ty->mem[i].ty; v->lv = 1; found = 1; break; }
            if (!found) evfail("Undefined structure member '%s'", name);
        } else if (src[pos] == '(') evfail("Function calls are not supported");
        else break;
    }
}

static void expr_unary(EV *v)
{
    skip();
    if (accept_op("-")) { expr_unary(v); rvalue(v); if (is_flt(v->ty)) mkflt(v, -v->f, v->ty); else { FcDType *t = arith_type(v->ty, &t_int); mkint(v, -v->i, t); convert(v, t); } return; }
    if (accept_op("+")) { expr_unary(v); rvalue(v); return; }
    if (accept_op("!")) { expr_unary(v); rvalue(v); mkint(v, is_flt(v->ty) ? v->f == 0 : v->i == 0, &t_int); return; }
    if (accept_op("~")) { expr_unary(v); rvalue(v); if (!is_int(v->ty)) evfail("Illegal use of floating point"); FcDType *t = arith_type(v->ty, &t_int); mkint(v, ~v->i, t); convert(v, t); return; }
    if (accept_op("*")) {
        expr_unary(v); rvalue(v);
        if (v->ty->kind != FCT_PTR || !v->ty->base) evfail("Cannot dereference: not a pointer");
        if (v->ty->base->kind == FCT_VOID) evfail("Cannot dereference a void pointer");
        v->addr = (unsigned long)v->i; v->ty = v->ty->base; v->lv = 1; return;
    }
    if (accept_op("&")) {
        expr_unary(v);
        if (!v->lv) evfail("Cannot take the address of a value");
        if (v->bw) evfail("Illegal to take address of bit field");
        FcDType *pt = ptr_to(v->ty);
        mkint(v, (long long)v->addr, pt); return;
    }
    skip();
    if (word_at("sizeof")) {
        pos += 6; skip();
        int paren = src[pos] == '(';
        if (paren) { pos++; skip(); }
        FcDType *t;
        if (type_start()) t = parse_type(); else { EV x; expr_unary(&x); t = x.ty; }
        if (paren && !accept_op(")")) evfail("Syntax error: ) expected");
        mkint(v, t->kind == FCT_ARRAY ? (long long)t->size : t->size, &t_ulong); return;
    }
    if (src[pos] == '(') {
        int save = pos;
        pos++;
        if (type_start()) {
            FcDType *t = parse_type();
            if (!accept_op(")")) evfail("Syntax error: ) expected");
            expr_unary(v);
            convert(v, t);
            return;
        }
        pos = save;
    }
    expr_primary(v);
}

static void expr_mul(EV *v)
{
    expr_unary(v);
    for (;;) {
        const char *op = accept_op("*") ? "*" : accept_op("/") ? "/" : accept_op("%") ? "%" : NULL;
        if (!op) return;
        EV b; expr_unary(&b); binop(op, v, &b);
    }
}
static void expr_add(EV *v)
{
    expr_mul(v);
    for (;;) {
        const char *op = accept_op("+") ? "+" : accept_op("-") ? "-" : NULL;
        if (!op) return;
        EV b; expr_mul(&b); binop(op, v, &b);
    }
}
static void expr_shift(EV *v)
{
    expr_add(v);
    for (;;) {
        const char *op = accept_op("<<") ? "<<" : accept_op(">>") ? ">>" : NULL;
        if (!op) return;
        EV b; expr_add(&b); binop(op, v, &b);
    }
}
static void expr_rel(EV *v)
{
    expr_shift(v);
    for (;;) {
        const char *op = accept_op("<=") ? "<=" : accept_op(">=") ? ">=" : accept_op("<") ? "<" : accept_op(">") ? ">" : NULL;
        if (!op) return;
        EV b; expr_shift(&b); binop(op, v, &b);
    }
}
static void expr_eq(EV *v)
{
    expr_rel(v);
    for (;;) {
        const char *op = accept_op("==") ? "==" : accept_op("!=") ? "!=" : NULL;
        if (!op) return;
        EV b; expr_rel(&b); binop(op, v, &b);
    }
}
static void expr_band(EV *v) { expr_eq(v); while (accept_op("&")) { EV b; expr_eq(&b); binop("&", v, &b); } }
static void expr_bxor(EV *v) { expr_band(v); while (accept_op("^")) { EV b; expr_band(&b); binop("^", v, &b); } }
static void expr_bor(EV *v) { expr_bxor(v); while (accept_op("|")) { EV b; expr_bxor(&b); binop("|", v, &b); } }
static int truth(EV *v) { rvalue(v); return is_flt(v->ty) ? v->f != 0 : v->i != 0; }
static void expr_land(EV *v) { expr_bor(v); while (accept_op("&&")) { EV b; expr_bor(&b); int r = truth(v) && truth(&b); mkint(v, r, &t_int); } }
static void expr_lor(EV *v) { expr_land(v); while (accept_op("||")) { EV b; expr_land(&b); int r = truth(v) || truth(&b); mkint(v, r, &t_int); } }
static void expr_cond(EV *v)
{
    expr_lor(v);
    if (accept_op("?")) {
        EV a, b; expr_assign(&a);
        if (!accept_op(":")) evfail("Syntax error: : expected");
        expr_cond(&b);
        *v = truth(v) ? a : b;
    }
}

static void store(EV *lhs, EV *rhs)
{
    if (!lhs->lv) evfail("Cannot be modified");
    if (is_agg(lhs->ty) || lhs->ty->kind == FCT_FUNC) evfail("Cannot be modified");
    if (lhs->bw) {
        EV r = *rhs; convert(&r, lhs->ty);
        unsigned u, mask = lhs->bw == 32 ? 0xFFFFFFFFu : ((1u << lhs->bw) - 1);
        if (dbg_read_memory(lhs->addr, &u, 4)) evfail("Cannot access memory at 0x%lX", lhs->addr);
        u = (u & ~(mask << lhs->bsh)) | (((unsigned)r.i & mask) << lhs->bsh);
        if (dbg_write_memory(lhs->addr, &u, 4)) evfail("Cannot access memory at 0x%lX", lhs->addr);
        lhs->lv = 1; load(lhs);
        return;
    }
    EV r = *rhs; convert(&r, lhs->ty);
    unsigned char b[8] = { 0 };
    if (lhs->ty->kind == FCT_FLOAT) { float f = (float)r.f; memcpy(b, &f, 4); }
    else if (lhs->ty->kind == FCT_DOUBLE) memcpy(b, &r.f, 8);
    else memcpy(b, &r.i, lhs->ty->size);
    if (dbg_write_memory(lhs->addr, b, lhs->ty->size)) evfail("Cannot access memory at 0x%lX", lhs->addr);
    *lhs = r;
}

static void expr_assign(EV *v)
{
    expr_cond(v);
    skip();
    if (src[pos] == '=' && src[pos + 1] != '=') {
        pos++;
        EV r; expr_assign(&r);
        EV l = *v; if (l.lv == 2) l.lv = 1;
        rvalue(&r);
        store(&l, &r);
        *v = l;
    }
}

/* ---- formatting */
static char fmtspec;                       /* 0, 'x', 'd', 'c', 's', 'f' */

static void fmt_int(char *o, int n, long long x, FcDType *t)
{
    int hex = fmtspec == 'x' || (dbg_radix == 1 && fmtspec != 'd');
    int both = dbg_radix == 2 && !fmtspec;
    char dec[40], hx[40];
    if (t->uns) snprintf(dec, sizeof dec, "%llu", (unsigned long long)(t->size == 8 ? x : x & ((1ULL << (8 * t->size)) - 1)));
    else snprintf(dec, sizeof dec, "%lld", x);
    snprintf(hx, sizeof hx, "0x%llX", (unsigned long long)(t->size >= 8 ? x : x & ((1ULL << (8 * t->size)) - 1)));
    if (both) snprintf(o, n, "%s (%s)", dec, hx);
    else snprintf(o, n, "%s", hex ? hx : dec);
}

static void cstring(char *o, int n, unsigned long addr, int maxlen, int fixed)
{
    int k = 0;
    o[k++] = '"';
    for (int i = 0; i < maxlen && k < n - 6; i++) {
        unsigned char c;
        if (dbg_read_memory(addr + i, &c, 1)) { k = snprintf(o, n, "<cannot access 0x%lX>", addr); return; }
        if (!c) { if (fixed) break; break; }
        if (c == '\n') { o[k++] = '\\'; o[k++] = 'n'; }
        else if (c == '\t') { o[k++] = '\\'; o[k++] = 't'; }
        else if (c == '"') { o[k++] = '\\'; o[k++] = '"'; }
        else if (c < 32 || c > 126) k += snprintf(o + k, n - k, "\\x%02X", c);
        else o[k++] = c;
    }
    o[k++] = '"'; o[k] = 0;
}

static void format_value(EV *v, char *o, int n, int depth);

static void format_scalar(EV *v, char *o, int n)
{
    load(v);
    FcDType *t = v->ty;
    if (is_flt(t)) { snprintf(o, n, t->kind == FCT_FLOAT ? "%.7g" : "%.15g", v->f); return; }
    if (t->kind == FCT_CHAR && fmtspec != 'x' && fmtspec != 'd') {
        int c = (int)(t->uns ? (unsigned char)v->i : (signed char)v->i);
        char ch[16];
        if (c >= 32 && c < 127) snprintf(ch, sizeof ch, "'%c'", c); else snprintf(ch, sizeof ch, "'\\x%02X'", (unsigned char)c);
        if (fmtspec == 'c') snprintf(o, n, "%s", ch);
        else { char d[40]; fmt_int(d, sizeof d, v->i, t); snprintf(o, n, "%s %s", d, ch); }
        return;
    }
    if (fmtspec == 'c') { snprintf(o, n, "'%c'", (int)(v->i & 0xFF)); return; }
    fmt_int(o, n, v->i, t);
}

static void format_value(EV *v, char *o, int n, int depth)
{
    FcDType *t = v->ty;
    if (t->kind == FCT_FUNC) { snprintf(o, n, "0x%08lX", v->addr); return; }
    if (t->kind == FCT_PTR) {
        EV p = *v; rvalue(&p);
        unsigned long a = (unsigned long)p.i;
        if (!a) { snprintf(o, n, "NULL"); return; }
        if (t->base && t->base->kind == FCT_CHAR && fmtspec != 'x') {
            char s[120]; cstring(s, sizeof s, a, 60, 0);
            snprintf(o, n, "0x%08lX %s", a, s); return;
        }
        snprintf(o, n, "0x%08lX", a); return;
    }
    if (t->kind == FCT_ARRAY) {
        if (t->base->kind == FCT_CHAR && fmtspec != 'x' && fmtspec != 'd') { cstring(o, n, v->addr, t->len > 0 ? t->len : 60, 1); return; }
        int k = snprintf(o, n, "{");
        int cnt = t->len > 0 ? t->len : 0;
        for (int i = 0; i < cnt && k < n - 12; i++) {
            if (i == 16) { k += snprintf(o + k, n - k, ",..."); break; }
            EV e; memset(&e, 0, sizeof e); e.ty = t->base; e.lv = 1; e.addr = v->addr + (unsigned long)i * t->base->size;
            char s[100]; format_value(&e, s, sizeof s, depth + 1);
            k += snprintf(o + k, n - k, "%s%s", i ? "," : "", s);
        }
        snprintf(o + k, n - k, "}");
        return;
    }
    if (t->kind == FCT_STRUCT || t->kind == FCT_UNION) {
        if (depth > 2) { snprintf(o, n, "{...}"); return; }
        int k = snprintf(o, n, "{");
        for (int i = 0; i < t->nmem && k < n - 12; i++) {
            EV e; memset(&e, 0, sizeof e); e.ty = t->mem[i].ty; e.lv = 1; e.addr = v->addr + t->mem[i].off; e.bw = t->mem[i].bw; e.bsh = t->mem[i].bsh; e.buns = t->mem[i].buns;
            char s[100]; format_value(&e, s, sizeof s, depth + 1);
            k += snprintf(o + k, n - k, "%s%s:%s", i ? "," : "", t->mem[i].name, s);
        }
        snprintf(o + k, n - k, "}");
        return;
    }
    if (t->kind == FCT_VOID) { snprintf(o, n, "void"); return; }
    format_scalar(v, o, n);
}

/* split a trailing ",x" style format request from the expression */
static void split_format(char *expr)
{
    fmtspec = 0;
    int depth = 0, last = -1;
    for (int i = 0; expr[i]; i++) {
        if (expr[i] == '(' || expr[i] == '[') depth++;
        else if (expr[i] == ')' || expr[i] == ']') depth--;
        else if (expr[i] == ',' && !depth) last = i;
    }
    if (last < 0) return;
    char *p = expr + last + 1;
    while (*p == ' ') p++;
    if (p[0] && !p[1] && strchr("xXdDcCsSfF", p[0])) { fmtspec = (char)tolower((unsigned char)p[0]); expr[last] = 0; }
}

static int run_eval(const char *text, EV *v)
{
    src = text; pos = 0; npool = 0;
    if (setjmp(evj)) return -1;
    expr_assign(v);
    skip();
    if (src[pos]) evfail("Syntax error in expression");
    return 0;
}

int dbg_eval(const char *expr, char *out, int n)
{
    if (dbg_state() != DS_STOPPED) { snprintf(out, n, "Program not running"); return -1; }
    char e[300]; snprintf(e, sizeof e, "%s", expr);
    split_format(e);
    EV v;
    if (run_eval(e, &v)) { snprintf(out, n, "%s", everr); return -1; }
    if (setjmp(evj)) { snprintf(out, n, "%s", everr); return -1; }
    format_value(&v, out, n, 0);
    return 0;
}

int dbg_eval_cond(const char *expr, long long *res)
{
    EV v;
    if (run_eval(expr, &v)) return -1;
    if (setjmp(evj)) return -1;
    rvalue(&v);
    *res = is_flt(v.ty) ? v.f != 0 : v.i != 0;
    return 0;
}

int dbg_assign(const char *expr, const char *value, char *out, int n)
{
    if (dbg_state() != DS_STOPPED) { snprintf(out, n, "Program not running"); return -1; }
    char e[600]; snprintf(e, sizeof e, "(%s) = (%s)", expr, value);
    EV v;
    if (run_eval(e, &v)) { snprintf(out, n, "%s", everr); return -1; }
    if (setjmp(evj)) { snprintf(out, n, "%s", everr); return -1; }
    format_value(&v, out, n, 0);
    return 0;
}

int dbg_can_modify(const char *expr)
{
    if (dbg_state() != DS_STOPPED) return 0;
    char e[300]; snprintf(e, sizeof e, "%s", expr); split_format(e);
    EV v;
    if (run_eval(e, &v)) return 0;
    return v.lv && !is_agg(v.ty) && v.ty->kind != FCT_FUNC;
}

/* lines for the inspector: one per member / element / the pointed-to value */
int dbg_inspect_lines(const char *expr, char (*lines)[160], char (*exprs)[160], int max, char *title, int tn)
{
    if (dbg_state() != DS_STOPPED) return -1;
    char e[300]; snprintf(e, sizeof e, "%s", expr); split_format(e);
    EV v;
    if (run_eval(e, &v)) { snprintf(title, tn, "%s", everr); return -1; }
    int cnt = 0;
    if (setjmp(evj)) { snprintf(title, tn, "%s", everr); return -1; }
    FcDType *t = v.ty;
    char tb[100];
    const char *kind = t->kind == FCT_STRUCT ? "struct " : t->kind == FCT_UNION ? "union " : "";
    if (t->kind == FCT_ARRAY) snprintf(tb, sizeof tb, "array[%d]", t->len);
    else if (t->kind == FCT_PTR) snprintf(tb, sizeof tb, "pointer");
    else if (t->kind == FCT_STRUCT || t->kind == FCT_UNION) snprintf(tb, sizeof tb, "%s%s", kind, t->tag ? t->tag : "");
    else if (t->kind == FCT_FUNC) snprintf(tb, sizeof tb, "function");
    else snprintf(tb, sizeof tb, "%s%s", t->uns ? "unsigned " : "", t->kind == FCT_CHAR ? "char" : t->kind == FCT_SHORT ? "short" : t->kind == FCT_INT ? "int" : t->kind == FCT_LONG ? "long" : t->kind == FCT_FLOAT ? "float" : "double");
    snprintf(title, tn, "%s (%s)", expr, tb);
    if (t->kind == FCT_STRUCT || t->kind == FCT_UNION) {
        for (int i = 0; i < t->nmem && cnt < max; i++) {
            EV m; memset(&m, 0, sizeof m); m.ty = t->mem[i].ty; m.lv = 1; m.addr = v.addr + t->mem[i].off; m.bw = t->mem[i].bw; m.bsh = t->mem[i].bsh; m.buns = t->mem[i].buns;
            char s[120]; format_value(&m, s, sizeof s, 1);
            snprintf(lines[cnt], 160, "%-14s %s", t->mem[i].name, s);
            snprintf(exprs[cnt], 160, "(%s).%s", e, t->mem[i].name); cnt++;
        }
    } else if (t->kind == FCT_ARRAY) {
        int len = t->len > 0 ? t->len : 0;
        for (int i = 0; i < len && cnt < max; i++) {
            EV m; memset(&m, 0, sizeof m); m.ty = t->base; m.lv = 1; m.addr = v.addr + (unsigned long)i * t->base->size;
            char s[120]; format_value(&m, s, sizeof s, 1);
            snprintf(lines[cnt], 160, "[%d]%*s %s", i, i < 10 ? 10 : i < 100 ? 9 : 8, "", s);
            snprintf(exprs[cnt], 160, "(%s)[%d]", e, i); cnt++;
        }
    } else if (t->kind == FCT_PTR && t->base && t->base->kind != FCT_VOID) {
        char s[120]; format_value(&v, s, sizeof s, 1);
        snprintf(lines[cnt], 160, "%-14s %s", "value", s); snprintf(exprs[cnt], 160, "%s", e); cnt++;
        if ((unsigned long)(v.lv ? (rvalue(&v), v.i) : v.i)) {
            EV m = v; m.lv = 1; m.addr = (unsigned long)v.i; m.ty = t->base;
            char s2[120]; format_value(&m, s2, sizeof s2, 1);
            snprintf(lines[cnt], 160, "%-14s %s", "*", s2); snprintf(exprs[cnt], 160, "*(%s)", e); cnt++;
        }
    } else {
        char s[120]; format_value(&v, s, sizeof s, 0);
        snprintf(lines[cnt], 160, "%-14s %s", "value", s); snprintf(exprs[cnt], 160, "%s", e); cnt++;
    }
    return cnt;
}

/* ---- frames */
int dbg_frame_text(int i, char *out, int n)
{
    const DbgFrame *f = dbg_frame(i);
    if (!f) return -1;
    int save = dbg_selected_frame();
    dbg_select_frame(i);
    int k = snprintf(out, n, "%s(", f->fn->name);
    int first = 1;
    for (int j = 0; j < f->fn->nvars && k < n - 20; j++) {
        const FcDVar *p = &f->fn->vars[j];
        if (!p->is_param) continue;
        char v[100];
        EV e; memset(&e, 0, sizeof e); e.ty = p->ty; e.lv = 1; e.addr = f->fp + p->off;
        npool = 0;
        if (setjmp(evj)) snprintf(v, sizeof v, "?");
        else { fmtspec = 0; format_value(&e, v, sizeof v, 1); }
        k += snprintf(out + k, n - k, "%s%s=%s", first ? "" : ", ", p->name, v);
        first = 0;
    }
    snprintf(out + k, n - k, ")");
    dbg_select_frame(save);
    return 0;
}

/* ---- registers */
static const char *regname[] = { "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "RIP", "FLAGS" };
int dbg_nregs(void) { return 19; }
extern unsigned long dbg_reg_rbp(void);
extern int dbg_get_regs_copy(struct user_regs_struct *r);

int dbg_reg_text(int i, char *out, int n)
{
    struct user_regs_struct r;
    if (dbg_state() != DS_STOPPED || dbg_get_regs_copy(&r)) { snprintf(out, n, " Registers are available only while debugging"); return i == 0 ? 0 : -1; }
    unsigned long v[18] = { r.rax, r.rbx, r.rcx, r.rdx, r.rsi, r.rdi, r.rbp, r.rsp, r.r8, r.r9, r.r10, r.r11, r.r12, r.r13, r.r14, r.r15, r.rip, r.eflags };
    if (i < 18) { snprintf(out, n, " %-5s %016lX", regname[i], v[i]); return 0; }
    if (i == 18) {
        unsigned long f = r.eflags;
        snprintf(out, n, " c=%d z=%d s=%d o=%d p=%d a=%d d=%d i=%d", !!(f & 1), !!(f & 0x40), !!(f & 0x80), !!(f & 0x800), !!(f & 4), !!(f & 0x10), !!(f & 0x400), !!(f & 0x200));
        return 0;
    }
    return -1;
}
