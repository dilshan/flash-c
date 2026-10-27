/*
 * File:    opt.c
 * Purpose: Flash C - machine independent optimizer working on the syntax tree of a function
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 *
 * The optimizer runs on the tree of one function just before its code is generated.  Every
 * transformation can be switched on and off on its own (FCO_* flags in FcOptions):
 *
 *   FCO_FOLD       constant folding (arithmetic, casts, unary operators)
 *   FCO_ALGEBRA    algebraic simplification (x + 0, x * 1, x - x, (x + 1) + 2, ...)
 *   FCO_COMPARE    constant comparison evaluation (x == x, unsigned x >= 0, c == 300 for a char c)
 *   FCO_CONSTPROP  local constant propagation
 *   FCO_COPYPROP   local copy propagation
 *   FCO_DEADSTORE  dead assignment elimination
 *   FCO_UNREACH    unreachable statement elimination (also constant conditions and empty code)
 *   FCO_CASTS      redundant cast elimination
 *   FCO_CANON      expression canonicalization (constants to the right, a fixed operand order)
 *   FCO_CSE        local common subexpression elimination
 *   FCO_STRENGTH   strength reduction (multiplication, division and modulo by constants)
 *   FCO_BOOL       boolean and conditional simplification
 *
 * Data-flow facts (constants and copies) are only kept for local scalar variables whose address is
 * never taken, so no store through a pointer or call can change them.  Facts are dropped at loops
 * (for the variables the loop assigns), and the data-flow passes are skipped altogether for
 * functions with goto/labels, setjmp or (in programs that use it) volatile.  The passes that
 * change which statements exist or which values variables hold are also skipped for functions with
 * debug information, so that stepping and watches show what the source says.
 */
#include "fci.h"
#include <limits.h>

/* ------------------------------------------------------------------ */
/* state of the function being optimized                               */
/* ------------------------------------------------------------------ */
static uint64_t fl;      /* FCO_* passes enabled for this function */
static int jumps;        /* redundant jump elimination enabled (the -O switch) */
static Sym *cur_fn;      /* the function */
static int use_flow;     /* data-flow passes (propagation, dead stores, CSE) are allowed */
static int changed;      /* set whenever a pass changed the tree */
static int has_goto;     /* the function has goto statements or labels */
static int no_cfg;       /* the control-flow graph passes must not run (setjmp) */

/* Returns the representation-relevant size of a scalar type. */
static int same_rep(Type *a, Type *b)
{
    if (is_ptr(a) && is_ptr(b)) {
        return 1;
    }
    if (is_flt(a) || is_flt(b)) {
        return a->kind == b->kind;
    }
    return a->kind == b->kind && a->size == b->size && a->uns == b->uns;
}

/* Returns true if values of type a can be used as values of type b without any conversion
 * (the same registers contents): equal representation, or two integer/pointer types of 8 bytes. */
static int same_value_rep(Type *a, Type *b)
{
    if (same_rep(a, b)) {
        return 1;
    }
    int ia = is_int(a) || is_ptr(a), ib = is_int(b) || is_ptr(b);
    return ia && ib && a->size == 8 && b->size == 8;
}

/* Truncates and extends v to the size and signedness of type t. */
static long long norm_val(long long v, Type *t)
{
    switch (t->size) {
    case 1:
        return t->uns ? (long long)(uint8_t)v : (long long)(int8_t)v;
    case 2:
        return t->uns ? (long long)(uint16_t)v : (long long)(int16_t)v;
    case 4:
        return t->uns ? (long long)(uint32_t)v : (long long)(int32_t)v;
    }
    return v;
}

/* Replaces the contents of n by those of w, keeping n in its list. */
static void rep(Node *n, Node *w)
{
    Node *next = n->next;
    int line = n->line;
    *n = *w;
    n->next = next;
    if (!n->line) {
        n->line = line;
    }
    changed = 1;
}

/* Creates a node of the given kind. */
static Node *new_node(int kind, Type *ty, int line)
{
    Node *n = xalloc(sizeof *n);
    n->kind = kind;
    n->ty = ty;
    n->line = line;
    return n;
}

/* Creates an integer (or pointer) constant. */
static Node *num(long long v, Type *ty, int line)
{
    Node *n = new_node(N_NUM, ty, line);
    n->val = norm_val(v, ty);
    return n;
}

/* Creates a floating-point constant (single precision values are rounded). */
static Node *fnum(double v, Type *ty, int line)
{
    Node *n = new_node(N_FNUM, ty, line);
    n->fval = ty->kind == TY_FLOAT ? (double)(float)v : v;
    return n;
}

/* Creates a binary operation node. */
static Node *bin(int op, Node *l, Node *r, Type *ty, int uns, int line)
{
    Node *n = new_node(N_BIN, ty, line);
    n->op = op;
    n->l = l;
    n->r = r;
    n->cmpty_uns = uns;
    return n;
}

/* Returns a copy of the node itself (its children are shared). */
static Node *copy_node(Node *n)
{
    Node *c = xalloc(sizeof *c);
    *c = *n;
    c->next = NULL;
    return c;
}

/* ------------------------------------------------------------------ */
/* tree inspection                                                     */
/* ------------------------------------------------------------------ */
typedef void (*WalkFn)(Node *n, void *ud);

/* Visits n and everything below it (not n's list successors), parents before children. */
static void walk(Node *n, WalkFn f, void *ud)
{
    if (!n) {
        return;
    }
    f(n, ud);
    Node *kids[4] = {n->l, n->r, n->c, n->d};
    for (int i = 0; i < 4; i++) {
        if (i == 1 && (n->kind == S_CASE || n->kind == S_DEFAULT)) {
            continue; /* the case chain, not a child */
        }
        for (Node *k = kids[i]; k; k = k->next) {
            walk(k, f, ud);
        }
    }
}

/* Visits a statement list. */
static void walk_list(Node *n, WalkFn f, void *ud)
{
    for (; n; n = n->next) {
        walk(n, f, ud);
    }
}

/* Returns true if the type is a scalar that fits one register. */
static int scalar_ty(Type *t)
{
    return is_int(t) || is_ptr(t) || is_flt(t);
}

/* Returns true if the symbol is a local scalar variable whose value only changes by assignments
 * the optimizer can see. */
static int trackable(Sym *s)
{
    return s && s->kind == SY_VAR && !s->global && !s->esc && scalar_ty(s->ty);
}

/* Marks the variable at the root of an lvalue as having its address taken. */
static void mark_escape(Node *n)
{
    while (n && n->kind == N_MEMBER) {
        n = n->l;
    }
    if (n && n->kind == N_VAR && n->sym) {
        n->sym->esc = 1;
    }
}

/* Analysis walker: finds address-taken variables and constructs that disable data-flow. */
static void scan_node(Node *n, void *ud)
{
    (void)ud;
    switch (n->kind) {
    case N_ADDR:
        mark_escape(n->l);
        break;
    case N_CALL:
        if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__setjmp")) {
            use_flow = 0;
            no_cfg = 1;
        }
        break;
    case S_LABEL:
    case S_GOTO:
        has_goto = 1;
        break;
    default:
        break;
    }
}

/* Returns true if evaluating n has no effect: no stores, no calls and (with strict set) it cannot
 * trap by dividing. */
static int effect_free(Node *n, int strict)
{
    if (!n) {
        return 1;
    }
    switch (n->kind) {
    case N_NUM:
    case N_FNUM:
    case N_STR:
    case N_VAR:
        return 1;
    case N_ADDR:
    case N_DEREF:
    case N_MEMBER:
    case N_NOT:
    case N_NEG:
    case N_BITNOT:
        return effect_free(n->l, strict);
    case N_CAST:
        return effect_free(n->l, strict);
    case N_BIN:
        if (strict && (n->op == B_DIV || n->op == B_MOD) && !(n->r->kind == N_NUM && n->r->val)) {
            return 0;
        }
        return effect_free(n->l, strict) && effect_free(n->r, strict);
    case N_LOGAND:
    case N_LOGOR:
    case N_COMMA:
        return effect_free(n->l, strict) && effect_free(n->r, strict);
    case N_COND:
        return effect_free(n->c, strict) && effect_free(n->l, strict) && effect_free(n->r, strict);
    default:
        return 0;
    }
}

/* Returns true if the expression has no effect at all and can be dropped or duplicated. */
static int pure(Node *n)
{
    return effect_free(n, 1);
}

/* Returns true if the two expression trees are the same. */
static int same_tree(Node *a, Node *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b || a->kind != b->kind || a->op != b->op || a->ty->kind != b->ty->kind ||
        a->ty->size != b->ty->size || a->ty->uns != b->ty->uns || a->cmpty_uns != b->cmpty_uns) {
        return 0;
    }
    switch (a->kind) {
    case N_NUM:
        return a->val == b->val;
    case N_FNUM:
        return memcmp(&a->fval, &b->fval, sizeof a->fval) == 0;
    case N_STR:
        return a->val == b->val;
    case N_VAR:
        return a->sym == b->sym;
    case N_MEMBER:
        return a->val == b->val && a->bw == b->bw && a->bsh == b->bsh && same_tree(a->l, b->l);
    default:
        return same_tree(a->l, b->l) && same_tree(a->r, b->r) && same_tree(a->c, b->c);
    }
}

/* Total order of trees used to put the operands of commutative operators in a fixed order. */
static int tree_rank(Node *n)
{
    switch (n->kind) {
    case N_NUM:
    case N_FNUM:
        return 0;
    case N_VAR:
        return 1;
    default:
        return 2;
    }
}

/* Compares two pure operands; negative if a should come first. */
static int tree_cmp(Node *a, Node *b)
{
    int ra = tree_rank(a), rb = tree_rank(b);
    if (ra != rb) {
        return ra - rb;
    }
    if (a->kind == N_VAR && b->kind == N_VAR && a->sym != b->sym) {
        int d = strcmp(a->sym->name ? a->sym->name : "", b->sym->name ? b->sym->name : "");
        return d ? d : (a->sym->off < b->sym->off ? -1 : 1);
    }
    return a->kind - b->kind;
}

/* Returns true if the statement tree contains a label that control can enter at. */
static void label_cb(Node *n, void *ud)
{
    if (n->kind == S_LABEL || n->kind == S_CASE || n->kind == S_DEFAULT) {
        *(int *)ud = 1;
    }
}

static int has_label(Node *s)
{
    int found = 0;
    walk(s, label_cb, &found);
    return found;
}

/* ------------------------------------------------------------------ */
/* data-flow facts about local variables                               */
/* ------------------------------------------------------------------ */
enum { FACT_INT = 1, FACT_FLT, FACT_COPY };

typedef struct Fact {
    Sym *sym;
    int kind;
    long long iv;
    double fv;
    Sym *src;
} Fact;

#define MAXFACT 48

typedef struct Env {
    Fact f[MAXFACT];
    int n;
} Env;

/* Returns the fact about s, or NULL. */
static Fact *env_find(Env *e, Sym *s)
{
    for (int i = 0; i < e->n; i++) {
        if (e->f[i].sym == s) {
            return &e->f[i];
        }
    }
    return NULL;
}

/* Forgets everything known about s (and every copy of it). */
static void env_kill(Env *e, Sym *s)
{
    int w = 0;
    for (int i = 0; i < e->n; i++) {
        if (e->f[i].sym == s || (e->f[i].kind == FACT_COPY && e->f[i].src == s)) {
            continue;
        }
        e->f[w++] = e->f[i];
    }
    e->n = w;
}

/* Records a fact about s, replacing an older one. */
static void env_set(Env *e, Fact f)
{
    env_kill(e, f.sym);
    if (e->n == MAXFACT) {
        memmove(e->f, e->f + 1, (MAXFACT - 1) * sizeof(Fact));
        e->n--;
    }
    e->f[e->n++] = f;
}

/* Keeps only the facts that hold in both environments. */
static void env_join(Env *a, const Env *b)
{
    int w = 0;
    for (int i = 0; i < a->n; i++) {
        for (int k = 0; k < b->n; k++) {
            if (a->f[i].sym == b->f[k].sym && a->f[i].kind == b->f[k].kind &&
                a->f[i].iv == b->f[k].iv && a->f[i].src == b->f[k].src &&
                memcmp(&a->f[i].fv, &b->f[k].fv, sizeof(double)) == 0) {
                a->f[w++] = a->f[i];
                break;
            }
        }
    }
    a->n = w;
}

/* Walker callback: forgets the variables a tree assigns. */
static void kill_cb(Node *n, void *ud)
{
    Env *e = ud;
    switch (n->kind) {
    case N_ASSIGN:
    case N_ASSIGNOP:
    case N_PREINC:
    case N_POSTINC:
    case N_PREDEC:
    case N_POSTDEC:
        if (n->l && n->l->kind == N_VAR) {
            env_kill(e, n->l->sym);
        }
        break;
    default:
        break;
    }
}

/* Forgets the variables assigned anywhere in a tree. */
static void kill_assigned(Env *e, Node *n)
{
    walk(n, kill_cb, e);
}

static void kill_assigned_list(Env *e, Node *n)
{
    walk_list(n, kill_cb, e);
}

/* ------------------------------------------------------------------ */
/* constant arithmetic                                                 */
/* ------------------------------------------------------------------ */

/* Evaluates an integer operation on constants.  Returns 0 if it cannot (or must not) be folded. */
static int fold_int(int op, long long a, long long b, int u, long long *out)
{
    long long v = 0;
    switch (op) {
    case B_ADD:
        v = (long long)((uint64_t)a + (uint64_t)b);
        break;
    case B_SUB:
        v = (long long)((uint64_t)a - (uint64_t)b);
        break;
    case B_MUL:
        v = (long long)((uint64_t)a * (uint64_t)b);
        break;
    case B_DIV:
    case B_MOD:
        if (!b || (!u && a == LLONG_MIN && b == -1)) {
            return 0;
        }
        if (op == B_DIV) {
            v = u ? (long long)((uint64_t)a / (uint64_t)b) : a / b;
        } else {
            v = u ? (long long)((uint64_t)a % (uint64_t)b) : a % b;
        }
        break;
    case B_AND:
        v = a & b;
        break;
    case B_OR:
        v = a | b;
        break;
    case B_XOR:
        v = a ^ b;
        break;
    case B_SHL:
        v = (long long)((uint64_t)a << (b & 63));
        break;
    case B_SHR:
        v = u ? (long long)((uint64_t)a >> (b & 63)) : a >> (b & 63);
        break;
    case B_EQ:
        v = a == b;
        break;
    case B_NE:
        v = a != b;
        break;
    case B_LT:
        v = u ? (uint64_t)a < (uint64_t)b : a < b;
        break;
    case B_LE:
        v = u ? (uint64_t)a <= (uint64_t)b : a <= b;
        break;
    case B_GT:
        v = u ? (uint64_t)a > (uint64_t)b : a > b;
        break;
    case B_GE:
        v = u ? (uint64_t)a >= (uint64_t)b : a >= b;
        break;
    default:
        return 0;
    }
    *out = v;
    return 1;
}

/* Returns the swapped operator for "b op a" (comparison operators only). */
static int flip_cmp(int op)
{
    switch (op) {
    case B_LT:
        return B_GT;
    case B_LE:
        return B_GE;
    case B_GT:
        return B_LT;
    case B_GE:
        return B_LE;
    default:
        return op;
    }
}

/* Returns the operator that is true exactly when op is false. */
static int negate_cmp(int op)
{
    switch (op) {
    case B_EQ:
        return B_NE;
    case B_NE:
        return B_EQ;
    case B_LT:
        return B_GE;
    case B_LE:
        return B_GT;
    case B_GT:
        return B_LE;
    default:
        return B_LT; /* B_GE */
    }
}

/* Returns the base-2 logarithm if v is a power of two, else -1. */
static int log2_exact(long long v)
{
    if (v <= 0 || (v & (v - 1))) {
        return -1;
    }
    int k = 0;
    while (!(v & 1)) {
        v >>= 1;
        k++;
    }
    return k;
}

/* ------------------------------------------------------------------ */
/* expression simplification                                           */
/* ------------------------------------------------------------------ */

/* Returns the range of values an integer expression can have, if it is narrower than 64 bits. */
static int value_range(Node *n, long long *lo, long long *hi)
{
    if (n->kind == N_NUM && is_int(n->ty)) {
        *lo = *hi = n->val;
        return 1;
    }
    if (n->kind == N_CAST && is_int(n->ty) && is_int(n->l->ty) && n->l->ty->size < n->ty->size &&
        (n->ty->size < 8 || !n->ty->uns || !n->l->ty->uns || 1)) {
        long long l2, h2;
        if (value_range(n->l, &l2, &h2) && (l2 >= 0 || !n->ty->uns)) {
            *lo = l2;
            *hi = h2;
            return 1;
        }
    }
    if (n->kind == N_BIN && n->op == B_AND && n->r->kind == N_NUM && n->r->val >= 0 &&
        is_int(n->ty)) {
        *lo = 0;
        *hi = n->r->val;
        return 1;
    }
    if (is_int(n->ty) && n->ty->size < 8) {
        int bits = n->ty->size * 8;
        if (n->ty->uns) {
            *lo = 0;
            *hi = (1LL << bits) - 1;
        } else {
            *lo = -(1LL << (bits - 1));
            *hi = (1LL << (bits - 1)) - 1;
        }
        return 1;
    }
    return 0;
}

/* Replaces a comparison by a constant when the ranges of its operands decide it. */
static void compare_ranges(Node *n)
{
    Node *l = n->l, *r = n->r;
    if (n->op < B_EQ || !(is_int(l->ty) || is_ptr(l->ty))) {
        return;
    }
    if (r->kind != N_NUM) {
        return;
    }
    int u = n->cmpty_uns;
    if (u && r->val == 0) { /* unsigned values are never below zero */
        switch (n->op) {
        case B_GE:
            if (pure(l)) {
                rep(n, num(1, n->ty, n->line));
            }
            return;
        case B_LT:
            if (pure(l)) {
                rep(n, num(0, n->ty, n->line));
            }
            return;
        case B_LE:
            n->op = B_EQ;
            changed = 1;
            return;
        case B_GT:
            n->op = B_NE;
            changed = 1;
            return;
        default:
            return;
        }
    }
    long long lo, hi, c = r->val;
    if (!is_int(l->ty) || !value_range(l, &lo, &hi) || (u && lo < 0) || !pure(l)) {
        return;
    }
    int res = -1;
    switch (n->op) {
    case B_EQ:
        res = c < lo || c > hi ? 0 : -1;
        break;
    case B_NE:
        res = c < lo || c > hi ? 1 : -1;
        break;
    case B_LT:
        res = hi < c ? 1 : lo >= c ? 0 : -1;
        break;
    case B_LE:
        res = hi <= c ? 1 : lo > c ? 0 : -1;
        break;
    case B_GT:
        res = lo > c ? 1 : hi <= c ? 0 : -1;
        break;
    case B_GE:
        res = lo >= c ? 1 : hi < c ? 0 : -1;
        break;
    }
    if (res >= 0) {
        rep(n, num(res, n->ty, n->line));
    }
}

/* Creates the node "x != 0" for use where a 0/1 value is needed. */
static Node *truth(Node *x)
{
    if (x->kind == N_NOT || x->kind == N_LOGAND || x->kind == N_LOGOR ||
        (x->kind == N_BIN && x->op >= B_EQ)) {
        return x; /* already 0 or 1 */
    }
    Type *t = x->ty;
    Node *z = num(0, t, x->line);
    return bin(B_NE, x, z, ty_int, is_ptr(t) ? 1 : t->uns, x->line);
}

/* Simplifies n (an N_BIN whose operands are already simplified). */
static void simplify_bin(Node *n)
{
    int op = n->op;
    Node *l = n->l, *r = n->r;
    int line = n->line;

    if (is_flt(l->ty)) {
        if ((fl & FCO_FOLD) && l->kind == N_FNUM && r->kind == N_FNUM) {
            double a = l->fval, b = r->fval, v = 0;
            int cmp = 1;
            switch (op) {
            case B_ADD:
                v = a + b;
                cmp = 0;
                break;
            case B_SUB:
                v = a - b;
                cmp = 0;
                break;
            case B_MUL:
                v = a * b;
                cmp = 0;
                break;
            case B_DIV:
                if (b == 0) {
                    return;
                }
                v = a / b;
                cmp = 0;
                break;
            case B_EQ:
                v = a == b;
                break;
            case B_NE:
                v = a != b;
                break;
            case B_LT:
                v = a < b;
                break;
            case B_LE:
                v = a <= b;
                break;
            case B_GT:
                v = a > b;
                break;
            case B_GE:
                v = a >= b;
                break;
            default:
                return;
            }
            rep(n, cmp ? num((long long)v, n->ty, line) : fnum(v, n->ty, line));
            return;
        }
        if ((fl & FCO_ALGEBRA) && r->kind == N_FNUM && r->fval == 1.0 &&
            (op == B_MUL || op == B_DIV) && same_rep(l->ty, n->ty)) {
            rep(n, l);
        }
        return;
    }

    /* canonical form: constants on the right, fixed order for the other operands */
    if (fl & FCO_CANON) {
        if (l->kind == N_NUM && r->kind != N_NUM) {
            if (op == B_ADD || op == B_MUL || op == B_AND || op == B_OR || op == B_XOR ||
                op == B_EQ || op == B_NE) {
                n->l = r;
                n->r = l;
                changed = 1;
            } else if (op >= B_LT) {
                n->l = r;
                n->r = l;
                n->op = flip_cmp(op);
                changed = 1;
            }
        } else if ((op == B_ADD || op == B_MUL || op == B_AND || op == B_OR || op == B_XOR ||
                    op == B_EQ || op == B_NE) &&
                   l->kind != N_NUM && r->kind != N_NUM && pure(l) && pure(r) &&
                   tree_cmp(l, r) > 0) {
            n->l = r;
            n->r = l;
            changed = 1;
        }
        l = n->l;
        r = n->r;
        op = n->op;
        if (op == B_SUB && r->kind == N_NUM && is_int(n->ty) && r->val != 0) {
            n->op = B_ADD; /* x - c  ->  x + (-c) */
            n->r = num(-r->val, r->ty, r->line);
            changed = 1;
            r = n->r;
            op = B_ADD;
        }
    }

    /* constant folding */
    if ((fl & FCO_FOLD) && l->kind == N_NUM && r->kind == N_NUM) {
        long long v;
        if (fold_int(op, l->val, r->val, n->cmpty_uns, &v)) {
            rep(n, num(v, n->ty, line));
            return;
        }
    }

    if (fl & FCO_COMPARE) {
        if (op >= B_EQ && l->kind != N_NUM && pure(l) && pure(r) && same_tree(l, r) &&
            !is_flt(l->ty)) {
            rep(n, num(op == B_EQ || op == B_LE || op == B_GE, n->ty, line));
            return;
        }
        if (op >= B_EQ) {
            compare_ranges(n);
            if (n->kind != N_BIN) {
                return;
            }
        }
    }

    if (fl & FCO_ALGEBRA) {
        int keep = same_value_rep(l->ty, n->ty);
        if (r->kind == N_NUM) {
            long long c = r->val;
            int allones = c == norm_val(-1, r->ty);
            switch (op) {
            case B_ADD:
            case B_SUB:
            case B_OR:
            case B_XOR:
            case B_SHL:
            case B_SHR:
                if (c == 0 && keep) {
                    rep(n, l);
                    return;
                }
                break;
            case B_MUL:
                if (c == 1 && keep) {
                    rep(n, l);
                    return;
                }
                if (c == 0 && pure(l)) {
                    rep(n, num(0, n->ty, line));
                    return;
                }
                break;
            case B_DIV:
                if (c == 1 && keep) {
                    rep(n, l);
                    return;
                }
                break;
            case B_MOD:
                if (c == 1 && pure(l)) {
                    rep(n, num(0, n->ty, line));
                    return;
                }
                break;
            case B_AND:
                if (c == 0 && pure(l)) {
                    rep(n, num(0, n->ty, line));
                    return;
                }
                if (allones && keep) {
                    rep(n, l);
                    return;
                }
                break;
            }
            if (op == B_OR && allones && pure(l)) {
                rep(n, num(-1, n->ty, line));
                return;
            }
            /* (x op c1) op c2  ->  x op (c1 op c2) for the associative operators */
            if (l->kind == N_BIN && l->op == op && l->r->kind == N_NUM &&
                (op == B_ADD || op == B_MUL || op == B_AND || op == B_OR || op == B_XOR) &&
                same_rep(l->ty, n->ty) && is_int(n->ty)) {
                long long v;
                if (fold_int(op, l->r->val, c, n->cmpty_uns, &v)) {
                    n->l = l->l;
                    n->r = num(v, n->ty, line);
                    changed = 1;
                    simplify_bin(n);
                    return;
                }
            }
        } else if (l->kind == N_NUM) {
            if (op == B_SUB && l->val == 0 && is_int(n->ty)) { /* 0 - x  ->  -x */
                Node *neg = new_node(N_NEG, n->ty, line);
                neg->l = r;
                rep(n, neg);
                return;
            }
            if ((op == B_SHL || op == B_SHR) && l->val == 0 && pure(r)) {
                rep(n, num(0, n->ty, line));
                return;
            }
        }
        if (pure(l) && pure(r) && same_tree(l, r) && is_int(l->ty)) {
            switch (op) {
            case B_SUB:
            case B_XOR:
                rep(n, num(0, n->ty, line));
                return;
            case B_AND:
            case B_OR:
                if (same_value_rep(l->ty, n->ty)) {
                    rep(n, l);
                    return;
                }
                break;
            }
        }
    }

    if (fl & FCO_STRENGTH) {
        if (r->kind == N_NUM && is_int(n->ty) && same_rep(l->ty, n->ty)) {
            long long c = r->val;
            int k = log2_exact(c);
            int u = n->cmpty_uns;
            Type *it = ty_int;
            if (op == B_MUL && k > 0) {
                n->op = B_SHL;
                n->r = num(k, it, line);
                changed = 1;
                return;
            }
            if (op == B_DIV && u && k > 0) {
                n->op = B_SHR;
                n->r = num(k, it, line);
                changed = 1;
                return;
            }
            if (op == B_MOD && u && k > 0) {
                n->op = B_AND;
                n->r = num(c - 1, n->ty, line);
                changed = 1;
                return;
            }
            if (op == B_DIV && !u && k > 0 && (l->kind == N_VAR) && n->ty->size <= 8) {
                /* x / 2^k  ->  (x + ((x >> 63) & (2^k - 1))) >> k  (rounds toward zero) */
                Type *t = n->ty;
                Node *sign = bin(B_SHR, copy_node(l), num(63, it, line), t, 0, line);
                Node *adj = bin(B_AND, sign, num(c - 1, t, line), t, 0, line);
                Node *sum = bin(B_ADD, l, adj, t, 0, line);
                n->op = B_SHR;
                n->l = sum;
                n->r = num(k, it, line);
                n->cmpty_uns = 0;
                changed = 1;
                return;
            }
            if (op == B_MUL && (l->kind == N_VAR || l->kind == N_NUM) && c > 2) {
                /* x * (2^k + 1)  ->  (x << k) + x    x * (2^k - 1)  ->  (x << k) - x */
                int kp = log2_exact(c - 1), km = log2_exact(c + 1);
                if (kp > 0 || km > 0) {
                    int sh = kp > 0 ? kp : km;
                    Node *s = bin(B_SHL, l, num(sh, it, line), n->ty, 0, line);
                    n->op = kp > 0 ? B_ADD : B_SUB;
                    n->l = s;
                    n->r = copy_node(l);
                    n->cmpty_uns = 0;
                    changed = 1;
                    return;
                }
            }
        }
    }
}

/* Simplifies a cast whose operand is already simplified. */
static void simplify_cast(Node *n, int bc)
{
    Node *l = n->l;
    Type *to = n->ty, *from = l->ty;
    int line = n->line;
    if (to->kind == TY_VOID || !scalar_ty(to) || !scalar_ty(from)) {
        return;
    }
    if (fl & FCO_FOLD) {
        if (l->kind == N_NUM && (is_int(to) || is_ptr(to))) {
            rep(n, num(l->val, to, line));
            return;
        }
        if (l->kind == N_NUM && is_flt(to) && is_int(from)) {
            rep(n, fnum(from->uns && from->size == 8 ? (double)(unsigned long long)l->val
                                                      : (double)l->val,
                        to, line));
            return;
        }
        if (l->kind == N_FNUM && is_flt(to)) {
            rep(n, fnum(l->fval, to, line));
            return;
        }
        if (l->kind == N_FNUM && is_int(to)) {
            double d = l->fval;
            rep(n, num(d >= 9.2e18 ? (long long)(unsigned long long)d : (long long)d, to, line));
            return;
        }
    }
    if (!(fl & FCO_CASTS)) {
        return;
    }
    if (is_flt(to) || is_flt(from)) {
        if (to->kind == from->kind) {
            rep(n, l);
        }
        return;
    }
    if (same_rep(from, to)) { /* same representation: nothing to convert */
        rep(n, l);
        return;
    }
    if (from->size == 8 && to->size == 8) { /* long <-> unsigned long <-> pointer */
        Type *t = to;
        rep(n, l);
        n->ty = t;
        return;
    }
    if (l->kind == N_CAST && scalar_ty(l->ty) && !is_flt(l->ty) && !is_flt(l->l->ty) &&
        to->size <= l->ty->size) { /* (T2)(T1)x  ->  (T2)x when T2 is not wider than T1 */
        n->l = l->l;
        changed = 1;
        simplify_cast(n, bc);
        return;
    }
    if (bc && to->size >= from->size) { /* the truth value does not change when widening */
        rep(n, l);
    }
}

/* Simplifies a logical negation. */
static void simplify_not(Node *n, int bc)
{
    Node *l = n->l;
    if ((fl & FCO_FOLD) && l->kind == N_NUM) {
        rep(n, num(l->val == 0, n->ty, n->line));
        return;
    }
    if ((fl & FCO_FOLD) && l->kind == N_FNUM) {
        rep(n, num(l->fval == 0, n->ty, n->line));
        return;
    }
    if (!(fl & FCO_BOOL)) {
        return;
    }
    if (l->kind == N_NOT && bc) { /* !!x in a condition is just x */
        rep(n, l->l);
        return;
    }
    if (l->kind == N_BIN && l->op >= B_EQ && !is_flt(l->l->ty)) { /* !(a < b)  ->  a >= b */
        l->op = negate_cmp(l->op);
        rep(n, l);
        return;
    }
}

/* Simplifies && and ||. */
static void simplify_logic(Node *n, int bc)
{
    int and = n->kind == N_LOGAND;
    Node *l = n->l, *r = n->r;
    int line = n->line;
    if (is_flt(l->ty) || is_flt(r->ty)) {
        return;
    }
    if ((fl & FCO_FOLD) && l->kind == N_NUM && r->kind == N_NUM) {
        rep(n, num(and? (l->val && r->val) : (l->val || r->val), n->ty, line));
        return;
    }
    if (!(fl & FCO_BOOL)) {
        return;
    }
    if (l->kind == N_NUM) {
        int lt = l->val != 0;
        if (and) {
            rep(n, lt ? (bc ? r : truth(r)) : num(0, n->ty, line));
        } else {
            rep(n, lt ? num(1, n->ty, line) : (bc ? r : truth(r)));
        }
        return;
    }
    if (r->kind == N_NUM) {
        int rt = r->val != 0;
        if (and ? rt : !rt) { /* x && 1  and  x || 0  are just the truth value of x */
            rep(n, bc ? l : truth(l));
        } else { /* x && 0  and  x || 1  evaluate x and give a constant */
            Node *c = new_node(N_COMMA, n->ty, line);
            c->l = l;
            c->r = num(and? 0 : 1, n->ty, line);
            rep(n, c);
        }
    }
}

/* Simplifies the conditional operator. */
static void simplify_cond(Node *n, int bc)
{
    if (!(fl & FCO_BOOL)) {
        return;
    }
    Node *c = n->c, *a = n->l, *b = n->r;
    if (c->kind == N_NUM && !is_agg(n->ty) && same_value_rep(a->ty, n->ty) &&
        same_value_rep(b->ty, n->ty)) {
        rep(n, c->val ? a : b);
        return;
    }
    if (a->kind == N_NUM && b->kind == N_NUM && is_int(n->ty) && !is_flt(c->ty)) {
        if (a->val == 1 && b->val == 0) { /* c ? 1 : 0  ->  c != 0 */
            Node *t = truth(c);
            if (same_value_rep(t->ty, n->ty)) {
                rep(n, bc ? c : t);
            }
            return;
        }
        if (a->val == 0 && b->val == 1) { /* c ? 0 : 1  ->  !c */
            Node *t = new_node(N_NOT, n->ty, n->line);
            t->l = c;
            rep(n, t);
            return;
        }
    }
}

/* Simplifies x == 0 into !x and x != 0 into x in conditions. */
static void simplify_zero_test(Node *n, int bc)
{
    if (!(fl & FCO_BOOL) || n->kind != N_BIN || n->r->kind != N_NUM || n->r->val != 0 ||
        is_flt(n->l->ty) || (n->op != B_EQ && n->op != B_NE)) {
        return;
    }
    if (n->op == B_EQ) {
        Node *t = new_node(N_NOT, n->ty, n->line);
        t->l = n->l;
        rep(n, t);
    } else if (bc) {
        rep(n, n->l);
    }
}

/* ------------------------------------------------------------------ */
/* the main pass: propagation and simplification in evaluation order   */
/* ------------------------------------------------------------------ */
static void ex(Node *n, Env *e, int bc);

/* Handles an expression used as an lvalue: only its address computation is evaluated. */
static void lv(Node *n, Env *e)
{
    switch (n->kind) {
    case N_VAR:
    case N_STR:
        return;
    case N_DEREF:
        ex(n->l, e, 0);
        return;
    case N_MEMBER:
        lv(n->l, e);
        return;
    default:
        ex(n, e, 0);
    }
}

/* Applies what is known about a variable that is read. */
static void var_read(Node *n, Env *e)
{
    Sym *s = n->sym;
    if (!use_flow || !trackable(s)) {
        return;
    }
    Fact *f = env_find(e, s);
    if (!f) {
        return;
    }
    if (f->kind == FACT_INT && (fl & FCO_CONSTPROP) && (is_int(n->ty) || is_ptr(n->ty))) {
        rep(n, num(f->iv, n->ty, n->line));
    } else if (f->kind == FACT_FLT && (fl & FCO_CONSTPROP) && is_flt(n->ty)) {
        rep(n, fnum(f->fv, n->ty, n->line));
    } else if (f->kind == FACT_COPY && (fl & FCO_COPYPROP)) {
        n->sym = f->src;
        changed = 1;
    }
}

/* Records what an assignment to the variable at lhs establishes. */
static void assign_effect(Node *lhs, Node *rhs, Env *e)
{
    if (lhs->kind != N_VAR) {
        return;
    }
    Sym *s = lhs->sym;
    if (!use_flow || !trackable(s)) {
        return;
    }
    env_kill(e, s);
    Fact f = {0};
    f.sym = s;
    if (rhs && rhs->kind == N_NUM && (is_int(s->ty) || is_ptr(s->ty)) &&
        same_value_rep(rhs->ty, s->ty)) {
        f.kind = FACT_INT;
        f.iv = norm_val(rhs->val, s->ty);
    } else if (rhs && rhs->kind == N_FNUM && is_flt(s->ty) && rhs->ty->kind == s->ty->kind) {
        f.kind = FACT_FLT;
        f.fv = rhs->fval;
    } else if (rhs && rhs->kind == N_VAR && trackable(rhs->sym) && rhs->sym != s &&
               same_rep(rhs->ty, s->ty) && same_rep(rhs->sym->ty, s->ty)) {
        f.kind = FACT_COPY;
        f.src = rhs->sym;
    } else {
        return;
    }
    env_set(e, f);
}

/* Forgets the variable an operator such as += or ++ changes. */
static void modify_effect(Node *lhs, Env *e)
{
    if (lhs->kind == N_VAR) {
        env_kill(e, lhs->sym);
    }
}

/* Processes an expression: propagates facts into it, simplifies it and records its effects.
 * bc says that only whether the value is zero matters. */
static void ex(Node *n, Env *e, int bc)
{
    switch (n->kind) {
    case N_NUM:
    case N_FNUM:
    case N_STR:
        return;
    case N_VAR:
        var_read(n, e);
        return;
    case N_ADDR:
        lv(n->l, e);
        if (n->l->kind == N_DEREF && (fl & FCO_ALGEBRA) && same_value_rep(n->l->l->ty, n->ty)) {
            Type *t = n->ty; /* &*p  ->  p */
            rep(n, n->l->l);
            n->ty = t;
        }
        return;
    case N_DEREF:
        ex(n->l, e, 0);
        if (n->l->kind == N_ADDR && (fl & FCO_ALGEBRA) && !is_agg(n->ty) && n->ty->kind != TY_FUNC &&
            n->l->l->ty->kind == n->ty->kind && n->l->l->ty->size == n->ty->size &&
            n->l->l->ty->uns == n->ty->uns && n->l->l->kind == N_VAR) {
            rep(n, n->l->l); /* *&x  ->  x */
        }
        return;
    case N_MEMBER:
        lv(n->l, e);
        return;
    case N_ASSIGN:
        if (is_agg(n->ty)) {
            ex(n->r, e, 0);
            lv(n->l, e);
            return;
        }
        ex(n->r, e, 0);
        lv(n->l, e);
        assign_effect(n->l, n->r, e);
        return;
    case N_ASSIGNOP:
        lv(n->l, e);
        ex(n->r, e, 0);
        modify_effect(n->l, e);
        return;
    case N_PREINC:
    case N_POSTINC:
    case N_PREDEC:
    case N_POSTDEC:
        lv(n->l, e);
        modify_effect(n->l, e);
        return;
    case N_COND: {
        ex(n->c, e, 1);
        Env t = *e, f = *e;
        ex(n->l, &t, 0);
        ex(n->r, &f, 0);
        kill_assigned(e, n->l);
        kill_assigned(e, n->r);
        simplify_cond(n, bc);
        return;
    }
    case N_LOGAND:
    case N_LOGOR: {
        ex(n->l, e, 1);
        Env t = *e;
        ex(n->r, &t, 1);
        kill_assigned(e, n->r);
        simplify_logic(n, bc);
        return;
    }
    case N_COMMA:
        ex(n->l, e, 0);
        ex(n->r, e, bc);
        if ((fl & FCO_ALGEBRA) && effect_free(n->l, 0) && same_value_rep(n->r->ty, n->ty)) {
            rep(n, n->r);
        }
        return;
    case N_NOT:
        ex(n->l, e, 1);
        simplify_not(n, bc);
        return;
    case N_NEG:
        ex(n->l, e, 0);
        if (n->l->kind == N_NUM && (fl & FCO_FOLD)) {
            rep(n, num((long long)(0 - (uint64_t)n->l->val), n->ty, n->line));
        } else if (n->l->kind == N_FNUM && (fl & FCO_FOLD)) {
            rep(n, fnum(-n->l->fval, n->ty, n->line));
        } else if (n->l->kind == N_NEG && (fl & FCO_ALGEBRA) && same_value_rep(n->l->l->ty, n->ty)) {
            rep(n, n->l->l);
        }
        return;
    case N_BITNOT:
        ex(n->l, e, 0);
        if (n->l->kind == N_NUM && (fl & FCO_FOLD)) {
            rep(n, num(~n->l->val, n->ty, n->line));
        } else if (n->l->kind == N_BITNOT && (fl & FCO_ALGEBRA) &&
                   same_value_rep(n->l->l->ty, n->ty)) {
            rep(n, n->l->l);
        }
        return;
    case N_BIN:
        ex(n->l, e, 0);
        ex(n->r, e, 0);
        simplify_bin(n);
        if (n->kind == N_BIN) {
            simplify_zero_test(n, bc);
        }
        return;
    case N_CAST:
        ex(n->l, e, 0);
        simplify_cast(n, bc);
        return;
    case N_CALL: {
        Node *args[256];
        int argc = 0;
        for (Node *a = n->r; a && argc < 256; a = a->next) {
            args[argc++] = a;
        }
        for (int i = argc - 1; i >= 0; i--) { /* arguments are evaluated last to first */
            ex(args[i], e, 0);
        }
        if (n->l) {
            ex(n->l, e, 0);
        }
        return;
    }
    default:
        if (n->l) {
            ex(n->l, e, 0);
        }
        if (n->r) {
            ex(n->r, e, 0);
        }
        return;
    }
}

/* Returns true if control never continues after the statement. */
static int ends_flow(Node *s)
{
    if (!s) {
        return 0;
    }
    switch (s->kind) {
    case S_RETURN:
    case S_BREAK:
    case S_CONTINUE:
    case S_GOTO:
        return 1;
    case S_BLOCK: {
        Node *last = s->l;
        while (last && last->next) {
            last = last->next;
        }
        return ends_flow(last);
    }
    case S_IF:
        return s->r && ends_flow(s->l) && ends_flow(s->r);
    default:
        return 0;
    }
}

static void flow_list(Node *s, Env *e);

/* Processes one statement. */
static void flow_stmt(Node *s, Env *e)
{
    if (!s) {
        return;
    }
    switch (s->kind) {
    case S_EXPR:
        ex(s->l, e, 0);
        return;
    case S_BLOCK:
        flow_list(s->l, e);
        return;
    case S_IF: {
        ex(s->c, e, 1);
        Env a = *e, b = *e;
        flow_stmt(s->l, &a);
        flow_stmt(s->r, &b);
        int ea = ends_flow(s->l), eb = s->r && ends_flow(s->r);
        if (ea && !eb) {
            *e = b;
        } else if (eb && !ea) {
            *e = a;
        } else if (ea && eb) {
            *e = a;
        } else {
            env_join(&a, &b);
            *e = a;
        }
        return;
    }
    case S_WHILE:
    case S_DO:
    case S_FOR: {
        if (s->kind == S_FOR) {
            flow_list(s->d, e);
        }
        kill_assigned(e, s->c);
        kill_assigned(e, s->r);
        if (s->kind == S_FOR) {
            kill_assigned(e, s->l);
        } else {
            kill_assigned(e, s->l);
        }
        Env le = *e;
        if (s->kind == S_DO) {
            flow_stmt(s->l, &le);
            if (s->c) {
                ex(s->c, &le, 1);
            }
        } else {
            if (s->c) {
                ex(s->c, &le, 1);
            }
            flow_stmt(s->l, &le);
            if (s->kind == S_FOR && s->r) {
                ex(s->r, &le, 0);
            }
        }
        return;
    }
    case S_SWITCH: {
        ex(s->c, e, 0);
        kill_assigned(e, s->l);
        Env se = *e;
        flow_stmt(s->l, &se);
        return;
    }
    case S_CASE:
    case S_DEFAULT:
    case S_LABEL:
        flow_stmt(s->l, e);
        return;
    case S_RETURN:
        if (s->l) {
            ex(s->l, e, 0);
        }
        return;
    default:
        return;
    }
}

static void flow_list(Node *s, Env *e)
{
    for (; s; s = s->next) {
        flow_stmt(s, e);
    }
}

/* ------------------------------------------------------------------ */
/* statements: unreachable code, constant conditions, redundant jumps  */
/* ------------------------------------------------------------------ */
static Node *simp_stmt(Node *s);

/* Turns a statement into an empty one. */
static void make_nop(Node *s)
{
    Node *next = s->next;
    int line = s->line;
    memset(s, 0, sizeof *s);
    s->kind = S_NOP;
    s->line = line;
    s->next = next;
    changed = 1;
}

/* Returns true if the statement does nothing. */
static int is_nop(Node *s)
{
    return !s || s->kind == S_NOP;
}

/* Walker callback: finds break and continue statements. */
static void jump_cb(Node *n, void *ud)
{
    if (n->kind == S_BREAK || n->kind == S_CONTINUE) {
        *(int *)ud = 1;
    }
}

/* Simplifies a statement list: drops empty statements and code after a jump. */
static void simp_list(Node **head)
{
    Node **pp = head;
    int dead = 0; /* the previous statement never completes */
    while (*pp) {
        Node *s = *pp;
        if (dead && (fl & FCO_UNREACH)) {
            if (!has_label(s)) {
                *pp = s->next;
                changed = 1;
                continue;
            }
            dead = 0; /* something can jump to a label in this statement */
        }
        simp_stmt(s);
        if (is_nop(s) && (fl & FCO_UNREACH)) {
            *pp = s->next;
            changed = 1;
            continue;
        }
        if (s->kind == S_RETURN || s->kind == S_BREAK || s->kind == S_CONTINUE ||
            s->kind == S_GOTO) {
            dead = 1;
        }
        /* goto L; L:  ->  L: */
        if ((jumps || (fl & FCO_CFS)) && s->kind == S_GOTO && s->next && s->next->kind == S_LABEL &&
            s->next->str == s->str) {
            *pp = s->next;
            changed = 1;
            continue;
        }
        /* if (c) goto L; L:  ->  if (c) ; L: */
        if ((fl & FCO_BRANCH) && s->kind == S_IF && is_nop(s->r) && !is_nop(s->l)) {
            Node *g = s->l;
            while (g && g->kind == S_BLOCK && g->l && !g->l->next) {
                g = g->l;
            }
            Node *nx = s->next;
            while (nx && nx->kind == S_NOP) {
                nx = nx->next;
            }
            if (g && g->kind == S_GOTO && nx && nx->kind == S_LABEL && nx->str == g->str) {
                make_nop(s->l);
                if (effect_free(s->c, 0)) {
                    make_nop(s);
                } else {
                    s->kind = S_EXPR; /* the condition is still evaluated */
                    s->l = s->c;
                    s->c = s->r = NULL;
                }
                changed = 1;
            }
        }
        pp = &s->next;
    }
}

/* Simplifies a statement (in place). */
static Node *simp_stmt(Node *s)
{
    if (!s) {
        return s;
    }
    switch (s->kind) {
    case S_EXPR:
        if ((fl & FCO_UNREACH) && effect_free(s->l, 0)) {
            make_nop(s);
        }
        break;
    case S_BLOCK:
        simp_list(&s->l);
        if (!s->l && (fl & FCO_UNREACH)) {
            make_nop(s);
        }
        break;
    case S_IF:
        simp_stmt(s->l);
        simp_stmt(s->r);
        if (!(fl & FCO_UNREACH)) {
            break;
        }
        if (s->c->kind == N_NUM) {
            Node *keep = s->c->val ? s->l : s->r, *drop = s->c->val ? s->r : s->l;
            if (!has_label(drop)) {
                if (keep && keep->kind != S_CASE && keep->kind != S_DEFAULT) {
                    rep(s, keep);
                } else if (!keep) {
                    make_nop(s);
                }
            }
            break;
        }
        if (is_nop(s->l) && is_nop(s->r)) {
            if (effect_free(s->c, 0)) {
                make_nop(s);
            } else {
                Node *x = copy_node(s);
                x->kind = S_EXPR;
                x->l = s->c;
                x->c = x->r = NULL;
                rep(s, x);
            }
        } else if (is_nop(s->l) && s->r) { /* if (c) ; else X  ->  if (!c) X */
            Node *nt = new_node(N_NOT, ty_int, s->c->line);
            nt->l = s->c;
            s->c = nt;
            s->l = s->r;
            s->r = NULL;
            changed = 1;
        } else if (is_nop(s->r)) {
            s->r = NULL;
        }
        break;
    case S_WHILE:
        simp_stmt(s->l);
        if ((fl & FCO_UNREACH) && s->c->kind == N_NUM && !s->c->val && !has_label(s->l)) {
            make_nop(s);
        }
        break;
    case S_DO: {
        simp_stmt(s->l);
        int jumps_inside = 0;
        walk(s->l, jump_cb, &jumps_inside);
        if ((fl & FCO_UNREACH) && s->c->kind == N_NUM && !s->c->val && !jumps_inside &&
            !has_label(s->l)) {
            if (s->l && s->l->kind != S_CASE && s->l->kind != S_DEFAULT) {
                rep(s, s->l); /* do { X } while (0)  ->  X */
            } else if (!s->l) {
                make_nop(s);
            }
        }
        break;
    }
    case S_FOR:
        simp_list(&s->d);
        simp_stmt(s->l);
        if ((fl & FCO_UNREACH) && s->c && s->c->kind == N_NUM && !s->c->val && !has_label(s->l)) {
            if (s->d) { /* only the initialisation runs */
                Node *b = copy_node(s);
                b->kind = S_BLOCK;
                b->l = s->d;
                b->c = b->r = b->d = NULL;
                rep(s, b);
            } else {
                make_nop(s);
            }
        }
        break;
    case S_SWITCH:
        simp_stmt(s->l);
        break;
    case S_CASE:
    case S_DEFAULT:
    case S_LABEL:
        simp_stmt(s->l);
        break;
    default:
        break;
    }
    /* a continue as the last statement of a loop body is redundant */
    if (jumps && (s->kind == S_WHILE || s->kind == S_DO || s->kind == S_FOR) && s->l &&
        s->l->kind == S_BLOCK) {
        Node **pp = &s->l->l;
        while (*pp && (*pp)->next) {
            pp = &(*pp)->next;
        }
        if (*pp && (*pp)->kind == S_CONTINUE) {
            *pp = NULL;
            changed = 1;
        }
    }
    return s;
}


/* ------------------------------------------------------------------ */
/* dead assignment elimination                                         */
/* ------------------------------------------------------------------ */
static Sym **rd_syms; /* variables read somewhere, with their read counts */
static int *rd_cnt, nrd, caprd;

/* Counts one read of a variable. */
static void rd_add(Sym *s)
{
    for (int i = 0; i < nrd; i++) {
        if (rd_syms[i] == s) {
            rd_cnt[i]++;
            return;
        }
    }
    if (nrd == caprd) {
        caprd = caprd ? caprd * 2 : 64;
        rd_syms = realloc(rd_syms, caprd * sizeof *rd_syms);
        rd_cnt = realloc(rd_cnt, caprd * sizeof *rd_cnt);
    }
    rd_syms[nrd] = s;
    rd_cnt[nrd++] = 1;
}

/* Returns how often a variable is read in the function. */
static int rd_get(Sym *s)
{
    for (int i = 0; i < nrd; i++) {
        if (rd_syms[i] == s) {
            return rd_cnt[i];
        }
    }
    return 0;
}

/* Counts the reads of variables below n; the target of a plain assignment is not a read. */
static void count_reads(Node *n)
{
    if (!n) {
        return;
    }
    if (n->kind == N_VAR) {
        rd_add(n->sym);
        return;
    }
    if (n->kind == N_ASSIGN && n->l->kind == N_VAR) {
        count_reads(n->r);
        return;
    }
    Node *kids[4] = {n->l, n->r, n->c, n->d};
    for (int i = 0; i < 4; i++) {
        if (i == 1 && (n->kind == S_CASE || n->kind == S_DEFAULT)) {
            continue;
        }
        for (Node *k = kids[i]; k; k = k->next) {
            count_reads(k);
        }
    }
}

/* Replaces an assignment to a variable that is never read by its right side. */
static void drop_unread_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind == N_ASSIGN && n->l->kind == N_VAR && trackable(n->l->sym) &&
        rd_get(n->l->sym) == 0 && same_value_rep(n->r->ty, n->ty)) {
        Type *t = n->ty;
        rep(n, n->r);
        n->ty = t;
    }
}

/* Walker callback: sets the flag when the variable in ud[0] occurs. */
static void mention_cb(Node *n, void *ud)
{
    Sym **p = ud;
    if (n->kind == N_VAR && n->sym == p[0]) {
        p[1] = (Sym *)1;
    }
}

/* Returns true if the tree mentions the variable. */
static int mentions(Node *n, Sym *s)
{
    Sym *p[2] = {s, NULL};
    walk(n, mention_cb, p);
    return p[1] != NULL;
}

/* Walker callback: finds statements that move control somewhere else. */
static void jump_or_label_cb(Node *n, void *ud)
{
    if (n->kind == S_GOTO || n->kind == S_BREAK || n->kind == S_CONTINUE || n->kind == S_LABEL ||
        n->kind == S_CASE || n->kind == S_DEFAULT) {
        *(int *)ud = 1;
    }
}

/* Returns true if the value stored into s by statement st is never used: the variable is
 * overwritten, or the function ends, before anything reads it.  top says that the statement list
 * is the body of the function itself. */
static int overwritten(Node *st, Sym *s, int top)
{
    for (Node *t = st->next; t; t = t->next) {
        if (t->kind == S_EXPR) {
            Node *x = t->l;
            if (x->kind == N_ASSIGN && x->l->kind == N_VAR && x->l->sym == s) {
                return !mentions(x->r, s);
            }
            if (mentions(x, s)) {
                return 0;
            }
            continue;
        }
        if (t->kind == S_RETURN) {
            return !mentions(t->l, s);
        }
        int jmp = 0;
        walk(t, jump_or_label_cb, &jmp);
        if (jmp || mentions(t, s)) {
            return 0;
        }
    }
    return top;
}

static void dead_list(Node *head, int top);

/* Looks for dead stores in the statement lists nested in a statement. */
static void dead_nested(Node *s)
{
    if (!s) {
        return;
    }
    switch (s->kind) {
    case S_BLOCK:
        dead_list(s->l, 0);
        break;
    case S_IF:
        dead_nested(s->l);
        dead_nested(s->r);
        break;
    case S_WHILE:
    case S_DO:
        dead_nested(s->l);
        break;
    case S_FOR:
        dead_list(s->d, 0);
        dead_nested(s->l);
        break;
    case S_SWITCH:
    case S_CASE:
    case S_DEFAULT:
    case S_LABEL:
        dead_nested(s->l);
        break;
    default:
        break;
    }
}

/* Removes stores in a statement list whose value is never used. */
static void dead_list(Node *head, int top)
{
    for (Node *s = head; s; s = s->next) {
        if (s->kind == S_EXPR && s->l->kind == N_ASSIGN && s->l->l->kind == N_VAR &&
            trackable(s->l->l->sym) && same_value_rep(s->l->r->ty, s->l->ty) &&
            overwritten(s, s->l->l->sym, top)) {
            Type *t = s->l->ty;
            rep(s->l, s->l->r);
            s->l->ty = t;
            continue;
        }
        dead_nested(s);
    }
}

/* ------------------------------------------------------------------ */
/* local common subexpression elimination                              */
/* ------------------------------------------------------------------ */
enum { MAXCAND = 160 };

typedef struct Cands {
    Node *n[MAXCAND];
    int dead[MAXCAND];
    int size[MAXCAND];
    int count;
} Cands;

/* Returns the number of nodes of a tree. */
static int tree_size(Node *n)
{
    return n ? 1 + tree_size(n->l) + tree_size(n->r) + tree_size(n->c) : 0;
}

/* Returns true for value nodes worth keeping in a temporary. */
static int cse_candidate(Node *n)
{
    if (!scalar_ty(n->ty)) {
        return 0;
    }
    switch (n->kind) {
    case N_BIN:
    case N_NEG:
    case N_BITNOT:
    case N_NOT:
    case N_DEREF:
        return 1;
    case N_MEMBER:
        return n->l->kind != N_VAR;
    default:
        return 0;
    }
}

static void collect_value(Node *n, Cands *c);

/* Collects from an lvalue: its own node is not a value, only its address computation is. */
static void collect_lvalue(Node *n, Cands *c)
{
    if (!n) {
        return;
    }
    switch (n->kind) {
    case N_DEREF:
        collect_value(n->l, c);
        break;
    case N_MEMBER:
        collect_lvalue(n->l, c);
        break;
    case N_VAR:
    case N_STR:
        break;
    default:
        collect_value(n, c);
    }
}

/* Collects the candidates among the nodes that are always evaluated, in evaluation order. */
static void collect_value(Node *n, Cands *c)
{
    if (!n) {
        return;
    }
    switch (n->kind) {
    case N_NUM:
    case N_FNUM:
    case N_STR:
    case N_VAR:
        return;
    case N_ADDR:
        collect_lvalue(n->l, c);
        return;
    case N_MEMBER:
        collect_lvalue(n->l, c);
        break;
    case N_LOGAND:
    case N_LOGOR:
        collect_value(n->l, c);
        return;
    case N_COND:
        collect_value(n->c, c);
        return;
    default:
        collect_value(n->l, c);
        collect_value(n->r, c);
        break;
    }
    if (cse_candidate(n) && c->count < MAXCAND) {
        c->size[c->count] = tree_size(n);
        c->n[c->count++] = n;
    }
}

/* Returns true if sub is root or lies below it. */
static int inside(Node *root, Node *sub)
{
    if (!root) {
        return 0;
    }
    if (root == sub) {
        return 1;
    }
    return inside(root->l, sub) || inside(root->r, sub) || inside(root->c, sub);
}

/* Allocates a temporary variable in the frame of the function. */
static Node *new_temp(Type *ty, int line)
{
    Sym *t = xalloc(sizeof *t);
    t->name = "$t";
    t->ty = ty;
    t->kind = SY_VAR;
    t->used = t->assigned = 1;
    cur_fn->frame = (cur_fn->frame + 8 + 7) & ~7;
    t->off = -cur_fn->frame;
    Node *v = new_node(N_VAR, ty, line);
    v->sym = t;
    return v;
}

/* Replaces the repeated subexpressions of an effect-free tree by a temporary computed once. */
static void cse_tree(Node *root)
{
    if (!root || !effect_free(root, 0)) {
        return;
    }
    Cands c;
    memset(&c, 0, sizeof c);
    collect_value(root, &c);
    for (int round = 0; round < c.count; round++) {
        int best = -1; /* the largest candidate not yet handled */
        for (int i = 0; i < c.count; i++) {
            if (!c.dead[i] && (best < 0 || c.size[i] > c.size[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        c.dead[best] = 2; /* handled */
        int group[MAXCAND], ng = 0;
        for (int k = 0; k < c.count; k++) {
            if (!c.dead[k] || k == best) {
                if (same_tree(c.n[best], c.n[k]) && !(c.dead[k] == 1)) {
                    group[ng++] = k;
                }
            }
        }
        if (ng < 2) {
            continue;
        }
        int first = group[0];
        for (int g = 1; g < ng; g++) {
            if (group[g] < first) {
                first = group[g];
            }
        }
        Node *orig = c.n[first];
        Node *t = new_temp(orig->ty, orig->line);
        Node *saved = copy_node(orig);
        Node *asg = new_node(N_ASSIGN, orig->ty, orig->line);
        asg->l = copy_node(t);
        asg->r = saved;
        Node *comma = new_node(N_COMMA, orig->ty, orig->line);
        comma->l = asg;
        comma->r = copy_node(t);
        for (int g = 0; g < ng; g++) {
            int k = group[g];
            if (k == first) {
                continue;
            }
            Node *later = c.n[k];
            for (int m = 0; m < c.count; m++) { /* the replaced subtree is no longer evaluated */
                if (m != k && inside(later, c.n[m])) {
                    c.dead[m] = 1;
                }
            }
            c.dead[k] = 1;
            rep(later, copy_node(t));
        }
        c.dead[first] = 1;
        rep(orig, comma);
    }
}

/* Applies the CSE to the expressions of a statement tree. */
static void cse_cb(Node *n, void *ud)
{
    (void)ud;
    switch (n->kind) {
    case S_EXPR:
        if (n->l->kind == N_ASSIGN && scalar_ty(n->l->ty)) {
            cse_tree(n->l->r);
        } else {
            cse_tree(n->l);
        }
        break;
    case S_RETURN:
        cse_tree(n->l);
        break;
    case S_IF:
    case S_WHILE:
    case S_DO:
    case S_SWITCH:
        cse_tree(n->c);
        break;
    case S_FOR:
        cse_tree(n->c);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* control-flow graph: basic blocks, global data flow                  */
/* ------------------------------------------------------------------ */

/* Returns a copy of an expression tree (not of the nodes after it in a list). */
static Node *clone_chain(Node *n);

static Node *clone_expr(Node *n)
{
    if (!n) {
        return NULL;
    }
    Node *c = copy_node(n);
    c->l = clone_chain(n->l);
    c->r = clone_chain(n->r);
    c->c = clone_chain(n->c);
    return c;
}

/* Returns a copy of an expression list (call arguments are chained through next). */
static Node *clone_chain(Node *n)
{
    Node *head = NULL, **tail = &head;
    for (; n; n = n->next) {
        *tail = clone_expr(n);
        tail = &(*tail)->next;
    }
    return head;
}

/* What a basic block executes, in order.  The expression of an atom is *slot. */
enum {
    A_EXPR,   /* expression statement (can be removed as a whole) */
    A_VALUE,  /* expression that is part of a statement, such as the step of a for loop */
    A_COND,   /* condition of if/while/do/for; the block ends in a two-way branch */
    A_SWITCH, /* controlling expression of a switch */
    A_RET,    /* return with a value */
    A_JUMP    /* goto, break, continue, return without value */
};

/* A set of the expressions of the global common subexpression analysis. */
typedef struct ESet {
    uint64_t w[2];
} ESet;

typedef struct Atom {
    int kind;
    Node *stmt;
    Node **slot;
    ESet gen, kill; /* expressions this atom makes available / invalidates */
    int allkill;    /* assigns so many variables that every expression is invalidated */
} Atom;

enum { T_FALL, T_JUMP, T_BRANCH, T_RET, T_SWITCH };

/* A basic block: straight-line code that is entered only at its start. */
typedef struct Block {
    Atom *atoms;
    int natoms, capatoms;
    int term;               /* how the block ends (T_*) */
    int succ[2], nsucc;     /* successors; for a branch: [0] condition true, [1] false */
    int *multi, nmulti, capmulti; /* successors of a switch */
    Node **labels;          /* goto labels placed at the start of the block */
    int nlabels, caplabels;
    int vis;                /* reached by the data-flow analysis */
    Node *loop;             /* header of the loop statement (while, do, for), or NULL */
    Env in;                 /* what is known at the start of the block */
} Block;

static Block *bl;
static int nbl, capbl;
static int cur;           /* block being built */
static int cfg_ok;        /* the graph could be built */
static struct LabelMap {
    char *name;
    int blk;
} *lmap;
static int nlmap, caplmap;
enum { MAXNEST = 64 };
static int brk_stk[MAXNEST], cont_stk[MAXNEST], sw_stk[MAXNEST], sw_def[MAXNEST];
static int nbrk, ncont, nsw;

/* Appends a new empty block and returns its number. */
static int new_block(void)
{
    if (nbl == capbl) {
        capbl = capbl ? capbl * 2 : 64;
        bl = realloc(bl, capbl * sizeof *bl);
    }
    memset(&bl[nbl], 0, sizeof bl[nbl]);
    return nbl++;
}

/* Releases the graph. */
static void cfg_free(void)
{
    for (int i = 0; i < nbl; i++) {
        free(bl[i].atoms);
        free(bl[i].multi);
        free(bl[i].labels);
    }
    free(bl);
    free(lmap);
    bl = NULL;
    lmap = NULL;
    nbl = capbl = nlmap = caplmap = 0;
}

/* Adds an atom to the current block. */
static void add_atom(int kind, Node *stmt, Node **slot)
{
    Block *b = &bl[cur];
    if (b->natoms == b->capatoms) {
        b->capatoms = b->capatoms ? b->capatoms * 2 : 4;
        b->atoms = realloc(b->atoms, b->capatoms * sizeof *b->atoms);
    }
    b->atoms[b->natoms].kind = kind;
    b->atoms[b->natoms].stmt = stmt;
    b->atoms[b->natoms].slot = slot;
    b->natoms++;
}

/* Adds the edge from -> to. */
static void add_edge(int from, int to)
{
    Block *b = &bl[from];
    if (b->term == T_SWITCH) {
        if (b->nmulti == b->capmulti) {
            b->capmulti = b->capmulti ? b->capmulti * 2 : 4;
            b->multi = realloc(b->multi, b->capmulti * sizeof *b->multi);
        }
        b->multi[b->nmulti++] = to;
    } else if (b->nsucc < 2) {
        b->succ[b->nsucc++] = to;
    }
}

/* Lets the current block run on into block to, unless it already ends in a jump. */
static void fall_to(int to)
{
    if (bl[cur].term == T_FALL && bl[cur].nsucc == 0) {
        add_edge(cur, to);
    }
}

/* Ends the current block with an unconditional jump and starts an unreachable one. */
static void jump_block(int to)
{
    bl[cur].term = T_JUMP;
    add_edge(cur, to);
    cur = new_block();
}

/* Returns the block of a goto label, creating it on first use. */
static int label_block(char *name)
{
    for (int i = 0; i < nlmap; i++) {
        if (lmap[i].name == name) {
            return lmap[i].blk;
        }
    }
    if (nlmap == caplmap) {
        caplmap = caplmap ? caplmap * 2 : 8;
        lmap = realloc(lmap, caplmap * sizeof *lmap);
    }
    lmap[nlmap].name = name;
    lmap[nlmap].blk = new_block();
    return lmap[nlmap++].blk;
}

/* Ends the current block with a two-way branch. */
static void branch_block(int on_true, int on_false)
{
    bl[cur].term = T_BRANCH;
    add_edge(cur, on_true);
    add_edge(cur, on_false);
}

static void build_stmt(Node *s);

static void build_list(Node *s)
{
    for (; s; s = s->next) {
        build_stmt(s);
    }
}

/* Appends the blocks of a statement to the graph. */
static void build_stmt(Node *s)
{
    if (!s || !cfg_ok) {
        return;
    }
    switch (s->kind) {
    case S_EXPR:
        if (s->l) {
            add_atom(A_EXPR, s, &s->l);
        }
        return;
    case S_BLOCK:
        build_list(s->l);
        return;
    case S_IF: {
        add_atom(A_COND, s, &s->c);
        int head = cur, tb = new_block(), fb = new_block(), join = new_block();
        branch_block(tb, fb);
        cur = tb;
        build_stmt(s->l);
        fall_to(join);
        cur = fb;
        build_stmt(s->r);
        fall_to(join);
        cur = join;
        (void)head;
        return;
    }
    case S_WHILE:
    case S_DO:
    case S_FOR: {
        if (nbrk >= MAXNEST || ncont >= MAXNEST) {
            cfg_ok = 0;
            return;
        }
        if (s->kind == S_FOR) {
            build_list(s->d);
        }
        int body = new_block(), exit = new_block(), head = new_block();
        int step = s->kind == S_FOR ? new_block() : head; /* where continue goes */
        if (s->kind == S_DO) {
            fall_to(body);
            step = head; /* the test comes after the body */
        } else {
            fall_to(head);
        }
        if (s->kind != S_DO) {
            cur = head;
            if (s->c) {
                add_atom(A_COND, s, &s->c);
                branch_block(body, exit);
            } else {
                bl[cur].term = T_JUMP;
                add_edge(cur, body);
            }
        }
        brk_stk[nbrk++] = exit;
        cont_stk[ncont++] = step;
        bl[s->kind == S_DO ? body : head].loop = s;
        cur = body;
        build_stmt(s->l);
        if (s->kind == S_FOR) {
            fall_to(step);
            cur = step;
            if (s->r) {
                add_atom(A_VALUE, s, &s->r);
            }
            fall_to(head);
        } else if (s->kind == S_DO) {
            fall_to(head);
            cur = head;
            add_atom(A_COND, s, &s->c);
            branch_block(body, exit);
        } else {
            fall_to(head);
        }
        nbrk--;
        ncont--;
        cur = exit;
        return;
    }
    case S_SWITCH: {
        if (nbrk >= MAXNEST || nsw >= MAXNEST) {
            cfg_ok = 0;
            return;
        }
        add_atom(A_SWITCH, s, &s->c);
        int head = cur, exit = new_block();
        bl[head].term = T_SWITCH;
        sw_stk[nsw] = head;
        sw_def[nsw++] = 0;
        brk_stk[nbrk++] = exit;
        cur = new_block(); /* nothing falls into the first case from the test */
        build_stmt(s->l);
        fall_to(exit);
        nbrk--;
        nsw--;
        if (!sw_def[nsw]) {
            add_edge(head, exit);
        }
        cur = exit;
        return;
    }
    case S_CASE:
    case S_DEFAULT: {
        if (nsw == 0) {
            cfg_ok = 0;
            return;
        }
        int cb = new_block();
        fall_to(cb);
        add_edge(sw_stk[nsw - 1], cb);
        if (s->kind == S_DEFAULT) {
            sw_def[nsw - 1] = 1;
        }
        cur = cb;
        build_stmt(s->l);
        return;
    }
    case S_BREAK:
        if (nbrk == 0) {
            cfg_ok = 0;
            return;
        }
        add_atom(A_JUMP, s, NULL);
        jump_block(brk_stk[nbrk - 1]);
        return;
    case S_CONTINUE:
        if (ncont == 0) {
            cfg_ok = 0;
            return;
        }
        add_atom(A_JUMP, s, NULL);
        jump_block(cont_stk[ncont - 1]);
        return;
    case S_GOTO:
        add_atom(A_JUMP, s, NULL);
        jump_block(label_block(s->str));
        return;
    case S_LABEL: {
        int lb = label_block(s->str);
        fall_to(lb);
        cur = lb;
        Block *b = &bl[lb];
        if (b->nlabels == b->caplabels) {
            b->caplabels = b->caplabels ? b->caplabels * 2 : 2;
            b->labels = realloc(b->labels, b->caplabels * sizeof *b->labels);
        }
        b->labels[b->nlabels++] = s;
        build_stmt(s->l);
        return;
    }
    case S_RETURN:
        if (s->l) {
            add_atom(A_RET, s, &s->l);
        } else {
            add_atom(A_JUMP, s, NULL);
        }
        bl[cur].term = T_RET;
        cur = new_block();
        return;
    default:
        return; /* S_NOP, S_ZERO */
    }
}

/* Builds the graph of the function body; block 0 is the entry. */
static int cfg_build(Node *body)
{
    cfg_ok = 1;
    nbrk = ncont = nsw = 0;
    cur = new_block();
    build_stmt(body);
    if (!cfg_ok) {
        cfg_free();
    }
    return cfg_ok;
}

/* Returns the number of successors of a block. */
static int succ_count(Block *b)
{
    return b->term == T_SWITCH ? b->nmulti : b->nsucc;
}

/* Returns the i-th successor of a block. */
static int succ_at(Block *b, int i)
{
    return b->term == T_SWITCH ? b->multi[i] : b->succ[i];
}

/* Runs the atoms of a block on copies of their expressions and updates env.  Returns which
 * successors can be reached: bit 0 the first, bit 1 the second (only decided for a branch whose
 * condition turns out to be constant). */
static int cfg_transfer(Block *b, Env *env, uint64_t analysis_flags)
{
    uint64_t saved = fl;
    int taken = 3;
    fl = analysis_flags;
    for (int i = 0; i < b->natoms; i++) {
        Atom *a = &b->atoms[i];
        if (a->kind == A_JUMP || !*a->slot) {
            continue;
        }
        Node *c = clone_expr(*a->slot);
        ex(c, env, a->kind == A_COND);
        if (a->kind == A_COND && b->term == T_BRANCH && (saved & FCO_CCP)) {
            if (c->kind == N_NUM) {
                taken = c->val ? 1 : 2;
            } else if (c->kind == N_FNUM) {
                taken = c->fval != 0 ? 1 : 2;
            }
        }
    }
    fl = saved;
    return taken;
}

/* Finds what is known at the start of every block that can be reached (optimistic: a block is
 * only entered along edges that can be taken).  Returns 0 if it does not settle. */
static int cfg_solve(void)
{
    uint64_t aflags = fl | FCO_CONSTPROP | FCO_COPYPROP | FCO_FOLD | FCO_ALGEBRA | FCO_COMPARE |
                      FCO_BOOL;
    int *queue = malloc((nbl + 1) * sizeof *queue);
    char *queued = calloc(nbl, 1);
    int qh = 0, qt = 0, count = 0, ok = 1;
    long budget = (long)nbl * 60 + 1000;
    bl[0].vis = 1;
    bl[0].in.n = 0;
    queue[qt++] = 0;
    queued[0] = 1;
    count = 1;
    while (count > 0) {
        if (--budget < 0) {
            ok = 0;
            break;
        }
        int id = queue[qh];
        qh = (qh + 1) % (nbl + 1);
        count--;
        queued[id] = 0;
        Block *b = &bl[id];
        Env env = b->in;
        int taken = cfg_transfer(b, &env, aflags);
        int ns = succ_count(b);
        for (int k = 0; k < ns; k++) {
            if (b->term == T_BRANCH && !(taken & (1 << k))) {
                continue;
            }
            Block *s = &bl[succ_at(b, k)];
            int sid = succ_at(b, k);
            int push = 0;
            if (!s->vis) {
                s->vis = 1;
                s->in = env;
                push = 1;
            } else {
                int before = s->in.n;
                env_join(&s->in, &env);
                push = s->in.n != before;
            }
            if (push && !queued[sid]) {
                queued[sid] = 1;
                queue[qt] = sid;
                qt = (qt + 1) % (nbl + 1);
                count++;
            }
        }
    }
    free(queue);
    free(queued);
    return ok;
}

/* Applies the result of the analysis: propagates constants and copies into the reachable blocks and
 * removes the code of the blocks that can never run. */
static void cfg_apply(void)
{
    uint64_t saved = fl;
    fl &= ~(uint64_t)(FCO_CONSTPROP | FCO_COPYPROP);
    if (saved & FCO_GCONST) {
        fl |= FCO_CONSTPROP;
    }
    if (saved & FCO_GCOPY) {
        fl |= FCO_COPYPROP;
    }
    for (int id = 0; id < nbl; id++) {
        Block *b = &bl[id];
        if (b->vis) {
            Env env = b->in;
            for (int i = 0; i < b->natoms; i++) {
                Atom *a = &b->atoms[i];
                if (a->kind != A_JUMP && *a->slot) {
                    ex(*a->slot, &env, a->kind == A_COND);
                }
            }
            continue;
        }
        if (!(saved & FCO_UBLOCK)) {
            continue;
        }
        for (int i = 0; i < b->natoms; i++) {
            Atom *a = &b->atoms[i];
            switch (a->kind) {
            case A_EXPR:
            case A_RET:
            case A_JUMP:
                make_nop(a->stmt);
                break;
            case A_COND:
                if (pure(*a->slot) && !((*a->slot)->kind == N_NUM && !(*a->slot)->val)) {
                    *a->slot = num(0, ty_int, a->stmt->line);
                    changed = 1;
                }
                break;
            default:
                break;
            }
        }
        for (int i = 0; i < b->nlabels; i++) {
            Node *lab = b->labels[i];
            if (lab->kind == S_LABEL && lab->l && lab->l->kind != S_CASE &&
                lab->l->kind != S_DEFAULT) {
                rep(lab, lab->l);
            }
        }
    }
    fl = saved;
}

/* ------------------------------------------------------------------ */
/* live variables and global dead code elimination                     */
/* ------------------------------------------------------------------ */
enum { MAXLIVE = 512 };
static Sym *lv_syms[MAXLIVE];
static int nlv, lv_words, lv_over;

/* Returns the number of a tracked variable, or -1. */
static int lv_index(Sym *s)
{
    for (int i = 0; i < nlv; i++) {
        if (lv_syms[i] == s) {
            return i;
        }
    }
    return -1;
}

/* Walker callback: collects the variables whose liveness is tracked. */
static void lv_collect_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind == N_VAR && trackable(n->sym) && lv_index(n->sym) < 0) {
        if (nlv == MAXLIVE) {
            lv_over = 1;
        } else {
            lv_syms[nlv++] = n->sym;
        }
    }
}

static void lv_use_chain(Node *n, uint64_t *set);

/* Marks the variables an expression reads (the target of a plain assignment is not read). */
static void lv_use(Node *n, uint64_t *set)
{
    if (!n) {
        return;
    }
    if (n->kind == N_VAR) {
        int i = trackable(n->sym) ? lv_index(n->sym) : -1;
        if (i >= 0) {
            set[i / 64] |= 1ULL << (i % 64);
        }
        return;
    }
    if (n->kind == N_ASSIGN && n->l->kind == N_VAR) {
        lv_use_chain(n->r, set);
        return;
    }
    lv_use_chain(n->l, set);
    lv_use_chain(n->r, set);
    lv_use_chain(n->c, set);
}

static void lv_use_chain(Node *n, uint64_t *set)
{
    for (; n; n = n->next) {
        lv_use(n, set);
    }
}

/* Returns the number of the variable that the expression statement x = e assigns, or -1. */
static int lv_def(Node *root)
{
    if (root && root->kind == N_ASSIGN && root->l->kind == N_VAR && trackable(root->l->sym)) {
        return lv_index(root->l->sym);
    }
    return -1;
}

/* Moves the set of live variables backwards over one atom. */
static void lv_atom(uint64_t *set, Atom *a)
{
    if (a->kind == A_JUMP || !*a->slot) {
        return;
    }
    Node *root = *a->slot;
    int d = (a->kind == A_EXPR || a->kind == A_VALUE) ? lv_def(root) : -1;
    if (d >= 0) {
        set[d / 64] &= ~(1ULL << (d % 64));
        lv_use(root->r, set);
    } else {
        lv_use(root, set);
    }
}

static uint64_t *lv_in, *lv_out; /* live variables at the start and end of every block */

/* Computes the live variables of every block of the graph into lv_in and lv_out.  Returns 0 if
 * there are too many variables or the analysis does not settle. */
static int lv_solve(Node *body)
{
    nlv = lv_over = 0;
    walk(body, lv_collect_cb, NULL);
    if (lv_over || nlv == 0) {
        return 0;
    }
    int w = lv_words = (nlv + 63) / 64;
    lv_in = calloc((size_t)nbl * w, sizeof *lv_in);
    lv_out = calloc((size_t)nbl * w, sizeof *lv_out);
    uint64_t tmp[MAXLIVE / 64];
    int again = 1;
    for (int round = 0; again && round < 200; round++) {
        again = 0;
        for (int id = nbl - 1; id >= 0; id--) {
            Block *b = &bl[id];
            uint64_t *o = lv_out + (size_t)id * w;
            for (int k = 0; k < w; k++) {
                o[k] = 0;
            }
            for (int k = 0; k < succ_count(b); k++) {
                uint64_t *si = lv_in + (size_t)succ_at(b, k) * w;
                for (int j = 0; j < w; j++) {
                    o[j] |= si[j];
                }
            }
            memcpy(tmp, o, w * sizeof *tmp);
            for (int i = b->natoms - 1; i >= 0; i--) {
                lv_atom(tmp, &b->atoms[i]);
            }
            uint64_t *bi = lv_in + (size_t)id * w;
            if (memcmp(bi, tmp, w * sizeof *tmp)) {
                memcpy(bi, tmp, w * sizeof *tmp);
                again = 1;
            }
        }
    }
    if (again) {
        free(lv_in);
        free(lv_out);
        lv_in = lv_out = NULL;
        return 0;
    }
    return 1;
}

/* Releases the result of lv_solve(). */
static void lv_release(void)
{
    free(lv_in);
    free(lv_out);
    lv_in = lv_out = NULL;
}

/* Removes the assignments whose value is never used, using the live variables at each point. */
static void cfg_live_dce(Node *body)
{
    if (!lv_solve(body)) {
        return;
    }
    int w = lv_words;
    uint64_t tmp[MAXLIVE / 64];
    for (int id = 0; id < nbl; id++) {
        Block *b = &bl[id];
        memcpy(tmp, lv_out + (size_t)id * w, w * sizeof *tmp);
        for (int i = b->natoms - 1; i >= 0; i--) {
            Atom *a = &b->atoms[i];
            int d = (a->kind == A_EXPR || a->kind == A_VALUE) ? lv_def(*a->slot) : -1;
            if (d >= 0 && !(tmp[d / 64] >> (d % 64) & 1)) {
                Node *root = *a->slot;
                if (same_value_rep(root->r->ty, root->ty)) {
                    Type *t = root->ty;
                    rep(root, root->r);
                    root->ty = t;
                }
            }
            lv_atom(tmp, a);
        }
    }
    lv_release();
}

/* ------------------------------------------------------------------ */
/* jump threading, label merging                                       */
/* ------------------------------------------------------------------ */
static Node **label_nodes;
static int nlabs, caplabs;

/* Walker callback: collects the goto labels. */
static void label_collect_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind == S_LABEL) {
        if (nlabs == caplabs) {
            caplabs = caplabs ? caplabs * 2 : 16;
            label_nodes = realloc(label_nodes, caplabs * sizeof *label_nodes);
        }
        label_nodes[nlabs++] = n;
    }
}

/* Returns the label node with the given name, or NULL. */
static Node *find_label(char *name)
{
    for (int i = 0; i < nlabs; i++) {
        if (label_nodes[i]->str == name) {
            return label_nodes[i];
        }
    }
    return NULL;
}

/* Walker callback: sends a goto straight to where the label it names would jump on. */
static void thread_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind != S_GOTO) {
        return;
    }
    char *target = n->str;
    for (int hops = 0; hops < 16; hops++) {
        Node *lab = find_label(target);
        if (!lab) {
            break;
        }
        Node *x = lab->l;
        while (x && x->kind == S_LABEL) {
            x = x->l;
        }
        if (x && x->kind == S_GOTO && x->str != target) {
            target = x->str;
            continue;
        }
        break;
    }
    if (target != n->str) {
        n->str = target;
        changed = 1;
    }
}

/* Walker callback: counts the gotos that name a label (stored in the label's val field). */
static void label_ref_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind == S_GOTO) {
        Node *lab = find_label(n->str);
        if (lab) {
            lab->val++;
        }
    }
}

/* Walker callback: removes a label that no goto names, which merges its block into the block
 * that falls into it. */
static void label_merge_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind == S_LABEL && n->val == 0 && n->l && n->l->kind != S_CASE &&
        n->l->kind != S_DEFAULT) {
        rep(n, n->l);
    }
}

/* Threads jumps and merges blocks whose label is no longer a jump target. */
static void cfg_threading(Node *body, int thread, int merge)
{
    nlabs = 0;
    walk(body, label_collect_cb, NULL);
    if (nlabs == 0) {
        return;
    }
    if (thread) {
        walk(body, thread_cb, NULL);
    }
    if (merge) {
        for (int i = 0; i < nlabs; i++) {
            label_nodes[i]->val = 0;
        }
        walk(body, label_ref_cb, NULL);
        walk(body, label_merge_cb, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* tail-recursion elimination                                          */
/* ------------------------------------------------------------------ */
enum { MAXTAIL = 64, MAXPARAM = 16 };
static Node *tail_rets[MAXTAIL];
static int ntail, tail_bad;
static Sym *tail_fn;

/* Walker callback: finds the returns of a direct call of the function itself and the constructs
 * that make reusing the frame unsafe (variables whose address could be held). */
static void tail_scan_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind == N_VAR && n->sym && n->sym->kind == SY_VAR && !n->sym->global &&
        (!scalar_ty(n->sym->ty) || n->sym->esc)) {
        tail_bad = 1;
    } else if (n->kind == S_ZERO) {
        tail_bad = 1;
    } else if (n->kind == S_RETURN && n->l && n->l->kind == N_CALL && n->l->sym == tail_fn) {
        if (ntail < MAXTAIL) {
            tail_rets[ntail++] = n;
        }
    }
}

/* Turns every "return f(args);" of a function f into assignments to its parameters and a jump to
 * the start of the body. */
static void tail_recursion(Sym *f)
{
    Type *ft = f->ty;
    if (ft->variadic || ft->noproto || is_agg(ft->ret) || ft->nparams > MAXPARAM) {
        return;
    }
    Sym *params[MAXPARAM];
    int np = 0;
    for (int i = 0; i < f->ndbg; i++) {
        if (f->dbgvars[i]->is_param) {
            if (np == MAXPARAM) {
                return;
            }
            params[np++] = f->dbgvars[i];
        }
    }
    if (np != ft->nparams) {
        return;
    }
    tail_fn = f;
    ntail = tail_bad = 0;
    walk(f->body, tail_scan_cb, NULL);
    for (int i = 0; i < np; i++) {
        if (!scalar_ty(params[i]->ty) || params[i]->esc) {
            tail_bad = 1;
        }
    }
    if (tail_bad || ntail == 0) {
        return;
    }
    char *top = intern("$tail", 5);
    int done = 0;
    for (int t = 0; t < ntail; t++) {
        Node *ret = tail_rets[t], *call = ret->l;
        Node *args[MAXPARAM];
        int na = 0;
        for (Node *a = call->r; a && na <= MAXPARAM; a = a->next) {
            if (na < MAXPARAM) {
                args[na] = a;
            }
            na++;
        }
        if (na != np) {
            continue;
        }
        int ok = 1;
        for (int i = 0; i < np; i++) {
            if (!same_value_rep(args[i]->ty, params[i]->ty)) {
                ok = 0;
            }
        }
        if (!ok) {
            continue;
        }
        int line = ret->line;
        Node *blk = new_node(S_BLOCK, NULL, line), **tail = &blk->l;
        blk->cmpty_uns = line;
        Node *rhs[MAXPARAM];
        for (int i = 0; i < np; i++) {
            args[i]->next = NULL;
        }
        for (int i = 0; i < np; i++) { /* arguments may use the parameters: go through temporaries */
            rhs[i] = args[i];
            if (np > 1 && args[i]->kind != N_NUM && args[i]->kind != N_FNUM) {
                Node *tv = new_temp(params[i]->ty, line);
                Node *as = new_node(N_ASSIGN, params[i]->ty, line);
                as->l = copy_node(tv);
                as->r = args[i];
                Node *st = new_node(S_EXPR, NULL, line);
                st->l = as;
                *tail = st;
                tail = &st->next;
                rhs[i] = copy_node(tv);
            }
        }
        for (int i = 0; i < np; i++) {
            Node *pv = new_node(N_VAR, params[i]->ty, line);
            pv->sym = params[i];
            Node *as = new_node(N_ASSIGN, params[i]->ty, line);
            as->l = pv;
            as->r = rhs[i];
            Node *st = new_node(S_EXPR, NULL, line);
            st->l = as;
            *tail = st;
            tail = &st->next;
        }
        Node *go = new_node(S_GOTO, NULL, line);
        go->str = top;
        *tail = go;
        rep(ret, blk);
        done++;
    }
    if (done) {
        Node *lab = new_node(S_LABEL, NULL, f->body->line);
        lab->str = top;
        lab->l = new_node(S_NOP, NULL, f->body->line);
        lab->next = f->body->l;
        f->body->l = lab;
    }
}

/* ------------------------------------------------------------------ */
/* graph structure: reachability, predecessors, dominators, loops      */
/* ------------------------------------------------------------------ */
enum { MAXGRAPH = 3000 };

static char *g_reach;            /* block reachable from the entry */
static int *g_pstart, *g_pedge;  /* predecessors of block b: g_pedge[g_pstart[b] .. g_pstart[b + 1]) */
static uint64_t *g_dom;          /* g_dom[b * g_w + k]: bit set of the blocks that dominate b */
static int g_w;

typedef struct Loop {
    int header, nblocks, ok;
    Node *stmt;      /* the while, do or for statement of a structured loop */
    uint64_t *in;    /* the blocks of the loop */
    Sym **asg;       /* variables assigned in the loop, with the number of assignments */
    int *asgn, nasg, asg_over;
} Loop;

static Loop *loops;
static int nloops;

/* Releases the graph analysis. */
static void graph_free(void)
{
    free(g_reach);
    free(g_pstart);
    free(g_pedge);
    free(g_dom);
    g_reach = NULL;
    g_pstart = g_pedge = NULL;
    g_dom = NULL;
    for (int i = 0; i < nloops; i++) {
        free(loops[i].in);
        free(loops[i].asg);
        free(loops[i].asgn);
    }
    free(loops);
    loops = NULL;
    nloops = 0;
}

static int bit_has(const uint64_t *set, int i)
{
    return (int)(set[i / 64] >> (i % 64) & 1);
}

/* Computes reachability, predecessors and dominators.  Returns 0 if the graph is too big. */
static int graph_build(void)
{
    if (nbl > MAXGRAPH) {
        return 0;
    }
    g_w = (nbl + 63) / 64;
    g_reach = calloc(nbl, 1);
    int *stack = malloc(nbl * sizeof *stack), sp = 0;
    stack[sp++] = 0;
    g_reach[0] = 1;
    while (sp > 0) {
        Block *b = &bl[stack[--sp]];
        for (int k = 0; k < succ_count(b); k++) {
            int s = succ_at(b, k);
            if (!g_reach[s]) {
                g_reach[s] = 1;
                stack[sp++] = s;
            }
        }
    }
    free(stack);
    g_pstart = calloc(nbl + 2, sizeof *g_pstart);
    for (int b = 0; b < nbl; b++) {
        if (!g_reach[b]) {
            continue;
        }
        for (int k = 0; k < succ_count(&bl[b]); k++) {
            g_pstart[succ_at(&bl[b], k) + 1]++;
        }
    }
    for (int b = 0; b < nbl; b++) {
        g_pstart[b + 1] += g_pstart[b];
    }
    g_pedge = malloc((g_pstart[nbl] + 1) * sizeof *g_pedge);
    int *fill = calloc(nbl, sizeof *fill);
    for (int b = 0; b < nbl; b++) {
        if (!g_reach[b]) {
            continue;
        }
        for (int k = 0; k < succ_count(&bl[b]); k++) {
            int s = succ_at(&bl[b], k);
            g_pedge[g_pstart[s] + fill[s]++] = b;
        }
    }
    free(fill);
    g_dom = malloc((size_t)nbl * g_w * sizeof *g_dom);
    for (int b = 0; b < nbl; b++) {
        for (int k = 0; k < g_w; k++) {
            g_dom[(size_t)b * g_w + k] = b == 0 ? 0 : ~0ULL;
        }
    }
    g_dom[0] |= 1;
    int again = 1;
    for (int round = 0; again && round < 100; round++) {
        again = 0;
        for (int b = 1; b < nbl; b++) {
            if (!g_reach[b]) {
                continue;
            }
            uint64_t *d = g_dom + (size_t)b * g_w;
            uint64_t nw[MAXGRAPH / 64 + 1];
            for (int k = 0; k < g_w; k++) {
                nw[k] = ~0ULL;
            }
            for (int e = g_pstart[b]; e < g_pstart[b + 1]; e++) {
                uint64_t *pd = g_dom + (size_t)g_pedge[e] * g_w;
                for (int k = 0; k < g_w; k++) {
                    nw[k] &= pd[k];
                }
            }
            nw[b / 64] |= 1ULL << (b % 64);
            if (memcmp(nw, d, g_w * sizeof *d)) {
                memcpy(d, nw, g_w * sizeof *d);
                again = 1;
            }
        }
    }
    return !again;
}

/* Returns true if block a dominates block b. */
static int dominates(int a, int b)
{
    return bit_has(g_dom + (size_t)b * g_w, a);
}

/* Walker callback: records the variables a tree assigns in the loop in ud. */
static void loop_asg_cb(Node *n, void *ud)
{
    Loop *lp = ud;
    switch (n->kind) {
    case N_ASSIGN:
    case N_ASSIGNOP:
    case N_PREINC:
    case N_POSTINC:
    case N_PREDEC:
    case N_POSTDEC:
        if (n->l && n->l->kind == N_VAR) {
            for (int i = 0; i < lp->nasg; i++) {
                if (lp->asg[i] == n->l->sym) {
                    lp->asgn[i]++;
                    return;
                }
            }
            if (lp->nasg == 64) {
                lp->asg_over = 1;
            } else {
                lp->asg[lp->nasg] = n->l->sym;
                lp->asgn[lp->nasg++] = 1;
            }
        }
        break;
    default:
        break;
    }
}

/* Finds the natural loops: a back edge t -> h where h dominates t, with all the blocks that reach
 * t without passing h.  A loop is "structured" when its header comes from a while, do or for
 * statement and every block of it is dominated by the header (it is entered only at the top). */
static void loops_find(void)
{
    loops = calloc(nbl + 1, sizeof *loops);
    for (int t = 0; t < nbl; t++) {
        if (!g_reach[t]) {
            continue;
        }
        for (int k = 0; k < succ_count(&bl[t]); k++) {
            int h = succ_at(&bl[t], k);
            if (!dominates(h, t)) {
                continue;
            }
            Loop *lp = NULL;
            for (int i = 0; i < nloops; i++) {
                if (loops[i].header == h) {
                    lp = &loops[i];
                }
            }
            if (!lp) {
                lp = &loops[nloops++];
                lp->header = h;
                lp->in = calloc(g_w, sizeof *lp->in);
                lp->asg = calloc(64, sizeof *lp->asg);
                lp->asgn = calloc(64, sizeof *lp->asgn);
                lp->in[h / 64] |= 1ULL << (h % 64);
            }
            int *stack = malloc(nbl * sizeof *stack), sp = 0;
            if (!bit_has(lp->in, t)) {
                lp->in[t / 64] |= 1ULL << (t % 64);
                stack[sp++] = t;
            }
            while (sp > 0) {
                int b = stack[--sp];
                for (int e = g_pstart[b]; e < g_pstart[b + 1]; e++) {
                    int p = g_pedge[e];
                    if (!bit_has(lp->in, p)) {
                        lp->in[p / 64] |= 1ULL << (p % 64);
                        stack[sp++] = p;
                    }
                }
            }
            free(stack);
        }
    }
    for (int i = 0; i < nloops; i++) {
        Loop *lp = &loops[i];
        lp->stmt = bl[lp->header].loop;
        lp->ok = lp->stmt != NULL;
        for (int b = 0; b < nbl; b++) {
            if (!bit_has(lp->in, b)) {
                continue;
            }
            lp->nblocks++;
            if (!dominates(lp->header, b)) {
                lp->ok = 0;
            }
            for (int a = 0; a < bl[b].natoms; a++) {
                Atom *at = &bl[b].atoms[a];
                if (at->kind != A_JUMP && *at->slot) {
                    walk(*at->slot, loop_asg_cb, lp);
                }
            }
        }
        if (lp->asg_over) {
            lp->ok = 0;
        }
    }
}

/* Returns how often the loop assigns the variable. */
static int loop_defs(Loop *lp, Sym *s)
{
    for (int i = 0; i < lp->nasg; i++) {
        if (lp->asg[i] == s) {
            return lp->asgn[i];
        }
    }
    return 0;
}

/* Returns the innermost loop containing block b, or NULL. */
static Loop *loop_of(int b)
{
    Loop *best = NULL;
    for (int i = 0; i < nloops; i++) {
        if (bit_has(loops[i].in, b) && (!best || loops[i].nblocks < best->nblocks)) {
            best = &loops[i];
        }
    }
    return best;
}

/* Returns true if the block runs in every iteration of the loop (it dominates every latch). */
static int every_iteration(Loop *lp, int b)
{
    for (int t = 0; t < nbl; t++) {
        if (bit_has(lp->in, t) && g_reach[t]) {
            for (int k = 0; k < succ_count(&bl[t]); k++) {
                if (succ_at(&bl[t], k) == lp->header && !dominates(b, t)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Replaces the loop statement s by a block { for-init; pre; loop } so that the statements in pre
 * run once before the loop.  The graph is stale afterwards. */
static void insert_before_loop(Node *s, Node *pre)
{
    Node *lp = copy_node(s);
    Node *init = NULL;
    if (s->kind == S_FOR) {
        init = s->d;
        lp->d = NULL;
    }
    Node *blk = new_node(S_BLOCK, NULL, s->line);
    blk->cmpty_uns = s->line;
    Node **t = &blk->l;
    if (init) {
        *t = init;
        while (*t) {
            t = &(*t)->next;
        }
    }
    *t = pre;
    while (*t) {
        t = &(*t)->next;
    }
    *t = lp;
    rep(s, blk);
}

/* Replaces the statement st by the block { st; extra } so that extra runs right after it. */
static void insert_after_stmt(Node *st, Node *extra)
{
    Node *old = copy_node(st);
    Node *blk = new_node(S_BLOCK, NULL, st->line);
    blk->cmpty_uns = st->line;
    blk->l = old;
    old->next = extra;
    rep(st, blk);
}

/* Creates the statement "target = value;". */
static Node *assign_stmt(Node *target_var, Node *value, int line)
{
    Node *as = new_node(N_ASSIGN, target_var->ty, line);
    as->l = target_var;
    as->r = value;
    Node *st = new_node(S_EXPR, NULL, line);
    st->l = as;
    return st;
}

/* ------------------------------------------------------------------ */
/* reaching definitions                                                */
/* ------------------------------------------------------------------ */
enum { MAXDEFS = 1024 };

static struct Def {
    Sym *var;
    int blk, atom;
} defs[MAXDEFS];
static int ndefs, def_w;
static Sym **rd_vars; /* the variables with definitions; the entry value of v is definition v */
static int nrdv;
static uint64_t *rd_in; /* definitions that reach the start of every block */

/* Returns the number of the variable, or -1. */
static int rdv_index(Sym *s)
{
    for (int i = 0; i < nrdv; i++) {
        if (rd_vars[i] == s) {
            return i;
        }
    }
    return -1;
}

/* Walker callback: lists the variables an expression assigns. */
static void asg_list_cb(Node *n, void *ud)
{
    Sym **list = ud; /* list[0..63], count in list[64] */
    switch (n->kind) {
    case N_ASSIGN:
    case N_ASSIGNOP:
    case N_PREINC:
    case N_POSTINC:
    case N_PREDEC:
    case N_POSTDEC:
        if (n->l && n->l->kind == N_VAR && trackable(n->l->sym)) {
            long cnt = (long)list[64];
            for (long i = 0; i < cnt; i++) {
                if (list[i] == n->l->sym) {
                    return;
                }
            }
            if (cnt < 64) {
                list[cnt] = n->l->sym;
                list[64] = (Sym *)(cnt + 1);
            }
        }
        break;
    default:
        break;
    }
}

/* Lists the tracked variables assigned anywhere in the atom; returns how many. */
static int atom_assigns(Atom *a, Sym **list)
{
    list[64] = NULL;
    if (a->kind != A_JUMP && *a->slot) {
        walk(*a->slot, asg_list_cb, list);
    }
    return (int)(long)list[64];
}

/* Computes which definitions of the tracked variables reach the start of each block. */
static int rd_solve(Node *body)
{
    nlv = lv_over = 0;
    walk(body, lv_collect_cb, NULL);
    if (lv_over || nlv == 0) {
        return 0;
    }
    rd_vars = lv_syms; /* the table of tracked variables */
    nrdv = nlv;
    ndefs = nrdv; /* the first definitions are the values at function entry */
    for (int b = 0; b < nbl; b++) {
        for (int a = 0; a < bl[b].natoms; a++) {
            Sym *list[65];
            int n = atom_assigns(&bl[b].atoms[a], list);
            for (int k = 0; k < n; k++) {
                if (ndefs == MAXDEFS) {
                    return 0;
                }
                defs[ndefs].var = list[k];
                defs[ndefs].blk = b;
                defs[ndefs].atom = a;
                ndefs++;
            }
        }
    }
    def_w = (ndefs + 63) / 64;
    rd_in = calloc((size_t)nbl * def_w, sizeof *rd_in);
    uint64_t *out = calloc((size_t)nbl * def_w, sizeof *out);
    for (int v = 0; v < nrdv; v++) {
        rd_in[v / 64] |= 1ULL << (v % 64);
    }
    int again = 1;
    for (int round = 0; again && round < 100; round++) {
        again = 0;
        for (int b = 0; b < nbl; b++) {
            if (!g_reach[b]) {
                continue;
            }
            uint64_t *in = rd_in + (size_t)b * def_w;
            if (b > 0) {
                for (int k = 0; k < def_w; k++) {
                    in[k] = 0;
                }
                for (int e = g_pstart[b]; e < g_pstart[b + 1]; e++) {
                    uint64_t *po = out + (size_t)g_pedge[e] * def_w;
                    for (int k = 0; k < def_w; k++) {
                        in[k] |= po[k];
                    }
                }
            }
            uint64_t cur_set[MAXDEFS / 64];
            memcpy(cur_set, in, def_w * sizeof *in);
            for (int a = 0; a < bl[b].natoms; a++) {
                Sym *list[65];
                int n = atom_assigns(&bl[b].atoms[a], list);
                for (int k = 0; k < n; k++) {
                    int v = rdv_index(list[k]);
                    for (int d = 0; d < ndefs; d++) { /* kill the other definitions of v */
                        if (d == v || (d >= nrdv && defs[d].var == list[k])) {
                            cur_set[d / 64] &= ~(1ULL << (d % 64));
                        }
                    }
                    for (int d = nrdv; d < ndefs; d++) {
                        if (defs[d].blk == b && defs[d].atom == a && defs[d].var == list[k]) {
                            cur_set[d / 64] |= 1ULL << (d % 64);
                        }
                    }
                }
            }
            uint64_t *o = out + (size_t)b * def_w;
            if (memcmp(o, cur_set, def_w * sizeof *o)) {
                memcpy(o, cur_set, def_w * sizeof *o);
                again = 1;
            }
        }
    }
    free(out);
    if (again) {
        free(rd_in);
        rd_in = NULL;
        return 0;
    }
    return 1;
}

static void rd_release(void)
{
    free(rd_in);
    rd_in = NULL;
}

/* Fills set with the definitions that reach the point just before atom number atom of block b. */
static void rd_before(int b, int atom, uint64_t *set)
{
    memcpy(set, rd_in + (size_t)b * def_w, def_w * sizeof *set);
    for (int a = 0; a < atom; a++) {
        Sym *list[65];
        int n = atom_assigns(&bl[b].atoms[a], list);
        for (int k = 0; k < n; k++) {
            int v = rdv_index(list[k]);
            for (int d = 0; d < ndefs; d++) {
                if (d == v || (d >= nrdv && defs[d].var == list[k])) {
                    set[d / 64] &= ~(1ULL << (d % 64));
                }
            }
            for (int d = nrdv; d < ndefs; d++) {
                if (defs[d].blk == b && defs[d].atom == a && defs[d].var == list[k]) {
                    set[d / 64] |= 1ULL << (d % 64);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* expressions: available expressions, global CSE, PRE, loop hoisting  */
/* ------------------------------------------------------------------ */
enum { MAXEX = 128 };

typedef struct Ex {
    Node *tree;
    Sym *vars[8];
    int nvars;
    Node *temp; /* the variable that holds the value */
} Ex;
static Ex exs[MAXEX];
static int nex;

static int eset_has(const ESet *s, int i)
{
    return (int)(s->w[i / 64] >> (i % 64) & 1);
}

static void eset_add(ESet *s, int i)
{
    s->w[i / 64] |= 1ULL << (i % 64);
}

/* Returns the number of nodes of a tree. */
static int gc_size(Node *e)
{
    return e ? 1 + gc_size(e->l) + gc_size(e->r) : 0;
}

/* Returns true if e is built from constants and local scalar variables with operators that do not
 * trap, so it can be evaluated at any point and any number of times. */
static int gc_ok(Node *e)
{
    switch (e->kind) {
    case N_NUM:
    case N_FNUM:
        return 1;
    case N_VAR:
        return trackable(e->sym);
    case N_NEG:
    case N_NOT:
    case N_BITNOT:
        return scalar_ty(e->ty) && gc_ok(e->l);
    case N_CAST:
        return scalar_ty(e->ty) && scalar_ty(e->l->ty) && gc_ok(e->l);
    case N_BIN:
        if ((e->op == B_DIV || e->op == B_MOD) && !(e->r->kind == N_NUM && e->r->val != 0)) {
            return 0;
        }
        return gc_ok(e->l) && gc_ok(e->r);
    default:
        return 0;
    }
}

/* Returns true for an expression worth keeping in a temporary. */
static int gc_candidate(Node *e)
{
    return e->kind != N_NUM && e->kind != N_FNUM && e->kind != N_VAR && gc_size(e) >= 3 && gc_ok(e);
}

/* Collects the distinct variables of e into v (up to 8); returns 0 if there are more. */
static int gc_vars(Node *e, Sym **v, int *n)
{
    if (!e) {
        return 1;
    }
    if (e->kind == N_VAR) {
        for (int i = 0; i < *n; i++) {
            if (v[i] == e->sym) {
                return 1;
            }
        }
        if (*n == 8) {
            return 0;
        }
        v[(*n)++] = e->sym;
        return 1;
    }
    return gc_vars(e->l, v, n) && gc_vars(e->r, v, n);
}

/* Returns the number of the expression in the universe, or -1. */
static int ex_find(Node *e)
{
    for (int i = 0; i < nex; i++) {
        if (same_tree(exs[i].tree, e)) {
            return i;
        }
    }
    return -1;
}

typedef int (*GcFn)(Node *n, void *ud);

/* Calls fn on the candidate expressions in the part of the tree that is always evaluated.  fn
 * returns 1 to continue into the operands of the expression.  Expressions that use a variable in K
 * (assigned in the same atom) are skipped. */
static void gc_walk(Node *n, Sym **K, int nK, GcFn fn, void *ud);

static void gc_walk_kids(Node *n, Sym **K, int nK, GcFn fn, void *ud)
{
    switch (n->kind) {
    case N_LOGAND:
    case N_LOGOR:
        gc_walk(n->l, K, nK, fn, ud);
        return;
    case N_COND:
        gc_walk(n->c, K, nK, fn, ud);
        return;
    default:
        break;
    }
    Node *kids[3] = {n->l, n->r, n->c};
    for (int i = 0; i < 3; i++) {
        for (Node *k = kids[i]; k; k = k->next) {
            gc_walk(k, K, nK, fn, ud);
        }
    }
}

static void gc_walk(Node *n, Sym **K, int nK, GcFn fn, void *ud)
{
    if (!n) {
        return;
    }
    if (gc_candidate(n)) {
        int bad = 0;
        for (int i = 0; i < nK && !bad; i++) {
            bad = mentions(n, K[i]);
        }
        if (!bad && !fn(n, ud)) {
            return;
        }
    }
    gc_walk_kids(n, K, nK, fn, ud);
}

/* Callback of the universe building: adds the expression. */
static int ex_add_cb(Node *n, void *ud)
{
    (void)ud;
    if (ex_find(n) < 0 && nex < MAXEX) {
        Ex *x = &exs[nex];
        x->nvars = 0;
        if (gc_vars(n, x->vars, &x->nvars)) {
            x->tree = clone_expr(n); /* the tree itself is rewritten in place */
            x->temp = NULL;
            nex++;
        }
    }
    return 1;
}

/* Callback: marks the expression as generated. */
static int ex_gen_cb(Node *n, void *ud)
{
    int e = ex_find(n);
    if (e >= 0) {
        eset_add(ud, e);
    }
    return 1;
}

static ESet *av_in, *av_out;

/* Finds the expressions of the function and what each atom generates and invalidates. */
static int avail_prepare(void)
{
    nex = 0;
    for (int b = 0; b < nbl; b++) {
        if (!g_reach[b]) {
            continue;
        }
        for (int a = 0; a < bl[b].natoms; a++) {
            Atom *at = &bl[b].atoms[a];
            Sym *K[65];
            int nK = atom_assigns(at, K);
            if (at->kind != A_JUMP && *at->slot) {
                gc_walk(*at->slot, K, nK, ex_add_cb, NULL);
            }
        }
    }
    if (nex == 0) {
        return 0;
    }
    for (int b = 0; b < nbl; b++) {
        for (int a = 0; a < bl[b].natoms; a++) {
            Atom *at = &bl[b].atoms[a];
            memset(&at->gen, 0, sizeof at->gen);
            memset(&at->kill, 0, sizeof at->kill);
            Sym *K[65];
            int nK = atom_assigns(at, K);
            if (at->kind != A_JUMP && *at->slot) {
                gc_walk(*at->slot, K, nK, ex_gen_cb, &at->gen);
            }
            for (int e = 0; e < nex; e++) {
                for (int v = 0; v < exs[e].nvars; v++) {
                    for (int k = 0; k < nK; k++) {
                        if (exs[e].vars[v] == K[k]) {
                            eset_add(&at->kill, e);
                        }
                    }
                }
            }
        }
    }
    return 1;
}

/* Moves a set of available expressions over one atom. */
static void av_atom(ESet *s, Atom *a)
{
    for (int k = 0; k < 2; k++) {
        s->w[k] = (s->w[k] | a->gen.w[k]) & ~a->kill.w[k];
    }
}

/* Computes the expressions available at the start and end of every block (available = computed on
 * every path and not invalidated since). */
static int avail_solve(void)
{
    av_in = calloc(nbl, sizeof *av_in);
    av_out = calloc(nbl, sizeof *av_out);
    ESet all;
    memset(&all, 0, sizeof all);
    for (int e = 0; e < nex; e++) {
        eset_add(&all, e);
    }
    for (int b = 0; b < nbl; b++) {
        av_in[b] = b == 0 ? (ESet){{0, 0}} : all;
        av_out[b] = all;
    }
    int again = 1;
    for (int round = 0; again && round < 100; round++) {
        again = 0;
        for (int b = 0; b < nbl; b++) {
            if (!g_reach[b]) {
                continue;
            }
            ESet in = all;
            if (b == 0) {
                memset(&in, 0, sizeof in);
            }
            for (int e = g_pstart[b]; b > 0 && e < g_pstart[b + 1]; e++) {
                for (int k = 0; k < 2; k++) {
                    in.w[k] &= av_out[g_pedge[e]].w[k];
                }
            }
            ESet out = in;
            for (int a = 0; a < bl[b].natoms; a++) {
                av_atom(&out, &bl[b].atoms[a]);
            }
            if (memcmp(&in, &av_in[b], sizeof in) || memcmp(&out, &av_out[b], sizeof out)) {
                av_in[b] = in;
                av_out[b] = out;
                again = 1;
            }
        }
    }
    if (again) {
        free(av_in);
        free(av_out);
        av_in = av_out = NULL;
        return 0;
    }
    return 1;
}

static void avail_release(void)
{
    free(av_in);
    free(av_out);
    av_in = av_out = NULL;
}

typedef struct GcCtx {
    ESet avail;
    int phase; /* 0: find the expressions that are reused, 1: rewrite */
    Sym **K;
    int nK;
} GcCtx;

static int gc_need[MAXEX];

/* Callback of global CSE. */
static int gcse_cb(Node *n, void *ud)
{
    GcCtx *c = ud;
    int e = ex_find(n);
    if (e < 0) {
        return 1;
    }
    if (eset_has(&c->avail, e)) { /* computed on every path before: use the temporary */
        if (c->phase == 0) {
            gc_need[e] = 1;
        } else if (exs[e].temp) {
            rep(n, copy_node(exs[e].temp));
        }
        return 0;
    }
    if (c->phase == 1 && gc_need[e] && exs[e].temp) { /* a computation: also store the value */
        Node *orig = copy_node(n);
        Node *as = new_node(N_ASSIGN, n->ty, n->line);
        as->l = copy_node(exs[e].temp);
        as->r = orig;
        Node *cm = new_node(N_COMMA, n->ty, n->line);
        cm->l = as;
        cm->r = copy_node(exs[e].temp);
        rep(n, cm);
        gc_walk_kids(orig, c->K, c->nK, gcse_cb, c);
        return 0;
    }
    return 1;
}

/* Global common subexpression elimination: an expression that is available where it is computed
 * again is replaced by a temporary that every computation of it fills.  Returns 1 if it changed
 * the tree. */
static int gcse_apply(void)
{
    memset(gc_need, 0, sizeof gc_need);
    for (int phase = 0; phase < 2; phase++) {
        if (phase == 1) {
            int any = 0;
            for (int e = 0; e < nex; e++) {
                if (gc_need[e]) {
                    any = 1;
                    exs[e].temp = new_temp(exs[e].tree->ty, exs[e].tree->line);
                }
            }
            if (!any) {
                return 0;
            }
        }
        for (int b = 0; b < nbl; b++) {
            if (!g_reach[b]) {
                continue;
            }
            GcCtx c;
            c.avail = av_in[b];
            c.phase = phase;
            for (int a = 0; a < bl[b].natoms; a++) {
                Atom *at = &bl[b].atoms[a];
                Sym *K[65];
                c.K = K;
                c.nK = atom_assigns(at, K);
                if (at->kind != A_JUMP && *at->slot) {
                    gc_walk(*at->slot, K, c.nK, gcse_cb, &c);
                }
                av_atom(&c.avail, at);
            }
        }
    }
    return 1;
}

/* Callback of PRE: remembers the first expression of a block that is computed there. */
static struct {
    int expr;
    int blk, atom;
    ESet before;
    int found, only_blk, only_atom;
} pre_c;

static int pre_cb(Node *n, void *ud)
{
    (void)ud;
    int e = ex_find(n);
    if (e >= 0 && !pre_c.found && !eset_has(&pre_c.before, e)) {
        pre_c.found = 1;
        pre_c.expr = e;
        return 0;
    }
    return 1;
}

/* Partial redundancy elimination: an expression that is computed in a block where it is available
 * from some of the predecessors but not from all is computed at the end of the predecessors that
 * lack it, which makes it fully available so that global CSE can remove the later computation.
 * Returns 1 after inserting one expression. */
static int pre_insert(void)
{
    for (int b = 1; b < nbl; b++) {
        if (!g_reach[b] || g_pstart[b + 1] - g_pstart[b] < 2) {
            continue;
        }
        ESet some;
        memset(&some, 0, sizeof some);
        for (int e = g_pstart[b]; e < g_pstart[b + 1]; e++) {
            for (int k = 0; k < 2; k++) {
                some.w[k] |= av_out[g_pedge[e]].w[k];
            }
        }
        ESet cur_set = av_in[b];
        for (int a = 0; a < bl[b].natoms; a++) {
            Atom *at = &bl[b].atoms[a];
            Sym *K[65];
            int nK = atom_assigns(at, K);
            pre_c.found = 0;
            pre_c.before = cur_set;
            if (at->kind != A_JUMP && *at->slot) {
                gc_walk(*at->slot, K, nK, pre_cb, NULL);
            }
            if (pre_c.found) {
                int e = pre_c.expr;
                int fully = 1, ok = 1;
                for (int k = g_pstart[b]; k < g_pstart[b + 1]; k++) {
                    int p = g_pedge[k];
                    if (!eset_has(&av_out[p], e)) {
                        fully = 0;
                        Block *pb = &bl[p];
                        if (!(pb->term == T_FALL || pb->term == T_JUMP) || pb->nsucc != 1 ||
                            pb->natoms == 0 || pb->atoms[pb->natoms - 1].kind != A_EXPR) {
                            ok = 0;
                        }
                    }
                }
                if (eset_has(&some, e) && !fully && ok && !eset_has(&cur_set, e)) {
                    for (int k = g_pstart[b]; k < g_pstart[b + 1]; k++) {
                        int p = g_pedge[k];
                        if (!eset_has(&av_out[p], e)) {
                            Block *pb = &bl[p];
                            Node *st = pb->atoms[pb->natoms - 1].stmt;
                            Node *ns = new_node(S_EXPR, NULL, st->line);
                            ns->l = clone_expr(exs[e].tree);
                            insert_after_stmt(st, ns);
                        }
                    }
                    return 1;
                }
            }
            av_atom(&cur_set, at);
        }
    }
    return 0;
}

/* ---- loop-invariant code motion ---- */

/* Returns true if the expression only uses variables the loop does not assign. */
static int loop_invariant(Loop *lp, Node *e)
{
    Sym *v[8];
    int n = 0;
    if (!gc_vars(e, v, &n)) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (loop_defs(lp, v[i])) {
            return 0;
        }
    }
    return 1;
}

static struct {
    Loop *lp;
    Node *found;
    int blk;
} licm_c;

/* Callback: finds an invariant expression in a loop. */
static int licm_cb(Node *n, void *ud)
{
    (void)ud;
    if (!licm_c.found && loop_invariant(licm_c.lp, n)) {
        licm_c.found = n;
        return 0;
    }
    return 1;
}

/* Callback: replaces the occurrences of an invariant expression by its temporary. */
static int licm_rep_cb(Node *n, void *ud)
{
    if (same_tree(n, licm_c.found)) {
        rep(n, copy_node(ud));
        return 0;
    }
    return 1;
}

/* Moves one invariant assignment or expression out of a loop.  Returns 1 if it changed the tree. */
static int licm_transform(int use_rd, Node *body)
{
    int rd_ok = 0, lv_ok = 0;
    if (use_rd) {
        rd_ok = rd_solve(body);
        lv_ok = rd_ok && lv_solve(body);
    }
    int result = 0;
    for (int li = 0; li < nloops && !result; li++) {
        Loop *lp = &loops[li];
        if (!lp->ok) {
            continue;
        }
        /* (1) an invariant assignment  x = e;  whose variable is only used after it */
        for (int b = 0; b < nbl && !result && rd_ok && lv_ok; b++) {
            if (!bit_has(lp->in, b) || !every_iteration(lp, b) || loop_of(b) != lp) {
                continue;
            }
            for (int a = 0; a < bl[b].natoms && !result; a++) {
                Atom *at = &bl[b].atoms[a];
                if (at->kind != A_EXPR || (*at->slot)->kind != N_ASSIGN ||
                    (*at->slot)->l->kind != N_VAR) {
                    continue;
                }
                Node *as = *at->slot;
                Sym *x = as->l->sym;
                if (!trackable(x) || !gc_ok(as->r) || !loop_invariant(lp, as->r) ||
                    loop_defs(lp, x) != 1 || !same_value_rep(as->r->ty, as->ty)) {
                    continue;
                }
                int xi = lv_index(x), me = -1;
                for (int d = nrdv; d < ndefs; d++) {
                    if (defs[d].blk == b && defs[d].atom == a && defs[d].var == x) {
                        me = d;
                    }
                }
                if (xi < 0 || me < 0) {
                    continue;
                }
                int dead_after = 1; /* not live where control leaves the loop */
                for (int lb = 0; lb < nbl; lb++) {
                    if (!bit_has(lp->in, lb)) {
                        continue;
                    }
                    for (int k = 0; k < succ_count(&bl[lb]); k++) {
                        int s = succ_at(&bl[lb], k);
                        if (!bit_has(lp->in, s) &&
                            bit_has(lv_in + (size_t)s * lv_words, xi)) {
                            dead_after = 0;
                        }
                    }
                }
                int only_def = 1; /* every use in the loop is reached by this definition alone */
                for (int lb = 0; lb < nbl && only_def; lb++) {
                    if (!bit_has(lp->in, lb) || !g_reach[lb]) {
                        continue;
                    }
                    for (int la = 0; la < bl[lb].natoms && only_def; la++) {
                        Atom *u = &bl[lb].atoms[la];
                        if (u == at || u->kind == A_JUMP || !*u->slot || !mentions(*u->slot, x)) {
                            continue;
                        }
                        uint64_t rs[MAXDEFS / 64];
                        rd_before(lb, la, rs);
                        for (int d = 0; d < ndefs; d++) {
                            if (bit_has(rs, d) && (d == xi || (d >= nrdv && defs[d].var == x)) &&
                                d != me) {
                                only_def = 0;
                            }
                        }
                    }
                }
                if (dead_after && only_def) {
                    Node *copy = copy_node(at->stmt);
                    copy->next = NULL;
                    make_nop(at->stmt);
                    insert_before_loop(lp->stmt, copy);
                    result = 1;
                }
            }
        }
        /* (2) an invariant expression inside a bigger one: compute it once before the loop */
        for (int b = 0; b < nbl && !result; b++) {
            if (!bit_has(lp->in, b) || !every_iteration(lp, b) || !g_reach[b]) {
                continue;
            }
            for (int a = 0; a < bl[b].natoms && !result; a++) {
                Atom *at = &bl[b].atoms[a];
                if (at->kind == A_JUMP || !*at->slot) {
                    continue;
                }
                Sym *K[65];
                int nK = atom_assigns(at, K);
                licm_c.lp = lp;
                licm_c.found = NULL;
                gc_walk(*at->slot, K, nK, licm_cb, NULL);
                if (!licm_c.found) {
                    continue;
                }
                Node *e = clone_expr(licm_c.found);
                Node *keep = licm_c.found;
                licm_c.found = e;
                Node *t = new_temp(e->ty, e->line);
                /* replace the occurrences in all atoms of the loop */
                for (int lb = 0; lb < nbl; lb++) {
                    if (!bit_has(lp->in, lb) || !g_reach[lb]) {
                        continue;
                    }
                    for (int la = 0; la < bl[lb].natoms; la++) {
                        Atom *u = &bl[lb].atoms[la];
                        Sym *K2[65];
                        int nK2 = atom_assigns(u, K2);
                        if (u->kind != A_JUMP && *u->slot) {
                            gc_walk(*u->slot, K2, nK2, licm_rep_cb, t);
                        }
                    }
                }
                (void)keep;
                insert_before_loop(lp->stmt, assign_stmt(copy_node(t), e, e->line));
                result = 1;
            }
        }
    }
    if (rd_ok) {
        rd_release();
    }
    if (lv_ok) {
        lv_release();
    }
    return result;
}

/* ---- induction variables ---- */

/* An update of a basic induction variable found in a loop. */
typedef struct IvUpdate {
    Sym *var;
    long long step;
    Node *root;   /* the expression i++ , i += c, i = i + c */
    Atom *atom;
    Node **slotp; /* where the root is stored (for a for-loop step inside a comma) */
} IvUpdate;

/* Decodes "i++", "i--", "i += c", "i -= c", "i = i + c" and "i = i - c" for an integer variable. */
static int iv_decode(Node *e, Sym **var, long long *step)
{
    Node *l = e->l;
    if (e->kind == N_POSTINC || e->kind == N_PREINC || e->kind == N_POSTDEC ||
        e->kind == N_PREDEC) {
        if (l->kind != N_VAR || !is_int(l->ty)) {
            return 0;
        }
        *var = l->sym;
        *step = (e->kind == N_POSTINC || e->kind == N_PREINC) ? e->val : -e->val;
        return 1;
    }
    if (e->kind == N_ASSIGNOP && (e->op == B_ADD || e->op == B_SUB) && l->kind == N_VAR &&
        is_int(l->ty) && e->r->kind == N_NUM && same_rep(e->cty, l->ty)) {
        *var = l->sym;
        *step = e->op == B_ADD ? e->r->val : -e->r->val;
        return 1;
    }
    if (e->kind == N_ASSIGN && l->kind == N_VAR && is_int(l->ty) && e->r->kind == N_BIN &&
        (e->r->op == B_ADD || e->r->op == B_SUB) && e->r->l->kind == N_VAR &&
        e->r->l->sym == l->sym && e->r->r->kind == N_NUM && same_rep(e->r->ty, l->ty)) {
        *var = l->sym;
        *step = e->r->op == B_ADD ? e->r->r->val : -e->r->r->val;
        return 1;
    }
    return 0;
}

/* Finds the (single) standalone update of variable v in the loop. */
static int iv_find_update(Loop *lp, Sym *v, IvUpdate *u)
{
    int found = 0;
    for (int b = 0; b < nbl; b++) {
        if (!bit_has(lp->in, b) || !g_reach[b]) {
            continue;
        }
        for (int a = 0; a < bl[b].natoms; a++) {
            Atom *at = &bl[b].atoms[a];
            if ((at->kind != A_EXPR && at->kind != A_VALUE) || !*at->slot) {
                continue;
            }
            Node **cands[8];
            int nc = 0;
            Node **slotp = at->slot;
            while ((*slotp)->kind == N_COMMA && nc < 6) { /* a, b, c in a for-loop step */
                cands[nc++] = &(*slotp)->l;
                slotp = &(*slotp)->r;
            }
            cands[nc++] = slotp;
            for (int c = 0; c < nc; c++) {
                Sym *var;
                long long step;
                if (iv_decode(*cands[c], &var, &step) && var == v) {
                    u->var = v;
                    u->step = step;
                    u->root = *cands[c];
                    u->atom = at;
                    u->slotp = cands[c];
                    found++;
                }
            }
        }
    }
    return found == 1 && loop_defs(lp, v) == 1;
}

static struct {
    Loop *lp;
    Node *found;
    Sym *iv;
    long long step;       /* step of the derived value per iteration */
    int widen;            /* the induction variable is widened by a cast */
} iv_c;

/* Returns the induction variable behind X: i, or (long)i for a signed 4-byte i. */
static Sym *iv_base(Node *x)
{
    if (x->kind == N_VAR && trackable(x->sym) && is_int(x->ty) && x->ty->size >= 4) {
        return x->sym;
    }
    if (x->kind == N_CAST && x->l->kind == N_VAR && trackable(x->l->sym) && is_int(x->l->ty) &&
        !x->l->ty->uns && x->l->ty->size == 4 && is_int(x->ty) && x->ty->size == 8) {
        return x->l->sym;
    }
    return NULL;
}

/* Callback: looks for  i * c,  i << c,  i + inv  or  inv + i  in the loop. */
static int iv_cb(Node *n, void *ud)
{
    (void)ud;
    if (iv_c.found || n->kind != N_BIN) {
        return 1;
    }
    Sym *v = iv_base(n->l);
    long long c = 0;
    int kind = 0; /* 1: multiply, 2: add */
    if (v && n->r->kind == N_NUM && n->op == B_MUL) {
        kind = 1;
        c = n->r->val;
    } else if (v && n->r->kind == N_NUM && n->op == B_SHL && n->r->val >= 0 && n->r->val < 31) {
        kind = 1;
        c = 1LL << n->r->val;
    } else if (n->op == B_ADD) {
        Sym *w = iv_base(n->l);
        Node *other = n->r;
        if (!w) {
            w = iv_base(n->r);
            other = n->l;
        }
        v = w;
        if (v && gc_ok(other) && loop_invariant(iv_c.lp, other) && n->ty->size == 8 &&
            (is_ptr(n->ty) || is_int(n->ty)) && n->l->ty->size == 8 && n->r->ty->size == 8) {
            kind = 2;
            c = 1;
        }
    }
    if (!kind || !v || v->kind != SY_VAR) {
        return 1;
    }
    if (!(is_int(n->ty) || is_ptr(n->ty)) || n->ty->size < 4) {
        return 1;
    }
    iv_c.found = n;
    iv_c.iv = v;
    iv_c.step = c; /* multiplied by the step of the variable later */
    return 0;
}

/* Callback: replaces the occurrences of the derived value by its temporary. */
static int iv_rep_cb(Node *n, void *ud)
{
    if (n->kind == N_BIN && same_tree(n, iv_c.found)) {
        rep(n, copy_node(ud));
        return 0;
    }
    return 1;
}

/* Induction-variable optimization (strength reduction): a value derived from the counter of a
 * loop, such as i * 8 or p + i, becomes a variable that is initialised before the loop and
 * advanced next to the update of the counter.  Returns 1 if it changed the tree. */
static int iv_transform(void)
{
    for (int li = 0; li < nloops; li++) {
        Loop *lp = &loops[li];
        if (!lp->ok) {
            continue;
        }
        for (int b = 0; b < nbl; b++) {
            if (!bit_has(lp->in, b) || !g_reach[b]) {
                continue;
            }
            for (int a = 0; a < bl[b].natoms; a++) {
                Atom *at = &bl[b].atoms[a];
                if (at->kind == A_JUMP || !*at->slot) {
                    continue;
                }
                Sym *K[65];
                int nK = atom_assigns(at, K);
                iv_c.lp = lp;
                iv_c.found = NULL;
                gc_walk(*at->slot, K, nK, iv_cb, NULL);
                if (!iv_c.found) {
                    continue;
                }
                IvUpdate up = {0};
                if (!iv_find_update(lp, iv_c.iv, &up) || up.atom->stmt == NULL) {
                    iv_c.found = NULL;
                    continue;
                }
                /* the update must belong to this loop, not to a loop inside it */
                int ub = -1;
                for (int xb = 0; xb < nbl && ub < 0; xb++) {
                    for (int xa = 0; xa < bl[xb].natoms; xa++) {
                        if (&bl[xb].atoms[xa] == up.atom) {
                            ub = xb;
                        }
                    }
                }
                if (ub < 0 || loop_of(ub) != lp) {
                    iv_c.found = NULL;
                    continue;
                }
                Node *d = iv_c.found;
                long long k = up.step * iv_c.step;
                Type *tt = d->ty;
                Type *kt = is_ptr(tt) ? ty_long : tt;
                Node *init = clone_expr(d);
                int dline = d->line;
                iv_c.found = init; /* d itself is rewritten below */
                Node *t = new_temp(tt, dline);
                for (int lb = 0; lb < nbl; lb++) {
                    if (!bit_has(lp->in, lb) || !g_reach[lb]) {
                        continue;
                    }
                    for (int la = 0; la < bl[lb].natoms; la++) {
                        Atom *u = &bl[lb].atoms[la];
                        Sym *K2[65];
                        int nK2 = atom_assigns(u, K2);
                        if (u->kind != A_JUMP && *u->slot) {
                            gc_walk(*u->slot, K2, nK2, iv_rep_cb, t);
                        }
                    }
                }
                /* t = t + k  right after the update of the counter */
                Node *sum = new_node(N_BIN, tt, dline);
                sum->op = B_ADD;
                sum->l = copy_node(t);
                sum->r = num(k, kt, dline);
                sum->cmpty_uns = 1;
                Node *as = new_node(N_ASSIGN, tt, dline);
                as->l = copy_node(t);
                as->r = sum;
                if (up.atom->kind == A_EXPR && up.slotp == up.atom->slot) {
                    Node *st = new_node(S_EXPR, NULL, dline);
                    st->l = as;
                    insert_after_stmt(up.atom->stmt, st);
                } else {
                    Node *cm = new_node(N_COMMA, ty_int, dline);
                    cm->l = *up.slotp;
                    cm->r = as;
                    *up.slotp = cm;
                }
                insert_before_loop(lp->stmt, assign_stmt(copy_node(t), init, dline));
                return 1;
            }
        }
    }
    return 0;
}

/* Runs the analyses and transformations that need loops and expression availability. */
static void cfg_phase2(Sym *f)
{
    int do_loops = (fl & FCO_LOOPS) && (fl & (FCO_LICM | FCO_IV));
    int do_avail = (fl & FCO_AVAIL) && (fl & (FCO_GCSE | FCO_PRE));
    if (!use_flow || !(do_loops || do_avail)) {
        return;
    }
    for (int round = 0; round < 24; round++) {
        int moved = 0;
        if (!cfg_build(f->body)) {
            break;
        }
        if (graph_build()) {
            if (do_loops) {
                loops_find();
                if (fl & FCO_IV) {
                    moved = iv_transform();
                }
                if (!moved && (fl & FCO_LICM)) {
                    moved = licm_transform((fl & FCO_RD) != 0, f->body);
                }
            }
            if (!moved && do_avail && avail_prepare() && avail_solve()) {
                if (fl & FCO_PRE) {
                    moved = pre_insert();
                }
                if (!moved && (fl & FCO_GCSE)) {
                    moved = gcse_apply();
                }
                avail_release();
            }
        }
        graph_free();
        cfg_free();
        if (!moved) {
            break;
        }
    }
    simp_list(&f->body->l);
}

/* ------------------------------------------------------------------ */
/* driver                                                              */
/* ------------------------------------------------------------------ */

/* Runs the passes that work on the control-flow graph. */
static void cfg_phase(Sym *f)
{
    uint64_t value_passes = FCO_GCONST | FCO_GCOPY | FCO_CCP | FCO_UBLOCK;
    for (int round = 0; round < 3; round++) {
        changed = 0;
        if (fl & value_passes) {
            if (cfg_build(f->body) && cfg_solve()) {
                cfg_apply();
            }
            cfg_free();
        }
        if (fl & (FCO_THREAD | FCO_MERGE)) {
            cfg_threading(f->body, (fl & FCO_THREAD) != 0, (fl & FCO_MERGE) != 0);
        }
        simp_list(&f->body->l);
        if (!changed) {
            break;
        }
    }
    if ((fl & FCO_GDCE) && (fl & FCO_LIVE) && use_flow) {
        for (int round = 0; round < 3; round++) {
            changed = 0;
            if (cfg_build(f->body)) {
                cfg_live_dce(f->body);
            }
            cfg_free();
            simp_list(&f->body->l);
            if (!changed) {
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* function inlining                                                   */
/* ------------------------------------------------------------------ */
enum { INL_MAXSIZE = 40, INL_MAXPARAMS = 6, INL_DEPTH = 3 };

typedef struct InlInfo {
    Sym *ps[INL_MAXPARAMS];
    int np;
    Node *expr; /* the returned expression of the callee */
    int bad;
    Sym *self;
} InlInfo;

/* Walker callback: rejects bodies that cannot be substituted as an expression. */
static void inl_check_cb(Node *n, void *ud)
{
    InlInfo *in = ud;
    switch (n->kind) {
    case N_ADDR:
    case N_STR:
    case N_FUNCADDR:
    case N_ASSIGN:
    case N_ASSIGNOP:
    case N_PREINC:
    case N_POSTINC:
    case N_PREDEC:
    case N_POSTDEC:
        if (n->kind == N_ADDR || n->l == NULL || n->l->kind != N_VAR || n->l->sym->is_param ||
            n->l->sym->global == 0) {
            in->bad = 1; /* only stores to globals are kept simple; parameters stay unchanged */
        }
        break;
    case N_CALL:
        if (n->sym == in->self || !n->sym) {
            in->bad = 1;
        }
        break;
    case N_VAR:
        if (n->sym->kind == SY_VAR && !n->sym->global && !n->sym->is_param) {
            in->bad = 1; /* a local of the callee */
        }
        break;
    default:
        break;
    }
}

/* Decides whether function g can be inlined; fills in. */
static int inl_eligible(Sym *g, InlInfo *in)
{
    memset(in, 0, sizeof *in);
    if (!g->body || g->body->kind != S_BLOCK || g->ty->variadic || g->ty->noproto ||
        g->ty->ret->kind == TY_VOID || !scalar_ty(g->ty->ret)) {
        return 0;
    }
    Node *ret = NULL;
    for (Node *st = g->body->l; st; st = st->next) {
        if (st->kind == S_NOP) {
            continue;
        }
        if (st->kind != S_RETURN || ret || !st->l) {
            return 0;
        }
        ret = st;
    }
    if (!ret || gc_size(ret->l) > INL_MAXSIZE) {
        return 0;
    }
    for (int i = 0; i < g->ndbg; i++) {
        Sym *v = g->dbgvars[i];
        if (!v->is_param) {
            return 0;
        }
        if (in->np == INL_MAXPARAMS || !scalar_ty(v->ty)) {
            return 0;
        }
        in->ps[in->np++] = v;
    }
    int nparams = 0;
    for (Param *pp = g->ty->params; pp; pp = pp->next) {
        nparams++;
    }
    if (nparams != in->np) {
        return 0;
    }
    in->self = g;
    in->expr = ret->l;
    walk(in->expr, inl_check_cb, in);
    return !in->bad;
}

static struct {
    Sym *ps[INL_MAXPARAMS];
    Node *with[INL_MAXPARAMS];
    int np;
} inl_s;

/* Walker callback: replaces the parameters by their values. */
static void inl_subst_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind != N_VAR) {
        return;
    }
    for (int i = 0; i < inl_s.np; i++) {
        if (n->sym == inl_s.ps[i]) {
            rep(n, clone_expr(inl_s.with[i]));
            return;
        }
    }
}

static int inl_count;

/* Counts the uses of a parameter. */
static void inl_use_cb(Node *n, void *ud)
{
    if (n->kind == N_VAR && n->sym == ud) {
        inl_count++;
    }
}

/* Returns the expression that stands for a call of g, or NULL. */
static Node *inl_expand(Node *call, InlInfo *in)
{
    Node *args[INL_MAXPARAMS];
    int na = 0;
    for (Node *a = call->r; a; a = a->next) {
        if (na == INL_MAXPARAMS) {
            return NULL;
        }
        args[na++] = a;
    }
    if (na != in->np) {
        return NULL;
    }
    for (int i = 0; i < na; i++) {
        if (!same_rep(args[i]->ty, in->ps[i]->ty) || args[i]->ty->kind != in->ps[i]->ty->kind) {
            return NULL;
        }
    }
    Node *body = clone_expr(in->expr);
    Node *pre[INL_MAXPARAMS];
    int npre = 0;
    inl_s.np = in->np;
    for (int i = 0; i < in->np; i++) {
        inl_count = 0;
        walk(in->expr, inl_use_cb, in->ps[i]);
        int uses = inl_count;
        Node *a = args[i];
        int simple = a->kind == N_NUM || a->kind == N_FNUM ||
                     (a->kind == N_VAR && trackable(a->sym));
        int cheap = simple || (uses <= 1 && gc_ok(a));
        inl_s.ps[i] = in->ps[i];
        if (uses == 0 && !gc_ok(a)) { /* the argument is still evaluated */
            cheap = 0;
        }
        if (cheap) {
            inl_s.with[i] = a;
        } else {
            Node *t = new_temp(in->ps[i]->ty, call->line);
            Node *as = new_node(N_ASSIGN, t->ty, call->line);
            as->l = copy_node(t);
            as->r = clone_expr(a);
            pre[npre++] = as;
            inl_s.with[i] = t;
        }
    }
    walk(body, inl_subst_cb, NULL);
    Node *result = body;
    if (result->ty != call->ty && !same_rep(result->ty, call->ty)) {
        Node *cv = new_node(N_CAST, call->ty, call->line);
        cv->l = result;
        result = cv;
    }
    for (int i = npre - 1; i >= 0; i--) { /* arguments are evaluated from the last to the first */
        Node *cm = new_node(N_COMMA, call->ty, call->line);
        cm->l = pre[i];
        cm->r = result;
        result = cm;
    }
    return result;
}

static int inl_done;

/* Walker callback: expands the calls of small functions. */
static void inl_cb(Node *n, void *ud)
{
    (void)ud;
    if (n->kind != N_CALL || !n->sym || n->sym == cur_fn || n->sym->kind != SY_FUNC) {
        return;
    }
    InlInfo in;
    if (!inl_eligible(n->sym, &in)) {
        return;
    }
    Node *e = inl_expand(n, &in);
    if (e) {
        rep(n, e);
        inl_done++;
    }
}

/* Expands the calls of small functions of the form  { return expression; }  in place. */
static void inline_calls(Sym *f)
{
    for (int round = 0; round < INL_DEPTH; round++) {
        inl_done = 0;
        walk(f->body, inl_cb, NULL);
        if (!inl_done) {
            break;
        }
    }
}

/* Optimizes the body of function f.  debugged says that debug information is recorded for it. */
void opt_function(Sym *f, int debugged)
{
    fl = CX->opt.opt_flags;
    jumps = CX->opt.jump_opt;
    if (!(fl || jumps) || !f->body) {
        return;
    }
    cur_fn = f;
    use_flow = 1;
    has_goto = no_cfg = 0;
    int volatile_fn = PG.uses_volatile && !f->from_rt;
    if (debugged) { /* keep the statements and variable values the source shows */
        fl &= FCO_FOLD | FCO_ALGEBRA | FCO_COMPARE | FCO_CASTS | FCO_CANON | FCO_STRENGTH | FCO_BOOL;
        jumps = 0;
    }
    if ((fl & FCO_INLINE) && !debugged && !PG.uses_volatile) {
        inline_calls(f);
    }
    walk(f->body, scan_node, NULL);
    if (volatile_fn) {
        use_flow = 0;
        fl &= ~(uint64_t)FCO_CSE;
    }
    if (!no_cfg && use_flow && (fl & FCO_TAILREC)) {
        tail_recursion(f);
        walk(f->body, scan_node, NULL);
    }
    uint64_t all = fl;
    if (!use_flow || has_goto) { /* the tree walking data-flow passes need structured code */
        fl &= ~(uint64_t)(FCO_CONSTPROP | FCO_COPYPROP | FCO_DEADSTORE);
    }
    for (int pass = 0; pass < 3; pass++) {
        changed = 0;
        Env e;
        e.n = 0;
        if (use_flow && !has_goto) {
            flow_stmt(f->body, &e);
        }
        simp_list(&f->body->l);
        if (!changed) {
            break;
        }
    }
    fl = all;
    if ((fl & FCO_CFG) && !no_cfg) {
        cfg_phase(f);
        if (!has_goto) {
            cfg_phase2(f);
        }
    }
    if (!use_flow || has_goto) {
        fl &= ~(uint64_t)(FCO_CONSTPROP | FCO_COPYPROP | FCO_DEADSTORE);
    }
    if (fl & FCO_DEADSTORE) {
        nrd = 0;
        count_reads(f->body);
        walk(f->body, drop_unread_cb, NULL);
        if (!has_goto) {
            dead_list(f->body->l, 1);
        }
        simp_list(&f->body->l);
    }
    if (fl & FCO_CSE) {
        walk(f->body, cse_cb, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* register allocation and tail-call eligibility                       */
/* ------------------------------------------------------------------ */
enum { RA_MAXVARS = 256, RA_NREGS = 5 };

static struct RaVar {
    Sym *sym;
    long weight;
} ra_vars[RA_MAXVARS];
static int ra_n, ra_setjmp, ra_unsafe_frame;

/* Returns true for a local that can live in a register: an integer or pointer whose address is
 * never taken. */
static int reg_candidate(Sym *s)
{
    return s && s->kind == SY_VAR && !s->global && !s->esc && (is_int(s->ty) || is_ptr(s->ty));
}

/* Adds weight to a variable's use count. */
static void ra_note(Sym *s, long w)
{
    for (int i = 0; i < ra_n; i++) {
        if (ra_vars[i].sym == s) {
            ra_vars[i].weight += w;
            return;
        }
    }
    if (ra_n < RA_MAXVARS) {
        ra_vars[ra_n].sym = s;
        ra_vars[ra_n++].weight = w;
    }
}

/* Walks the tree counting variable uses, weighted by loop depth (a use in a loop counts 8 times
 * as much per level), and noting what rules out register variables or frame reuse. */
static void ra_walk(Node *n, int depth)
{
    if (!n) {
        return;
    }
    switch (n->kind) {
    case N_ADDR:
        mark_escape(n->l);
        break;
    case N_VAR:
        if (n->sym && n->sym->kind == SY_VAR && !n->sym->global && !scalar_ty(n->sym->ty)) {
            ra_unsafe_frame = 1;
        }
        ra_note(n->sym, 1L << (3 * (depth > 3 ? 3 : depth)));
        break;
    case S_ZERO:
        ra_unsafe_frame = 1;
        break;
    case N_CALL:
        if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__setjmp")) {
            ra_setjmp = 1;
        }
        break;
    default:
        break;
    }
    int inner = n->kind == S_WHILE || n->kind == S_DO || n->kind == S_FOR;
    Node *kids[4] = {n->l, n->r, n->c, n->d};
    for (int i = 0; i < 4; i++) {
        if (i == 1 && (n->kind == S_CASE || n->kind == S_DEFAULT)) {
            continue;
        }
        int d = depth + ((inner && i != 3) ? 1 : 0);
        for (Node *k = kids[i]; k; k = k->next) {
            ra_walk(k, d);
        }
    }
}

/* Reserves a frame slot for saving a callee-saved register. */
static int ra_slot(Sym *f)
{
    f->frame = (f->frame + 8 + 7) & ~7;
    return -f->frame;
}

/* Chooses the variables that live in the callee-saved registers rbx, r12-r15, and decides whether
 * the frame of the function can be dropped by a tail call (all variables are scalars whose address
 * is never taken).  The code generator saves and restores the registers in prologue and epilogue. */
void opt_regalloc(Sym *f, int debugged)
{
    uint64_t flags = CX->opt.opt_flags;
    f->regmask = 0;
    f->tail_ok = 0;
    if (!f->body || !(flags & (FCO_REGALLOC | FCO_TAILCALL)) || debugged) {
        return;
    }
    ra_n = ra_setjmp = ra_unsafe_frame = 0;
    for (int i = 0; i < f->ndbg; i++) {
        ra_note(f->dbgvars[i], 0);
    }
    ra_walk(f->body, 0);
    int params_ok = 1;
    for (Param *p = f->ty->params; p; p = p->next) {
        if (!scalar_ty(p->ty)) {
            params_ok = 0;
        }
    }
    int any_esc = 0;
    for (int i = 0; i < ra_n; i++) {
        if (ra_vars[i].sym->esc) {
            any_esc = 1;
        }
    }
    if (!ra_setjmp && !ra_unsafe_frame && !any_esc && params_ok && !f->ty->variadic) {
        f->tail_ok = 1;
    }
    if (!(flags & FCO_REGALLOC)) {
        return;
    }
    if (ra_setjmp) { /* a longjmp can leave the registers changed: keep the caller's values */
        for (int r = 1; r <= RA_NREGS; r++) {
            f->regmask |= 1 << (r - 1);
            f->rsave[r - 1] = ra_slot(f);
        }
        return;
    }
    if (PG.uses_volatile && !f->from_rt) {
        return;
    }
    int chosen = 0;
    while (chosen < RA_NREGS) { /* pick the heaviest candidate each time */
        int best = -1;
        for (int i = 0; i < ra_n; i++) {
            Sym *s = ra_vars[i].sym;
            if (reg_candidate(s) && !s->reg && ra_vars[i].weight >= 6 &&
                (best < 0 || ra_vars[i].weight > ra_vars[best].weight)) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        ra_vars[best].sym->reg = ++chosen;
        f->regmask |= 1 << (chosen - 1);
        f->rsave[chosen - 1] = ra_slot(f);
    }
}
