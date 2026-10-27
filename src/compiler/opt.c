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
static unsigned fl;      /* FCO_* passes enabled for this function */
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

typedef struct Atom {
    int kind;
    Node *stmt;
    Node **slot;
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
static int cfg_transfer(Block *b, Env *env, unsigned analysis_flags)
{
    unsigned saved = fl;
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
    unsigned aflags = fl | FCO_CONSTPROP | FCO_COPYPROP | FCO_FOLD | FCO_ALGEBRA | FCO_COMPARE |
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
    unsigned saved = fl;
    fl &= ~(unsigned)(FCO_CONSTPROP | FCO_COPYPROP);
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

/* Removes the assignments whose value is never used, using the live variables at each point. */
static void cfg_live_dce(Node *body)
{
    nlv = lv_over = 0;
    walk(body, lv_collect_cb, NULL);
    if (lv_over || nlv == 0) {
        return;
    }
    int w = lv_words = (nlv + 63) / 64;
    uint64_t *in = calloc((size_t)nbl * w, sizeof *in), *out = calloc((size_t)nbl * w, sizeof *out);
    uint64_t tmp[MAXLIVE / 64];
    int again = 1;
    for (int round = 0; again && round < 200; round++) {
        again = 0;
        for (int id = nbl - 1; id >= 0; id--) {
            Block *b = &bl[id];
            uint64_t *o = out + (size_t)id * w;
            for (int k = 0; k < w; k++) {
                o[k] = 0;
            }
            for (int k = 0; k < succ_count(b); k++) {
                uint64_t *si = in + (size_t)succ_at(b, k) * w;
                for (int j = 0; j < w; j++) {
                    o[j] |= si[j];
                }
            }
            memcpy(tmp, o, w * sizeof *tmp);
            for (int i = b->natoms - 1; i >= 0; i--) {
                lv_atom(tmp, &b->atoms[i]);
            }
            uint64_t *bi = in + (size_t)id * w;
            if (memcmp(bi, tmp, w * sizeof *tmp)) {
                memcpy(bi, tmp, w * sizeof *tmp);
                again = 1;
            }
        }
    }
    if (!again) { /* only trust a settled result */
        for (int id = 0; id < nbl; id++) {
            Block *b = &bl[id];
            memcpy(tmp, out + (size_t)id * w, w * sizeof *tmp);
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
    }
    free(in);
    free(out);
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
/* driver                                                              */
/* ------------------------------------------------------------------ */

/* Runs the passes that work on the control-flow graph. */
static void cfg_phase(Sym *f)
{
    unsigned value_passes = FCO_GCONST | FCO_GCOPY | FCO_CCP | FCO_UBLOCK;
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
    walk(f->body, scan_node, NULL);
    if (volatile_fn) {
        use_flow = 0;
        fl &= ~(unsigned)FCO_CSE;
    }
    if (!no_cfg && use_flow && (fl & FCO_TAILREC)) {
        tail_recursion(f);
        walk(f->body, scan_node, NULL);
    }
    unsigned all = fl;
    if (!use_flow || has_goto) { /* the tree walking data-flow passes need structured code */
        fl &= ~(unsigned)(FCO_CONSTPROP | FCO_COPYPROP | FCO_DEADSTORE);
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
    }
    if (!use_flow || has_goto) {
        fl &= ~(unsigned)(FCO_CONSTPROP | FCO_COPYPROP | FCO_DEADSTORE);
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
