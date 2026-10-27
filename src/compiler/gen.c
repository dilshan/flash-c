/*
 * File:    gen.c
 * Purpose: Flash C - x86-64 code generator and ELF64 writer
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "fci.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static uint8_t *code;
static int ncode, capcode;

typedef struct CFix {
    int pos;
    Sym *sym;
    long long add;
    int kind;
} CFix; /* kind 0 data off, 1 var, 2 func, 3 rel32 func */

static CFix *cfix;
static int ncfix, capcfix;
static int *lpos;
static int nlabels, caplabels;

typedef struct JFix {
    int pos, label, rel8;
} JFix; /* pos: offset of the displacement field; rel8: shortened by -O */

static JFix *jfix;
static int njfix, capjfix;

typedef struct GL {
    char *name;
    int label;
} GL;

static GL *gls;
static int ngl, capgl;
static int brk_label[64], cont_label[64], ctl_sp;
static int ret_label;
static Sym *gen_fn;
static char gerr[256];
static int gerr_set;

#define BASE 0x400000
#define HDRSZ 176 /* ELF header + two program headers */

/* Emits one byte of machine code. */
static void b1(int x)
{
    if (ncode == capcode) {
        capcode = capcode ? capcode * 2 : 65536;
        code = realloc(code, capcode);
    }
    code[ncode++] = (uint8_t)x;
}

/* Emits two bytes of machine code. */
static void b2(int x, int y)
{
    b1(x);
    b1(y);
}

/* Emits three bytes of machine code. */
static void b3(int x, int y, int z)
{
    b1(x);
    b1(y);
    b1(z);
}

/* Emits four bytes of machine code. */
static void b4(int x, int y, int z, int w)
{
    b1(x);
    b1(y);
    b1(z);
    b1(w);
}

/* Emits a 32-bit little-endian value. */
static void d32(int v)
{
    for (int i = 0; i < 4; i++) {
        b1((v >> (8 * i)) & 0xff);
    }
}

/* Emits a 64-bit little-endian value. */
static void d64(long long v)
{
    for (int i = 0; i < 8; i++) {
        b1((int)((v >> (8 * i)) & 0xff));
    }
}

/*
 * Records a fix-up at the current code position: kind says what to patch in (symbol address,
 * function address, ...), s is the target and add an offset.  Resolved when the layout is known.
 */
static void add_cfix(int kind, Sym *s, long long add)
{
    if (ncfix == capcfix) {
        capcfix = capcfix ? capcfix * 2 : 1024;
        cfix = realloc(cfix, capcfix * sizeof *cfix);
    }
    cfix[ncfix].pos = ncode;
    cfix[ncfix].sym = s;
    cfix[ncfix].add = add;
    cfix[ncfix].kind = kind;
    ncfix++;
    d32(0);
}

/* Allocates a new code label (its position is set later by place()). */
static int newlabel(void)
{
    if (nlabels == caplabels) {
        caplabels = caplabels ? caplabels * 2 : 256;
        lpos = realloc(lpos, caplabels * sizeof(int));
    }
    lpos[nlabels] = -1;
    return nlabels++;
}

static int ndln;
static uint32_t dln_last_pos(void);
static void dln_set_last_pos(int pos);
/* -O / -Z bookkeeping: what was emitted last, and where a label or debug line mark pins the code */
static int mark_pos = -1;                  /* ncode at the last label placement or line mark */
static int last_push = -1;                 /* ncode right after the last 'push rax' */
static int pl_pos = -1, pl_n, pl_list[32]; /* labels placed at pl_pos */

/*
 * Binds label l to the current code position.  A jump that would only skip to this very
 * position is removed first when the jump optimisation is on.
 */
static void place(int l)
{
    if (CX->opt.jump_opt && njfix && jfix[njfix - 1].label == l &&
        jfix[njfix - 1].pos == ncode - 4 && ncode >= 5 && code[ncode - 5] == 0xE9 &&
        !(pl_pos == ncode && pl_n >= 32)) {
        /* the previous instruction is 'jmp l' and l is placed right behind it: drop the jump */
        if (pl_pos == ncode) {
            for (int i = 0; i < pl_n; i++) {
                lpos[pl_list[i]] = ncode - 5;
            }
        }
        if (ndln && dln_last_pos() == (uint32_t)ncode) {
            dln_set_last_pos(ncode - 5);
        }
        ncode -= 5;
        njfix--;
        pl_pos = -1;
        last_push = -1;
    }
    if (pl_pos != ncode) {
        pl_pos = ncode;
        pl_n = 0;
    }
    if (pl_n < 32) {
        pl_list[pl_n++] = l;
    }
    lpos[l] = ncode;
    mark_pos = ncode;
}

static void jump_to(int l, int cond) /* cond < 0: jmp; else 0F 8x */
{
    if (cond < 0) {
        b1(0xE9);
    } else {
        b2(0x0F, 0x80 + cond);
    }
    if (njfix == capjfix) {
        capjfix = capjfix ? capjfix * 2 : 256;
        jfix = realloc(jfix, capjfix * sizeof *jfix);
    }
    jfix[njfix].pos = ncode;
    jfix[njfix].label = l;
    jfix[njfix].rel8 = 0;
    njfix++;
    d32(0);
}

enum {
    CC_E = 4,
    CC_NE = 5,
    CC_B = 2,
    CC_AE = 3,
    CC_BE = 6,
    CC_A = 7,
    CC_L = 0xC,
    CC_GE = 0xD,
    CC_LE = 0xE,
    CC_G = 0xF
};

/* ---------- basic emitters ---------- */
static void push_rax(void)
{
    b1(0x50);
    last_push = ncode;
}

static int peep_push(void) /* -Z: the previous instruction is 'push rax' and nothing is pinned to
                              the code behind it */
{
    if (!CX->opt.redundant_loads || last_push != ncode || mark_pos == ncode) {
        return 0;
    }
    ncode--;
    last_push = -1;
    return 1;
}

/* Emits pop rax, unless it cancels an immediately preceding push rax. */
static void pop_rax(void)
{
    if (!peep_push()) {
        b1(0x58);
    }
}

/* Emits pop rcx; after a push rax this becomes the shorter mov rcx, rax. */
static void pop_rcx(void)
{
    if (peep_push()) {
        b3(0x48, 0x89, 0xC1);
    } else {
        b1(0x59);
    }
} /* push rax; pop rcx  ->  mov rcx, rax */

/* Emits mov rcx, rax. */
static void mov_rcx_rax(void)
{
    b3(0x48, 0x89, 0xC1);
}

/* Emits test rax, rax. */
static void test_rax(void)
{
    b3(0x48, 0x85, 0xC0);
}

/* Emits code that loads the constant v into rax, using the shortest encoding. */
static void load_imm(long long v)
{
    if (v == 0) {
        b2(0x31, 0xC0); /* xor eax,eax */
    } else if (v > 0 && v <= 0xffffffffLL) {
        b1(0xB8);
        d32((int)v);
    } else if (v >= -0x80000000LL && v < 0) {
        b3(0x48, 0xC7, 0xC0);
        d32((int)v);
    } else {
        b2(0x48, 0xB8);
        d64(v);
    }
}

/* Emits code that loads the constant v into rcx, using the shortest encoding. */
static void load_imm_rcx(long long v)
{
    if (v == 0) {
        b2(0x31, 0xC9); /* xor ecx,ecx */
    } else if (v > 0 && v <= 0xffffffffLL) {
        b1(0xB9);
        d32((int)v);
    } else if (v >= -0x80000000LL && v < 0) {
        b3(0x48, 0xC7, 0xC1);
        d32((int)v);
    } else {
        b2(0x48, 0xB9);
        d64(v);
    }
}

/* ---- SSE2 helpers: a floating value lives in rax as its double bit pattern ---- */
static void movq_x0_rax(void)
{
    b1(0x66);
    b4(0x48, 0x0F, 0x6E, 0xC0);
}

/* Emits movq xmm1, rcx. */
static void movq_x1_rcx(void)
{
    b1(0x66);
    b4(0x48, 0x0F, 0x6E, 0xC9);
}

/* Emits movq rax, xmm0. */
static void movq_rax_x0(void)
{
    b1(0x66);
    b4(0x48, 0x0F, 0x7E, 0xC0);
}

/* Emits the conversion of xmm0 from double to single precision. */
static void cvt_sd2ss(void)
{
    b4(0xF2, 0x0F, 0x5A, 0xC0);
}

/* Emits the conversion of xmm0 from single to double precision. */
static void cvt_ss2sd(void)
{
    b4(0xF3, 0x0F, 0x5A, 0xC0);
}

static void gen_fop(int op) /* rax op rcx, both doubles -> rax */
{
    movq_x0_rax();
    movq_x1_rcx();
    switch (op) {
    case B_ADD:
        b4(0xF2, 0x0F, 0x58, 0xC1);
        break;
    case B_SUB:
        b4(0xF2, 0x0F, 0x5C, 0xC1);
        break;
    case B_MUL:
        b4(0xF2, 0x0F, 0x59, 0xC1);
        break;
    case B_DIV:
        b4(0xF2, 0x0F, 0x5E, 0xC1);
        break;
    }
    movq_rax_x0();
}

/* Compares the doubles in rax (left) and rcx (right) for operator op and leaves 0 or 1 in rax. */
static void gen_fcmp(int op)
{
    movq_x0_rax();
    movq_x1_rcx();
    switch (op) {
    case B_EQ:
        b1(0x66);
        b3(0x0F, 0x2E, 0xC1);
        b3(0x0F, 0x94, 0xC0);
        b3(0x0F, 0x9B, 0xC1);
        b2(0x20, 0xC8);
        break;
    case B_NE:
        b1(0x66);
        b3(0x0F, 0x2E, 0xC1);
        b3(0x0F, 0x95, 0xC0);
        b3(0x0F, 0x9A, 0xC1);
        b2(0x08, 0xC8);
        break;
    case B_GT:
        b1(0x66);
        b3(0x0F, 0x2E, 0xC1);
        b3(0x0F, 0x97, 0xC0);
        break;
    case B_GE:
        b1(0x66);
        b3(0x0F, 0x2E, 0xC1);
        b3(0x0F, 0x93, 0xC0);
        break;
    case B_LT:
        b1(0x66);
        b3(0x0F, 0x2E, 0xC8);
        b3(0x0F, 0x97, 0xC0);
        break;
    case B_LE:
        b1(0x66);
        b3(0x0F, 0x2E, 0xC8);
        b3(0x0F, 0x93, 0xC0);
        break;
    }
    b3(0x0F, 0xB6, 0xC0);
}

/* Returns true for types that are handled by address (arrays, structs, unions, functions). */
static int is_mem_type(Type *t)
{
    return t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_UNION || t->kind == TY_FUNC;
}

/* rax = *(type*)rax */
static void load_ind(Type *t)
{
    if (is_mem_type(t)) {
        return;
    }
    if (t->kind == TY_FLOAT) {
        b4(0xF3, 0x0F, 0x10, 0x00);
        cvt_ss2sd();
        movq_rax_x0();
        return;
    }
    switch (t->size) {
    case 1:
        if (t->uns) {
            b3(0x0F, 0xB6, 0x00);
        } else {
            b4(0x48, 0x0F, 0xBE, 0x00);
        }
        break;
    case 2:
        if (t->uns) {
            b3(0x0F, 0xB7, 0x00);
        } else {
            b4(0x48, 0x0F, 0xBF, 0x00);
        }
        break;
    case 4:
        if (t->uns) {
            b2(0x8B, 0x00);
        } else {
            b3(0x48, 0x63, 0x00);
        }
        break;
    default:
        b3(0x48, 0x8B, 0x00);
    }
}

/* rax = local at [rbp+off] */
static void load_local(int off, Type *t)
{
    if (t->kind == TY_FLOAT) {
        b4(0xF3, 0x0F, 0x10, 0x85);
        d32(off);
        cvt_ss2sd();
        movq_rax_x0();
        return;
    }
    switch (t->size) {
    case 1:
        if (t->uns) {
            b3(0x0F, 0xB6, 0x85);
        } else {
            b4(0x48, 0x0F, 0xBE, 0x85);
        }
        break;
    case 2:
        if (t->uns) {
            b3(0x0F, 0xB7, 0x85);
        } else {
            b4(0x48, 0x0F, 0xBF, 0x85);
        }
        break;
    case 4:
        if (t->uns) {
            b2(0x8B, 0x85);
        } else {
            b3(0x48, 0x63, 0x85);
        }
        break;
    default:
        b3(0x48, 0x8B, 0x85);
    }
    d32(off);
}

/* *(type*)rcx = rax */
static void store_ind(Type *t)
{
    if (t->kind == TY_FLOAT) {
        movq_x0_rax();
        cvt_sd2ss();
        b4(0xF3, 0x0F, 0x11, 0x01);
        cvt_ss2sd();
        movq_rax_x0();
        return;
    }
    switch (t->size) {
    case 1:
        b2(0x88, 0x01);
        break;
    case 2:
        b3(0x66, 0x89, 0x01);
        break;
    case 4:
        b2(0x89, 0x01);
        break;
    default:
        b3(0x48, 0x89, 0x01);
    }
}

/* Stores rax into the local variable at rbp offset off, using the width of type t. */
static void store_local(int off, Type *t)
{
    if (t->kind == TY_FLOAT) {
        movq_x0_rax();
        cvt_sd2ss();
        b4(0xF3, 0x0F, 0x11, 0x85);
        d32(off);
        cvt_ss2sd();
        movq_rax_x0();
        return;
    }
    switch (t->size) {
    case 1:
        b2(0x88, 0x85);
        break;
    case 2:
        b3(0x66, 0x89, 0x85);
        break;
    case 4:
        b2(0x89, 0x85);
        break;
    default:
        b3(0x48, 0x89, 0x85);
    }
    d32(off);
}

/* ---------- bit fields: a field lives in a 32-bit unit at the member offset ---------- */
static int is_bf(Node *n)
{
    return n->kind == N_MEMBER && n->bw > 0;
}

static void bf_extract(Node *m) /* rax = unit value -> rax = field value */
{
    b3(0x48, 0xC1, 0xE0);
    b1((uint8_t)(64 - m->bsh - m->bw)); /* shl rax,64-sh-w */
    b3(0x48, 0xC1, m->buns ? 0xE8 : 0xF8);
    b1((uint8_t)(64 - m->bw)); /* shr / sar rax,64-w */
}

static void bf_load(Node *m) /* rax = address of the unit -> rax = field value */
{
    b2(0x8B, 0x00); /* mov eax,[rax] */
    bf_extract(m);
}

static void bf_store(Node *m) /* rax = value, rcx = address of the unit -> rax = stored value */
{
    uint32_t mask = (m->bw == 32 ? 0xFFFFFFFFu : ((1u << m->bw) - 1)) << m->bsh;
    b3(0x48, 0x89, 0xC2); /* mov rdx,rax */
    b3(0x48, 0xC1, 0xE2);
    b1((uint8_t)(64 - m->bw)); /* shl rdx,64-w */
    b3(0x48, 0xC1, 0xEA);
    b1((uint8_t)(64 - m->bw)); /* shr rdx,64-w */
    if (m->bsh) {
        b3(0x48, 0xC1, 0xE2);
        b1((uint8_t)m->bsh);
    } /* shl rdx,sh */
    b2(0x8B, 0x31); /* mov esi,[rcx] */
    b2(0x81, 0xE6);
    d32((int)~mask); /* and esi,~mask */
    b2(0x09, 0xD6);  /* or esi,edx */
    b2(0x89, 0x31);  /* mov [rcx],esi */
    b3(0x48, 0xC1, 0xE0);
    b1((uint8_t)(64 - m->bw)); /* shl rax,64-w */
    b3(0x48, 0xC1, m->buns ? 0xE8 : 0xF8);
    b1((uint8_t)(64 - m->bw)); /* shr / sar rax,64-w */
}

/* Emits the conversion of the value in rax/xmm0 from one type to another. */
static void gen_cast(Type *from, Type *to)
{
    if (to->kind == TY_VOID || !is_scalar(to) || !is_scalar(from)) {
        return;
    }
    if (is_flt(to) || is_flt(from)) {
        if (is_flt(to) && is_flt(from)) {
            if (to->kind == TY_FLOAT && from->kind == TY_DOUBLE) {
                movq_x0_rax();
                cvt_sd2ss();
                cvt_ss2sd();
                movq_rax_x0();
            }
            return;
        }
        if (is_flt(to)) { /* integer -> floating */
            b1(0xF2);
            b4(0x48, 0x0F, 0x2A, 0xC0); /* cvtsi2sd xmm0,rax */
            if (to->kind == TY_FLOAT) {
                cvt_sd2ss();
                cvt_ss2sd();
            }
            movq_rax_x0();
        } else { /* floating -> integer */
            movq_x0_rax();
            b1(0xF2);
            b4(0x48, 0x0F, 0x2C, 0xC0); /* cvttsd2si rax,xmm0 */
            Type l = *ty_long;
            gen_cast(&l, to);
        }
        return;
    }
    if (to->size == 8) {
        return;
    }
    if (to->size == from->size && to->uns == from->uns) {
        return;
    }
    if (to->size > from->size && (!to->uns || from->uns)) {
        return;
    }
    switch (to->size) {
    case 1:
        if (to->uns) {
            b3(0x0F, 0xB6, 0xC0);
        } else {
            b4(0x48, 0x0F, 0xBE, 0xC0);
        }
        break;
    case 2:
        if (to->uns) {
            b3(0x0F, 0xB7, 0xC0);
        } else {
            b4(0x48, 0x0F, 0xBF, 0xC0);
        }
        break;
    case 4:
        if (to->uns) {
            b2(0x89, 0xC0);
        } else {
            b3(0x48, 0x63, 0xC0);
        }
        break;
    }
}

/* Re-normalises the integer in rax to its declared size (sign or zero extension). */
static void gen_norm(Type *t)
{
    if (t->size < 8 && is_int(t)) {
        Type l = *ty_long;
        gen_cast(&l, t);
    }
}

static void gen_expr(Node *n);
static void gen_addr(Node *n);

static void gen_truth(Node *c) /* rax = value of c, nonzero iff true */
{
    gen_expr(c);
    if (is_flt(c->ty)) {
        movq_x0_rax();
        b1(0x66);
        b3(0x0F, 0x57, 0xC9); /* xorpd xmm1,xmm1 */
        b1(0x66);
        b3(0x0F, 0x2E, 0xC1); /* ucomisd xmm0,xmm1 */
        b3(0x0F, 0x95, 0xC0);
        b3(0x0F, 0x9A, 0xC1);
        b2(0x08, 0xC8);
        b3(0x0F, 0xB6, 0xC0);
    }
}

/* Emits code that leaves the address of the lvalue n in rax. */
static void gen_addr(Node *n)
{
    switch (n->kind) {
    case N_VAR: {
        Sym *s = n->sym;
        if (s->kind == SY_FUNC) {
            b1(0xB8);
            add_cfix(2, s, 0);
        } else if (s->global) {
            b1(0xB8);
            add_cfix(1, s, 0);
        } else {
            b3(0x48, 0x8D, 0x85);
            d32(s->off);
        }
        return;
    }
    case N_DEREF:
        gen_expr(n->l);
        return;
    case N_MEMBER:
        gen_addr(n->l);
        if (n->val) {
            b2(0x48, 0x05);
            d32((int)n->val);
        }
        return;
    case N_STR:
        b1(0xB8);
        add_cfix(0, NULL, n->val);
        return;
    case N_ASSIGN:
    case N_COND:
    case N_COMMA:
    case N_CALL:
        gen_expr(n);
        return; /* aggregate rvalues yield their address */
    default:
        break;
    }
    if (!gerr_set) {
        snprintf(gerr, sizeof gerr, "internal: cannot take address (node %d)", n->kind);
        gerr_set = 1;
    }
}

/* Emits the binary operation n on n->l and n->r; the result is left in rax. */
static void gen_bin(Node *n)
{
    if (CX->opt.redundant_loads && (n->r->kind == N_NUM || n->r->kind == N_FNUM)) {
        /* -Z: a constant right operand goes straight into rcx; no save/restore of the left value */
        long long bits = n->r->val;
        if (n->r->kind == N_FNUM) {
            double dv = n->r->fval;
            memcpy(&bits, &dv, 8);
        }
        gen_expr(n->l);
        load_imm_rcx(bits);
    } else {
        gen_expr(n->l);
        push_rax();
        gen_expr(n->r);
        mov_rcx_rax();
        pop_rax();
    }
    int u = n->cmpty_uns;
    if (is_flt(n->l->ty)) {
        if (n->op >= B_EQ) {
            gen_fcmp(n->op);
            return;
        }
        gen_fop(n->op);
        if (n->ty->kind == TY_FLOAT) {
            movq_x0_rax();
            cvt_sd2ss();
            cvt_ss2sd();
            movq_rax_x0();
        } /* single precision result */
        return;
    }
    switch (n->op) {
    case B_ADD:
        b3(0x48, 0x01, 0xC8);
        break;
    case B_SUB:
        b3(0x48, 0x29, 0xC8);
        break;
    case B_MUL:
        b4(0x48, 0x0F, 0xAF, 0xC1);
        break;
    case B_DIV:
    case B_MOD:
        if (u) {
            b2(0x31, 0xD2);
            b3(0x48, 0xF7, 0xF1);
        } else {
            b2(0x48, 0x99);
            b3(0x48, 0xF7, 0xF9);
        }
        if (n->op == B_MOD) {
            b3(0x48, 0x89, 0xD0);
        }
        break;
    case B_AND:
        b3(0x48, 0x21, 0xC8);
        break;
    case B_OR:
        b3(0x48, 0x09, 0xC8);
        break;
    case B_XOR:
        b3(0x48, 0x31, 0xC8);
        break;
    case B_SHL:
        b3(0x48, 0xD3, 0xE0);
        break;
    case B_SHR:
        if (u) {
            b3(0x48, 0xD3, 0xE8);
        } else {
            b3(0x48, 0xD3, 0xF8);
        }
        break;
    default: {
        b3(0x48, 0x39, 0xC8);
        int cc = 0;
        switch (n->op) {
        case B_EQ:
            cc = CC_E;
            break;
        case B_NE:
            cc = CC_NE;
            break;
        case B_LT:
            cc = u ? CC_B : CC_L;
            break;
        case B_LE:
            cc = u ? CC_BE : CC_LE;
            break;
        case B_GT:
            cc = u ? CC_A : CC_G;
            break;
        case B_GE:
            cc = u ? CC_AE : CC_GE;
            break;
        }
        b3(0x0F, 0x90 + cc, 0xC0);
        b3(0x0F, 0xB6, 0xC0);
        return;
    }
    }
    gen_norm(n->ty);
}

/* apply 'op' with rax = lhs (already converted), rcx = rhs */
static void gen_op_rr(int op, int u)
{
    switch (op) {
    case B_ADD:
        b3(0x48, 0x01, 0xC8);
        break;
    case B_SUB:
        b3(0x48, 0x29, 0xC8);
        break;
    case B_MUL:
        b4(0x48, 0x0F, 0xAF, 0xC1);
        break;
    case B_DIV:
    case B_MOD:
        if (u) {
            b2(0x31, 0xD2);
            b3(0x48, 0xF7, 0xF1);
        } else {
            b2(0x48, 0x99);
            b3(0x48, 0xF7, 0xF9);
        }
        if (op == B_MOD) {
            b3(0x48, 0x89, 0xD0);
        }
        break;
    case B_AND:
        b3(0x48, 0x21, 0xC8);
        break;
    case B_OR:
        b3(0x48, 0x09, 0xC8);
        break;
    case B_XOR:
        b3(0x48, 0x31, 0xC8);
        break;
    case B_SHL:
        b3(0x48, 0xD3, 0xE0);
        break;
    case B_SHR:
        if (u) {
            b3(0x48, 0xD3, 0xE8);
        } else {
            b3(0x48, 0xD3, 0xF8);
        }
        break;
    }
}

/* Returns the number of bytes a call argument of type t occupies on the stack. */
static int arg_slot(Type *t)
{
    return is_agg(t) ? (int)((t->size + 7) & ~7L) : 8;
}

/* Emits a function call: evaluates and pushes the arguments, calls, and removes them again. */
static void gen_call(Node *n)
{
    Node *args[256];
    int argc = 0, bytes = 0;
    for (Node *a = n->r; a && argc < 256; a = a->next) {
        args[argc++] = a;
    }
    for (int i = argc - 1; i >= 0; i--) {
        gen_expr(args[i]);
        if (is_agg(args[i]->ty)) { /* structure by value: copy it onto the stack */
            int slot = arg_slot(args[i]->ty);
            b3(0x48, 0x89, 0xC6); /* mov rsi,rax */
            b3(0x48, 0x81, 0xEC);
            d32(slot);            /* sub rsp,slot */
            b3(0x48, 0x89, 0xE7); /* mov rdi,rsp */
            b1(0xB9);
            d32((int)args[i]->ty->size); /* mov ecx,size */
            b2(0xF3, 0xA4);              /* rep movsb */
            bytes += slot;
            continue;
        }
        if (args[i]->ty->kind == TY_FLOAT) {
            movq_x0_rax();
            cvt_sd2ss();
            b1(0x66);
            b3(0x0F, 0x7E, 0xC0);
        } /* movd eax,xmm0 */
        push_rax();
        bytes += 8;
    }
    if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__sqrt")) {
        pop_rax();
        movq_x0_rax();
        b4(0xF2, 0x0F, 0x51, 0xC0);
        movq_rax_x0();
        return;
    }
    if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__setjmp")) {
        b1(0x58);                   /* pop rax = jmp_buf */
        b3(0x48, 0x89, 0x28);       /* mov [rax],rbp */
        b4(0x48, 0x89, 0x60, 0x08); /* mov [rax+8],rsp */
        b3(0x48, 0x8D, 0x0D);
        d32(6);                     /* lea rcx,[rip+6]  -> the resume point below */
        b4(0x48, 0x89, 0x48, 0x10); /* mov [rax+16],rcx */
        b2(0x31, 0xC0);             /* xor eax,eax  (a longjmp resumes after this) */
        return;
    }
    if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__longjmp")) {
        b1(0x59);
        b1(0x58); /* pop rcx = jmp_buf; pop rax = value */
        b2(0x85, 0xC0);
        b2(0x75, 0x05);
        b1(0xB8);
        d32(1);                     /* a value of 0 becomes 1 */
        b3(0x48, 0x8B, 0x29);       /* mov rbp,[rcx] */
        b4(0x48, 0x8B, 0x61, 0x08); /* mov rsp,[rcx+8] */
        b3(0xFF, 0x61, 0x10);       /* jmp [rcx+16] */
        return;
    }
    if (n->sym && n->sym->is_builtin) {
        static const uint8_t pops[7][2] = {{0x58, 0},    {0x5F, 0},    {0x5E, 0},   {0x5A, 0},
                                           {0x5A, 0x41}, {0x58, 0x41}, {0x59, 0x41}};
        for (int i = 0; i < argc && i < 7; i++) {
            if (pops[i][1]) {
                b1(pops[i][1]);
            }
            b1(pops[i][0]);
        }
        b2(0x0F, 0x05);
        return;
    }
    if (is_agg(n->ty)) { /* hidden first argument: address of the result slot */
        b3(0x48, 0x8D, 0x85);
        d32(n->cmpty_uns); /* lea rax,[rbp+slot] */
        push_rax();
        bytes += 8;
    }
    if (n->sym) {
        b1(0xE8);
        add_cfix(3, n->sym, 0);
    } else {
        gen_expr(n->l);
        b2(0xFF, 0xD0);
    }
    if (bytes) {
        b3(0x48, 0x81, 0xC4);
        d32(bytes);
    }
}

/* Emits ++ and -- (prefix and postfix) on integers, pointers and floating-point values. */
static void gen_incdec(Node *n)
{
    int pre = n->kind == N_PREINC || n->kind == N_PREDEC;
    int inc = n->kind == N_PREINC || n->kind == N_POSTINC;
    Type *t = n->l->ty;
    int bf = is_bf(n->l);
    gen_addr(n->l);
    push_rax(); /* [addr] */
    if (bf) {
        bf_load(n->l);
    } else {
        load_ind(t); /* rax = old */
    }
    if (!pre) {
        push_rax(); /* [addr, old] */
    }
    if (is_flt(t)) {
        movq_x0_rax();
        b2(0x48, 0xB9);
        d64(0x3FF0000000000000LL);
        movq_x1_rcx();
        if (inc) {
            b4(0xF2, 0x0F, 0x58, 0xC1);
        } else {
            b4(0xF2, 0x0F, 0x5C, 0xC1);
        }
        movq_rax_x0();
    } else {
        if (inc) {
            b2(0x48, 0x05);
            d32((int)n->val);
        } else {
            b2(0x48, 0x2D);
            d32((int)n->val);
        }
        gen_norm(t);
    }
    if (!pre) {
        b4(0x48, 0x8B, 0x4C, 0x24);
        b1(8);
    } /* mov rcx,[rsp+8] */
    else {
        b3(0x48, 0x8B, 0x0C);
        b1(0x24);
    } /* mov rcx,[rsp] */
    if (bf) {
        bf_store(n->l);
    } else {
        store_ind(t);
    }
    if (!pre) {
        pop_rax();
        pop_rcx();
    } /* rax = old; drop addr */
    else {
        pop_rcx();
    }
}

/* Emits a compound assignment such as +=. */
static void gen_assignop(Node *n)
{
    Type *lt = n->l->ty, *ct = n->cty;
    int bf = is_bf(n->l);
    gen_addr(n->l);
    push_rax();
    gen_expr(n->r);
    mov_rcx_rax();
    b4(0x48, 0x8B, 0x04, 0x24); /* mov rax,[rsp] */
    if (bf) {
        bf_load(n->l);
    } else {
        load_ind(lt);
    }
    gen_cast(lt, ct);
    if (is_flt(ct)) {
        gen_fop(n->op);
        if (ct->kind == TY_FLOAT) {
            movq_x0_rax();
            cvt_sd2ss();
            cvt_ss2sd();
            movq_rax_x0();
        }
    } else {
        gen_op_rr(n->op, ct->uns);
        gen_norm(ct);
    }
    gen_cast(ct, lt);
    pop_rcx();
    if (bf) {
        bf_store(n->l);
    } else {
        store_ind(lt);
    }
}

/* Emits code that evaluates the expression n; the result is left in rax (doubles as raw bits). */
static void gen_expr(Node *n)
{
    switch (n->kind) {
    case N_NUM:
        load_imm(n->val);
        return;
    case N_FNUM: {
        double dv = n->fval;
        long long bits;
        memcpy(&bits, &dv, 8);
        load_imm(bits);
        return;
    }
    case N_STR:
        gen_addr(n);
        return;
    case N_VAR:
        if (is_mem_type(n->ty)) {
            gen_addr(n);
            return;
        }
        if (!n->sym->global && n->sym->kind == SY_VAR) {
            load_local(n->sym->off, n->ty);
            return;
        }
        gen_addr(n);
        load_ind(n->ty);
        return;
    case N_ADDR:
        gen_addr(n->l);
        return;
    case N_DEREF:
        gen_expr(n->l);
        load_ind(n->ty);
        return;
    case N_MEMBER:
        gen_addr(n);
        if (is_bf(n)) {
            bf_load(n);
        } else {
            load_ind(n->ty);
        }
        return;
    case N_ASSIGN:
        if (is_agg(n->ty)) {
            gen_expr(n->r);
            push_rax();
            gen_addr(n->l);
            b3(0x48, 0x89, 0xC7); /* mov rdi,rax */
            b1(0x5E);             /* pop rsi */
            b1(0xB9);
            d32(n->ty->size);     /* mov ecx,size */
            b2(0xF3, 0xA4);       /* rep movsb */
            b3(0x48, 0x89, 0xF8); /* mov rax,rdi */
            return;
        }
        gen_expr(n->r);
        if (n->l->kind == N_VAR && !n->l->sym->global) {
            store_local(n->l->sym->off, n->l->ty);
            return;
        }
        push_rax();
        gen_addr(n->l);
        mov_rcx_rax();
        pop_rax();
        if (is_bf(n->l)) {
            bf_store(n->l);
        } else {
            store_ind(n->l->ty);
        }
        return;
    case N_ASSIGNOP:
        gen_assignop(n);
        return;
    case N_COND: {
        int lf = newlabel(), le = newlabel();
        gen_truth(n->c);
        test_rax();
        jump_to(lf, CC_E);
        gen_expr(n->l);
        jump_to(le, -1);
        place(lf);
        gen_expr(n->r);
        place(le);
        return;
    }
    case N_COMMA:
        gen_expr(n->l);
        gen_expr(n->r);
        return;
    case N_LOGAND:
    case N_LOGOR: {
        int lshort = newlabel(), le = newlabel();
        int and = n->kind == N_LOGAND;
        gen_truth(n->l);
        test_rax();
        jump_to(lshort, and? CC_E : CC_NE);
        gen_truth(n->r);
        test_rax();
        jump_to(lshort, and? CC_E : CC_NE);
        load_imm(and? 1 : 0);
        jump_to(le, -1);
        place(lshort);
        load_imm(and? 0 : 1);
        place(le);
        return;
    }
    case N_NOT:
        gen_truth(n->l);
        test_rax();
        b3(0x0F, 0x94, 0xC0);
        b3(0x0F, 0xB6, 0xC0);
        return;
    case N_NEG:
        gen_expr(n->l);
        if (is_flt(n->ty)) {
            b2(0x48, 0xB9);
            d64((long long)0x8000000000000000ULL);
            b3(0x48, 0x31, 0xC8);
            return;
        }
        b3(0x48, 0xF7, 0xD8);
        gen_norm(n->ty);
        return;
    case N_BITNOT:
        gen_expr(n->l);
        b3(0x48, 0xF7, 0xD0);
        gen_norm(n->ty);
        return;
    case N_BIN:
        gen_bin(n);
        return;
    case N_CALL:
        gen_call(n);
        return;
    case N_CAST:
        gen_expr(n->l);
        gen_cast(n->l->ty, n->ty);
        return;
    case N_PREINC:
    case N_POSTINC:
    case N_PREDEC:
    case N_POSTDEC:
        gen_incdec(n);
        return;
    default:
        if (!gerr_set) {
            snprintf(gerr, sizeof gerr, "internal: unknown expression node %d", n->kind);
            gerr_set = 1;
        }
    }
}

/* ---------- statements ---------- */
static int goto_label(const char *name)
{
    for (int i = 0; i < ngl; i++) {
        if (gls[i].name == name) {
            return gls[i].label;
        }
    }
    if (ngl == capgl) {
        capgl = capgl ? capgl * 2 : 16;
        gls = realloc(gls, capgl * sizeof *gls);
    }
    gls[ngl].name = (char *)name;
    gls[ngl].label = newlabel();
    return gls[ngl++].label;
}

enum { FC_USER, FC_C0, FC_LIBC, FC_LIBM, FC_OBJ };

static int fid_class(int fid);

/* ---------- debug line table ---------- */
static struct DLn {
    uint32_t pos;
    int line;
    Sym *fn;
} *dln;

static int capdln, dbg_rec;

/* Returns the code position of the last recorded debug line entry. */
static uint32_t dln_last_pos(void)
{
    return dln[ndln - 1].pos;
}

/* Moves the last recorded debug line entry to a new code position. */
static void dln_set_last_pos(int pos)
{
    dln[ndln - 1].pos = pos;
}

/* Records that code for source line "line" starts at the current position (debug line table). */
static void note_line(int line)
{
    mark_pos = ncode;
    if (!dbg_rec || !gen_fn || line <= 0) {
        return;
    }
    if (ndln && dln[ndln - 1].pos == (uint32_t)ncode && dln[ndln - 1].fn == gen_fn) {
        dln[ndln - 1].line = line;
        return;
    }
    if (ndln && dln[ndln - 1].fn == gen_fn && dln[ndln - 1].line == line) {
        return;
    }
    if (ndln == capdln) {
        capdln = capdln ? capdln * 2 : 256;
        dln = realloc(dln, capdln * sizeof *dln);
    }
    dln[ndln].pos = ncode;
    dln[ndln].line = line;
    dln[ndln].fn = gen_fn;
    ndln++;
}

static void gen_stmt(Node *n);

/* Emits a list of statements. */
static void gen_list(Node *n)
{
    for (; n; n = n->next) {
        gen_stmt(n);
    }
}

/* Emits one statement. */
static void gen_stmt(Node *n)
{
    switch (n->kind) {
    case S_NOP:
        return;
    case S_EXPR:
        note_line(n->line);
        gen_expr(n->l);
        return;
    case S_BLOCK:
        gen_list(n->l);
        return;
    case S_ZERO: {
        Sym *s = n->sym;
        b3(0x48, 0x8D, 0xBD);
        d32(s->off); /* lea rdi,[rbp+off] */
        b1(0xB9);
        d32(s->ty->size);
        b2(0x31, 0xC0);
        b2(0xF3, 0xAA);
        return;
    }
    case S_IF: {
        int lelse = newlabel(), lend = newlabel();
        note_line(n->line);
        gen_truth(n->c);
        test_rax();
        jump_to(lelse, CC_E);
        gen_stmt(n->l);
        if (n->r) {
            jump_to(lend, -1);
            place(lelse);
            gen_stmt(n->r);
            place(lend);
        } else {
            place(lelse);
        }
        return;
    }
    case S_WHILE: {
        int ltop = newlabel(), lend = newlabel();
        place(ltop);
        note_line(n->line);
        gen_truth(n->c);
        test_rax();
        jump_to(lend, CC_E);
        brk_label[++ctl_sp] = lend;
        cont_label[ctl_sp] = ltop;
        gen_stmt(n->l);
        ctl_sp--;
        jump_to(ltop, -1);
        place(lend);
        return;
    }
    case S_DO: {
        int ltop = newlabel(), lcont = newlabel(), lend = newlabel();
        place(ltop);
        brk_label[++ctl_sp] = lend;
        cont_label[ctl_sp] = lcont;
        gen_stmt(n->l);
        ctl_sp--;
        place(lcont);
        note_line(n->c->line);
        gen_truth(n->c);
        test_rax();
        jump_to(ltop, CC_NE);
        place(lend);
        return;
    }
    case S_FOR: {
        int ltop = newlabel(), lcont = newlabel(), lend = newlabel();
        note_line(n->line);
        gen_list(n->d);
        place(ltop);
        note_line(n->line);
        if (n->c) {
            gen_truth(n->c);
            test_rax();
            jump_to(lend, CC_E);
        }
        brk_label[++ctl_sp] = lend;
        cont_label[ctl_sp] = lcont;
        gen_stmt(n->l);
        ctl_sp--;
        place(lcont);
        note_line(n->line);
        if (n->r) {
            gen_expr(n->r);
        }
        jump_to(ltop, -1);
        place(lend);
        return;
    }
    case S_RETURN:
        note_line(n->line);
        if (n->l) {
            gen_expr(n->l);
        }
        if (n->l &&
            is_agg(gen_fn->ty
                       ->ret)) {  /* copy the value into the caller's buffer, return its address */
            b3(0x48, 0x89, 0xC6); /* mov rsi,rax */
            b4(0x48, 0x8B, 0x7D, 0x10); /* mov rdi,[rbp+16] */
            b1(0xB9);
            d32((int)gen_fn->ty->ret->size); /* mov ecx,size */
            b2(0xF3, 0xA4);                  /* rep movsb */
            b4(0x48, 0x8B, 0x45, 0x10);      /* mov rax,[rbp+16] */
        }
        jump_to(ret_label, -1);
        return;
    case S_BREAK:
        note_line(n->line);
        jump_to(brk_label[ctl_sp], -1);
        return;
    case S_CONTINUE: {
        note_line(n->line);
        int i = ctl_sp;
        while (i > 0 && cont_label[i] < 0) {
            i--;
        }
        jump_to(cont_label[i], -1);
        return;
    }
    case S_SWITCH: {
        int lend = newlabel(), ldef = -1;
        note_line(n->line);
        gen_expr(n->c);
        for (Node *c = n->cases; c; c = c->r) {
            c->label = newlabel();
            if (c->kind == S_DEFAULT) {
                ldef = c->label;
                continue;
            }
            b2(0x48, 0xB9);
            d64(c->val);          /* mov rcx,imm64 */
            b3(0x48, 0x39, 0xC8); /* cmp rax,rcx */
            jump_to(c->label, CC_E);
        }
        jump_to(ldef >= 0 ? ldef : lend, -1);
        brk_label[++ctl_sp] = lend;
        cont_label[ctl_sp] = -1;
        gen_stmt(n->l);
        ctl_sp--;
        place(lend);
        return;
    }
    case S_CASE:
    case S_DEFAULT:
        place(n->label);
        gen_stmt(n->l);
        return;
    case S_LABEL:
        place(goto_label(n->str));
        gen_stmt(n->l);
        return;
    case S_GOTO:
        note_line(n->line);
        jump_to(goto_label(n->str), -1);
        return;
    default:
        if (!gerr_set) {
            snprintf(gerr, sizeof gerr, "internal: unknown statement node %d", n->kind);
            gerr_set = 1;
        }
    }
}

/* -O: branch relaxation.  Every jump is first emitted with a 32-bit displacement (jmp = 5 bytes,
 * jcc = 6 bytes); the ones whose target lies within +-127 bytes are rewritten to the 2-byte form
 * and the function body is compacted.  Shrinking only ever brings targets closer, so the decision
 * is iterated to a fixed point. */
static int relax_newpos(const int *start, const int *saved, int n,
                        int x) /* new offset of old offset x */
{
    int lo = 0, hi = n; /* count jumps starting before x */
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (start[m] < x) {
            lo = m + 1;
        } else {
            hi = m;
        }
    }
    return x - (lo ? saved[lo - 1] : 0);
}

/*
 * Shrinks jumps whose target is near to the 8-bit form and fixes up everything that
 * moves (jump table, fix-ups and the debug line table).
 */
static void relax_jumps(int base, int cfix0, int dln0)
{
    if (!njfix) {
        return;
    }
    int *start = malloc(njfix * sizeof(int)), *size = malloc(njfix * sizeof(int)),
        *saved = malloc(njfix * sizeof(int));
    for (int i = 0; i < njfix; i++) {
        int jmp = code[jfix[i].pos - 1] == 0xE9;
        start[i] = jfix[i].pos - (jmp ? 1 : 2);
        size[i] = jmp ? 5 : 6;
    }
    for (int changed = 1; changed;) {
        changed = 0;
        int acc = 0;
        for (int i = 0; i < njfix; i++) {
            if (jfix[i].rel8) {
                acc += size[i] - 2;
            }
            saved[i] = acc;
        }
        for (int i = 0; i < njfix; i++) {
            if (jfix[i].rel8) {
                continue;
            }
            int disp = relax_newpos(start, saved, njfix, lpos[jfix[i].label]) -
                       (relax_newpos(start, saved, njfix, start[i]) + 2);
            if (disp >= -128 && disp <= 127) {
                jfix[i].rel8 = 1;
                changed = 1;
            }
        }
    }
    int acc = 0, any = 0;
    for (int i = 0; i < njfix; i++) {
        if (jfix[i].rel8) {
            acc += size[i] - 2;
            any = 1;
        }
        saved[i] = acc;
    }
    if (any) {
        int w = start[0], r = start[0], j = 0;
        while (r < ncode) {
            if (j < njfix && r == start[j]) {
                if (jfix[j].rel8) {
                    int jmp = size[j] == 5;
                    code[w++] = jmp ? 0xEB : (uint8_t)(0x70 + (code[r + 1] - 0x80));
                    code[w++] = 0;
                    r += size[j];
                    jfix[j].pos = w - 1; /* the new displacement byte, patched with the labels */
                } else {
                    int len = size[j];
                    memmove(code + w, code + r, len);
                    jfix[j].pos = w + len - 4;
                    w += len;
                    r += len;
                }
                j++;
            } else {
                code[w++] = code[r++];
            }
        }
        for (int i = 0; i < nlabels; i++) {
            if (lpos[i] >= 0) {
                lpos[i] = relax_newpos(start, saved, njfix, lpos[i]);
            }
        }
        for (int i = cfix0; i < ncfix; i++) {
            cfix[i].pos = relax_newpos(start, saved, njfix, cfix[i].pos);
        }
        for (int i = dln0; i < ndln; i++) {
            dln[i].pos = relax_newpos(start, saved, njfix, dln[i].pos);
        }
        ncode = w;
    }
    free(start);
    free(size);
    free(saved);
    (void)base;
}

/* Local Options|Exclude debug information: functions defined in these files get no line table */
static int file_without_debug(int fileid)
{
    for (int i = 0; i < CX->opt.nnodebug; i++) {
        if (!strcmp(CX->opt.nodebug_files[i], file_name(fileid))) {
            return 1;
        }
    }
    return 0;
}

/* Emits the code of one function: prologue, body and epilogue. */
static void gen_function(Sym *f)
{
    int cfix0 = ncfix, dln0 = ndln;
    f->codepos = ncode;
    gen_fn = f;
    ngl = 0;
    ctl_sp = 0;
    nlabels = 0;
    njfix = 0;
    dbg_rec = CX->opt.debug && fid_class(f->fileid) == FC_USER && !file_without_debug(f->fileid);
    cont_label[0] = -1;
    b1(0x55);
    b3(0x48, 0x89, 0xE5);
    b3(0x48, 0x81, 0xEC);
    int framepos = ncode;
    d32(0);
    if (CX->opt.stack_check &&
        fid_class(f->fileid) == FC_USER) { /* -N:  if (rsp < __fc_stklimit) __fc_stkover(); */
        Sym *lim = global_lookup("__fc_stklimit"), *over = global_lookup("__fc_stkover");
        if (lim && over) {
            b4(0x48, 0x3B, 0x24, 0x25);
            add_cfix(1, lim, 0); /* cmp rsp, [__fc_stklimit] */
            b2(0x73, 0x05);      /* jae +5 */
            b1(0xE8);
            add_cfix(3, over, 0); /* call __fc_stkover */
        }
    }
    opt_function(f, dbg_rec);
    ret_label = newlabel();
    gen_stmt(f->body);
    b2(0x31, 0xC0); /* falling off the end returns 0 */
    place(ret_label);
    note_line(f->body->cmpty_uns); /* the closing brace */
    if (f->ty->ret && f->ty->ret->kind == TY_VOID && !strcmp(f->name, "main")) {
        b2(0x31, 0xC0); /* void main exits with 0 */
    }
    b2(0xC9, 0xC3); /* leave; ret */
    if (CX->opt.jump_opt) {
        relax_jumps(f->codepos, cfix0, dln0);
    }
    f->codelen = ncode - f->codepos;
    int fs = (f->frame + 15) & ~15;
    for (int i = 0; i < 4; i++) {
        code[framepos + i] = (fs >> (8 * i)) & 0xff;
    }
    for (int i = 0; i < njfix; i++) {
        if (jfix[i].rel8) {
            code[jfix[i].pos] = (uint8_t)(lpos[jfix[i].label] - (jfix[i].pos + 1));
            continue;
        }
        int rel = lpos[jfix[i].label] - (jfix[i].pos + 4);
        for (int k = 0; k < 4; k++) {
            code[jfix[i].pos + k] = (rel >> (8 * k)) & 0xff;
        }
    }
}

/* ---------- program ---------- */
static void mark(Sym *s)
{
    if (s->reach & 1) {
        return;
    }
    s->reach |= 1;
    for (int i = 0; i < s->nrefs; i++) {
        mark(s->refs[i]);
    }
}

/* Stores a 32-bit little-endian value at p. */
static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (v >> (8 * i)) & 0xff;
    }
}

/* Stores a 64-bit little-endian value at p. */
static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (v >> (8 * i)) & 0xff;
    }
}

/* Reports a link error (an unresolved symbol); each message is shown only once. */
static void linker_error(const char *fmt, ...)
{
    static char seen[64][300];
    static int nseen;
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (CX->errors == 0) {
        nseen = 0;
    }
    for (int i = 0; i < nseen; i++) {
        if (!strcmp(seen[i], msg)) {
            return; /* each undefined symbol is reported once */
        }
    }
    if (nseen < 64) {
        strcpy(seen[nseen++], msg);
    }
    CX->errors++;
    if (CX->diag) {
        CX->diag(CX->user, FC_FATAL + 1, "", 0, msg);
    }
}

/* ---------- link step: start-up / exit procedures, module bookkeeping, map file ---------- */
static int fid_class(int fid)
{
    const char *n = file_name(fid);
    if (!strncmp(n, "c0/", 3)) {
        return FC_C0;
    }
    if (!strncmp(n, "libc/", 5)) {
        return FC_LIBC;
    }
    if (!strncmp(n, "libm/", 5)) {
        return FC_LIBM;
    }
    if (!strncmp(n, "obj/", 4)) {
        return FC_OBJ;
    }
    return FC_USER;
}

/* does the module (library file) have any symbol that is part of the program besides `except`? */
static int module_live(int fid, Sym *except)
{
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if (f != except && f->fileid == fid && f->defined && (f->reach & 1)) {
            return 1;
        }
    }
    for (Sym *y = global_syms(); y; y = y->next) {
        if (y != except && y->kind == SY_VAR && y->fileid == fid && y->defined && (y->reach & 1)) {
            return 1;
        }
    }
    return 0;
}

/* Returns the function named by a #pragma startup/exit entry if it is defined, else NULL. */
static Sym *pragma_fn(PragmaFn *p)
{
    Sym *f = global_lookup(p->name);
    return (f && f->kind == SY_FUNC && f->defined) ? f : NULL;
}

/* #pragma startup / exit functions of the program and of linked library modules join the link */
static void mark_pragma_fns(void)
{
    int changed;
    do {
        changed = 0;
        for (int i = 0; i < pp_npragmas; i++) {
            Sym *f = pragma_fn(&pp_pragmas[i]);
            if (!f || (f->reach & 1)) {
                continue;
            }
            int cls = fid_class(f->fileid);
            if (cls == FC_USER || cls == FC_C0 || cls == FC_OBJ || module_live(f->fileid, f)) {
                mark(f);
                changed = 1;
            }
        }
    } while (changed);
}

/* the compiler writes __fc_init / __fc_exit: they call the collected procedures in priority order
 */
static void emit_proc_list(const char *symname, int want_exit)
{
    Sym *t = global_lookup(symname);
    if (!t || !(t->reach & 1)) {
        return;
    }
    int sel[64], n = 0;
    for (int i = 0; i < pp_npragmas; i++) {
        PragmaFn *p = &pp_pragmas[i];
        Sym *f = pragma_fn(p);
        if (p->is_exit == want_exit && f && (f->reach & 1)) {
            sel[n++] = i;
        }
    }
    for (int i = 1; i < n; i++) { /* stable insertion sort: start-up ascending, exit descending */
        int v = sel[i], j = i;
        while (j > 0 && (want_exit ? pp_pragmas[sel[j - 1]].prio < pp_pragmas[v].prio
                                   : pp_pragmas[sel[j - 1]].prio > pp_pragmas[v].prio)) {
            sel[j] = sel[j - 1];
            j--;
        }
        sel[j] = v;
    }
    t->codepos = ncode;
    t->defined = 1;
    b1(0x55);
    b3(0x48, 0x89, 0xE5);
    for (int i = 0; i < n; i++) {
        b1(0xE8);
        add_cfix(3, pragma_fn(&pp_pragmas[sel[i]]), 0);
    }
    b2(0x5D, 0xC3);
    t->codelen = ncode - t->codepos;
}

/* signal delivery stubs: __sigtramp adapts the kernel's call to the stack convention, __sigrestorer
 * returns to the kernel */
static void emit_sigstubs(void)
{
    Sym *t = global_lookup("__sigtramp"), *r = global_lookup("__sigrestorer"),
        *d = global_lookup("__sig_dispatch");
    if (t && (t->reach & 1) && d && d->defined) {
        t->codepos = ncode;
        t->defined = 1;
        b1(0x57); /* push rdi  (signal number) */
        b1(0xE8);
        add_cfix(3, d, 0);          /* call __sig_dispatch */
        b4(0x48, 0x83, 0xC4, 0x08); /* add rsp,8 */
        b1(0xC3);                   /* ret -> __sigrestorer */
        t->codelen = ncode - t->codepos;
    }
    if (r && (r->reach & 1)) {
        r->codepos = ncode;
        r->defined = 1;
        b1(0xB8);
        d32(15);        /* mov eax,15 (rt_sigreturn) */
        b2(0x0F, 0x05); /* syscall */
        r->codelen = ncode - r->codepos;
    }
}

typedef struct {
    const char *name;
    uint32_t addr, size;
    int fid, isfn;
} MapEnt;

static struct LinkMod {
    char name[64];
    int cls;
    long bytes;
} linkmods[256];

static int nlinkmods;

/* Returns the number of run-time library modules in the last link. */
int fc_link_module_count(void)
{
    return nlinkmods;
}

/* Returns the name of linked module i; *lib is its library and *bytes its code size. */
const char *fc_link_module_at(int i, const char **lib, long *bytes)
{
    if (i < 0 || i >= nlinkmods) {
        return NULL;
    }
    if (lib) {
        *lib = linkmods[i].cls == FC_C0     ? "start-up"
               : linkmods[i].cls == FC_LIBC ? "libc"
               : linkmods[i].cls == FC_LIBM ? "libm"
               : linkmods[i].cls == FC_OBJ  ? "object"
                                            : "program";
    }
    if (bytes) {
        *bytes = linkmods[i].bytes;
    }
    return linkmods[i].name;
}

/* Adds bytes of generated code to the size recorded for the library module of file fid. */
static void note_module(int fid, long bytes)
{
    const char *full = file_name(fid);
    const char *nm = strchr(full, '/') ? strchr(full, '/') + 1 : full;
    int cls = fid_class(fid);
    for (int i = 0; i < nlinkmods; i++) {
        if (linkmods[i].cls == cls && !strcmp(linkmods[i].name, nm)) {
            linkmods[i].bytes += bytes;
            return;
        }
    }
    if (nlinkmods >= 256) {
        return;
    }
    snprintf(linkmods[nlinkmods].name, sizeof linkmods[0].name, "%s", nm);
    char *dot = strrchr(linkmods[nlinkmods].name, '.');
    if (dot && cls != FC_USER) {
        *dot = 0;
    }
    linkmods[nlinkmods].cls = cls;
    linkmods[nlinkmods].bytes = bytes;
    nlinkmods++;
}

/* qsort order of the link map: by name. */
static int map_cmp_name(const void *a, const void *b)
{
    return strcmp(((const MapEnt *)a)->name, ((const MapEnt *)b)->name);
}

/* qsort order of the link map: by address, then name. */
static int map_cmp_addr(const void *a, const void *b)
{
    const MapEnt *x = a, *y = b;
    return x->addr < y->addr ? -1 : x->addr > y->addr ? 1 : strcmp(x->name, y->name);
}

/*
 * Writes the link map (next to the executable, with the extension .map).  mode selects how
 * much it contains; the remaining arguments are the layout of the output file.
 */
static void write_map(const char *outpath, int mode, uint32_t codeaddr, int codelen,
                      uint32_t dataaddr, int datalen, uint32_t bssaddr, int bsslen, uint32_t entry)
{
    char path[1100];
    snprintf(path, sizeof path, "%s", outpath);
    char *dot = strrchr(path, '.'), *sl = strrchr(path, '/');
    if (dot && (!sl || dot > sl)) {
        *dot = 0;
    }
    strncat(path, ".map", sizeof path - strlen(path) - 1);
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    const char *base = strrchr(outpath, '/');
    base = base ? base + 1 : outpath;
    fprintf(f, "\n Flash C link map of %s\n\n", base);
    fprintf(f, " Start      Length     Name       Class\n");
    fprintf(f, " 0x%08X 0x%08X _TEXT      CODE\n", codeaddr, (unsigned)codelen);
    fprintf(f, " 0x%08X 0x%08X _DATA      DATA\n", dataaddr, (unsigned)datalen);
    fprintf(f, " 0x%08X 0x%08X _BSS       BSS\n", bssaddr, (unsigned)bsslen);

    MapEnt *ents = calloc(PG.nfuncs + 4096, sizeof *ents);
    int ne = 0;
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *fn = PG.funcs[i];
        if (!(fn->reach & 1) || !fn->defined || fn->is_builtin || fn->codepos < 0) {
            continue;
        }
        ents[ne++] = (MapEnt){fn->name, codeaddr + fn->codepos, fn->codelen, fn->fileid, 1};
    }
    for (int k = 0; k < 2; k++) { /* the compiler-written procedure lists */
        Sym *t = global_lookup(k ? "__fc_exit" : "__fc_init");
        if (t && (t->reach & 1) && t->codepos >= 0) {
            ents[ne++] = (MapEnt){t->name, codeaddr + t->codepos, t->codelen, t->fileid, 1};
        }
    }
    int nfn = ne;
    for (Sym *y = global_syms(); y && ne < PG.nfuncs + 4090; y = y->next) {
        if (y->kind != SY_VAR || !y->defined || !(y->reach & 1)) {
            continue;
        }
        ents[ne++] = (MapEnt){y->name, (y->bss ? bssaddr : dataaddr) + y->off,
                              y->ty->size > 0 ? y->ty->size : 0, y->fileid, 0};
    }
    if (mode >= 3) {
        fprintf(f, "\n Detailed map of segments\n");
        qsort(ents, ne, sizeof *ents, map_cmp_addr);
        for (int pass = 0; pass < 3; pass++) {
            const char *cn = pass == 0   ? "CODE"
                             : pass == 1 ? "DATA"
                                         : "BSS",
                       *sn = pass == 0   ? "_TEXT"
                             : pass == 1 ? "_DATA"
                                         : "_BSS";
            uint32_t lo = pass == 0   ? codeaddr
                          : pass == 1 ? dataaddr
                                      : bssaddr,
                     hi = lo + (pass == 0   ? codelen
                                : pass == 1 ? datalen
                                            : bsslen);
            for (int i = 0; i < ne;) {
                if (ents[i].addr < lo || ents[i].addr >= hi) {
                    i++;
                    continue;
                }
                int j = i;
                uint32_t end = ents[i].addr;
                while (j < ne && ents[j].fid == ents[i].fid && ents[j].addr >= lo &&
                       ents[j].addr < hi) {
                    if (ents[j].addr + ents[j].size > end) {
                        end = ents[j].addr + ents[j].size;
                    }
                    j++;
                }
                fprintf(f, " 0x%08X 0x%08X C=%-5s S=%-6s M=%s\n", ents[i].addr, end - ents[i].addr,
                        cn, sn, file_name(ents[i].fid));
                i = j;
            }
        }
    }
    if (mode >= 2) {
        int nstatic = 0;
        MapEnt *pub = calloc(ne + 1, sizeof *pub);
        for (int i = 0; i < ne; i++) {
            if (!strcmp(ents[i].name, "<hidden>")) {
                continue;
            }
            pub[nstatic++] = ents[i];
        }
        qsort(pub, nstatic, sizeof *pub, map_cmp_name);
        fprintf(f, "\n  Address     Publics by Name\n\n");
        for (int i = 0; i < nstatic; i++) {
            fprintf(f, " 0x%08X   _%s\n", pub[i].addr, pub[i].name);
        }
        qsort(pub, nstatic, sizeof *pub, map_cmp_addr);
        fprintf(f, "\n  Address     Publics by Value\n\n");
        for (int i = 0; i < nstatic; i++) {
            fprintf(f, " 0x%08X   _%s\n", pub[i].addr, pub[i].name);
        }
        free(pub);
    }
    (void)nfn;
    fprintf(f, "\nProgram entry point at 0x%08X\n", entry);
    free(ents);
    fclose(f);
}

/* ---------- debug information handed to the IDE debugger ---------- */
static FcDebug dbg_out;
static int dbg_have;
static void *dbg_blocks[4096];
static int ndbg_blocks;

/* Allocates zeroed memory that is released together with the debug information. */
static void *dalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (ndbg_blocks < 4096) {
        dbg_blocks[ndbg_blocks++] = p;
    }
    return p;
}

/* Duplicates a string for the debug information. */
static char *dstr(const char *s)
{
    size_t n = strlen(s);
    char *p = dalloc(n + 1);
    memcpy(p, s, n);
    return p;
}

/* Frees the debug information of the previous compilation. */
static void dbg_free(void)
{
    for (int i = 0; i < ndbg_blocks; i++) {
        free(dbg_blocks[i]);
    }
    ndbg_blocks = 0;
    dbg_have = 0;
    memset(&dbg_out, 0, sizeof dbg_out);
}

/* Returns the debug information of the last successful compile, or NULL. */
const FcDebug *fc_debug_info(void)
{
    return dbg_have ? &dbg_out : NULL;
}

static struct {
    Type *t;
    FcDType *d;
} tymap[512];

static int ntymap;

/* Converts a compiler type into its (cached) debug-information form. */
static FcDType *dtype(Type *t)
{
    if (!t) {
        return NULL;
    }
    for (int i = 0; i < ntymap; i++) {
        if (tymap[i].t == t) {
            return tymap[i].d;
        }
    }
    FcDType *d = dalloc(sizeof *d);
    if (ntymap < 512) {
        tymap[ntymap].t = t;
        tymap[ntymap].d = d;
        ntymap++;
    }
    d->kind = t->kind;
    d->size = t->size;
    d->uns = t->uns;
    d->len = t->len;
    d->tag = t->tag ? dstr(t->tag) : NULL;
    d->base = t->kind == TY_FUNC ? dtype(t->ret) : dtype(t->base);
    if ((t->kind == TY_STRUCT || t->kind == TY_UNION) && t->complete) {
        int n = 0;
        for (Member *m = t->members; m; m = m->next) {
            n++;
        }
        d->mem = dalloc(n * sizeof *d->mem);
        for (Member *m = t->members; m; m = m->next) {
            d->mem[d->nmem].name = m->name ? dstr(m->name) : "";
            d->mem[d->nmem].ty = dtype(m->ty);
            d->mem[d->nmem].off = m->off;
            d->mem[d->nmem].bw = m->bw;
            d->mem[d->nmem].bsh = m->bsh;
            d->mem[d->nmem].buns = m->buns;
            d->nmem++;
        }
    }
    return d;
}

/* Returns the index of a source file in the debug file table, adding it if needed. */
static int dbg_file_index(const char *name)
{
    for (int i = 0; i < dbg_out.nfiles; i++) {
        if (!strcmp(dbg_out.files[i], name)) {
            return i;
        }
    }
    const char **nf = dalloc((dbg_out.nfiles + 1) * sizeof *nf);
    if (dbg_out.nfiles) {
        memcpy(nf, dbg_out.files, dbg_out.nfiles * sizeof *nf);
    }
    nf[dbg_out.nfiles] = dstr(name);
    dbg_out.files = nf;
    return dbg_out.nfiles++;
}

/*
 * Builds the debug information (functions, variables, globals, lines) once the final
 * addresses of the data and bss sections are known.
 */
static void dbg_build(uint32_t dataaddr, uint32_t bssaddr)
{
    dbg_free();
    ntymap = 0;
    if (!CX->opt.debug) {
        return;
    }
    int nf = 0;
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if ((f->reach & 1) && f->defined && !f->is_builtin && fid_class(f->fileid) == FC_USER) {
            nf++;
        }
    }
    dbg_out.funcs = dalloc(nf * sizeof(FcDFunc));
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if (!((f->reach & 1) && f->defined && !f->is_builtin && fid_class(f->fileid) == FC_USER)) {
            continue;
        }
        FcDFunc *d = &dbg_out.funcs[dbg_out.nfuncs++];
        d->name = dstr(f->name);
        d->file = dstr(file_name(f->fileid));
        d->addr = BASE + HDRSZ + f->codepos;
        d->end = d->addr + f->codelen;
        d->line0 = f->line;
        d->line1 = f->body ? f->body->cmpty_uns : f->line;
        d->ret = dtype(f->ty->ret);
        d->vars = dalloc((f->ndbg ? f->ndbg : 1) * sizeof(FcDVar));
        for (int k = 0; k < f->ndbg; k++) {
            Sym *y = f->dbgvars[k];
            FcDVar *v = &d->vars[d->nvars++];
            v->name = dstr(y->name);
            v->ty = dtype(y->ty);
            v->off = y->off;
            v->is_param = y->is_param;
            v->line0 = y->is_param ? d->line0 : y->line;
            v->line1 = y->endline ? y->endline : d->line1;
            if (y->is_param) {
                d->nparams++;
            }
        }
        dbg_file_index(d->file);
    }
    dbg_out.lines = dalloc((ndln ? ndln : 1) * sizeof(FcDLine));
    for (int i = 0; i < ndln; i++) {
        FcDLine *l = &dbg_out.lines[dbg_out.nlines++];
        l->addr = BASE + HDRSZ + dln[i].pos;
        l->line = dln[i].line;
        l->file = dbg_file_index(file_name(dln[i].fn->fileid));
    }
    int ng = 0;
    for (Sym *y = global_syms(); y; y = y->next) {
        if (y->kind == SY_VAR && y->defined && y->global && fid_class(y->fileid) == FC_USER) {
            ng++;
        }
    }
    dbg_out.globals = dalloc((ng ? ng : 1) * sizeof(FcDGlobal));
    for (Sym *y = global_syms(); y; y = y->next) {
        if (!(y->kind == SY_VAR && y->defined && y->global && fid_class(y->fileid) == FC_USER)) {
            continue;
        }
        FcDGlobal *g = &dbg_out.globals[dbg_out.nglobals++];
        g->name = dstr(y->name);
        g->ty = dtype(y->ty);
        g->file = dstr(file_name(y->fileid));
        g->addr = y->bss ? bssaddr + y->off : dataaddr + y->off;
    }
    dbg_out.entry = BASE + HDRSZ;
    dbg_have = 1;
}

/* -lt: .symtab/.strtab with every function and variable that was linked (for nm, gdb, objdump,
 * perf) */
static void build_symtab(uint8_t **symtab, int *symsz, uint8_t **strtab, int *strsz,
                         uint32_t dataaddr, uint32_t bssaddr)
{
    int n = 1, strcap = 256, nstr = 1;
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if ((f->reach & 1) && f->defined && !f->is_builtin) {
            n++;
        }
    }
    for (Sym *y = global_syms(); y; y = y->next) {
        if (y->kind == SY_VAR && y->defined && (y->reach & 1)) {
            n++;
        }
    }
    uint8_t *st = calloc(n, 24), *str = calloc(strcap, 1);
    int k = 1;
#define ADDSYM(nm, val, size, info, shndx)                                                         \
    do {                                                                                           \
        int l = (int)strlen(nm) + 1;                                                               \
        while (nstr + l > strcap) {                                                                \
            strcap *= 2;                                                                           \
            str = realloc(str, strcap);                                                            \
        }                                                                                          \
        memcpy(str + nstr, nm, l);                                                                 \
        uint8_t *e = st + k * 24;                                                                  \
        put32(e, nstr);                                                                            \
        e[4] = (info);                                                                             \
        e[5] = 0;                                                                                  \
        e[6] = (shndx) & 0xff;                                                                     \
        e[7] = ((shndx) >> 8) & 0xff;                                                              \
        put64(e + 8, (val));                                                                       \
        put64(e + 16, (size));                                                                     \
        nstr += l;                                                                                 \
        k++;                                                                                       \
    } while (0)
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if (!((f->reach & 1) && f->defined && !f->is_builtin)) {
            continue;
        }
        ADDSYM(f->name, BASE + HDRSZ + f->codepos, f->codelen, 0x12, 1); /* GLOBAL FUNC in .text */
    }
    for (Sym *y = global_syms(); y; y = y->next) {
        if (!(y->kind == SY_VAR && y->defined && (y->reach & 1))) {
            continue;
        }
        ADDSYM(y->name, y->bss ? bssaddr + y->off : dataaddr + y->off,
               y->ty->size > 0 ? y->ty->size : 0, 0x11, y->bss ? 3 : 2); /* GLOBAL OBJECT */
    }
#undef ADDSYM
    *symtab = st;
    *symsz = k * 24;
    *strtab = str;
    *strsz = nstr;
}

/*
 * Generates the machine code for the parsed program, resolves the links and writes the
 * ELF executable.  Returns 0 on success, otherwise a message is stored in err.
 */
int gen_program(const char *outpath, char *err, int errlen, int mapmode)
{
    Sym *mainf = global_lookup("main"), *startf = global_lookup("_fc_start");
    if (!mainf || mainf->kind != SY_FUNC || !mainf->defined) {
        linker_error("Undefined symbol _main in module C0");
        return 1;
    }
    if (!startf || !startf->defined) {
        snprintf(err, errlen, "runtime start-up code missing");
        return 1;
    }
    nlinkmods = 0;
    mark(mainf);
    mark(startf);
    for (int i = 0; i < PG.nfuncs; i++) {
        if (PG.funcs[i]->reach & 2) {
            mark(PG.funcs[i]);
        }
    }
    for (int i = 0; i < PG.ndrel; i++) {
        if (PG.drel[i].kind == 2 && PG.drel[i].fn->kind == SY_FUNC) {
            mark(PG.drel[i].fn);
        }
    }
    if (PG.uses_fp && CX->opt.fp_mode) {
        Sym *r = global_lookup("__fpused");
        if (r) {
            mark(r);
        }
    } /* pulls in the floating-point run-time */
    mark_pragma_fns();
    if (CX->opt.stack_check) {
        Sym *a = global_lookup("__fc_stklimit"), *b = global_lookup("__fc_stkover");
        if (a) {
            mark(a);
        }
        if (b) {
            mark(b);
        }
    }
    {
        Sym *t = global_lookup("__sigtramp"), *d = global_lookup("__sig_dispatch");
        if (t && (t->reach & 1) && d) {
            mark(d);
        }
    }

    /* entry stub */
    b1(0x5F);
    b3(0x48, 0x89, 0xE6);
    b1(0x56);
    b1(0x57);
    b1(0xE8);
    add_cfix(3, startf, 0);
    b2(0x0F, 0x0B);
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if (!(f->reach & 1) || !f->defined) {
            continue;
        }
        if (f->is_builtin) {
            continue;
        }
        gen_function(f);
    }
    emit_proc_list("__fc_init", 0);
    emit_proc_list("__fc_exit", 1);
    emit_sigstubs();
    if (gerr_set) {
        snprintf(err, errlen, "%s", gerr);
        return 1;
    }

    int codeend = (ncode + 15) & ~15;
    int dataoff = HDRSZ + codeend;
    if (CX->opt.wx) {
        dataoff = (dataoff + 0xFFF) &
                  ~0xFFF; /* data on its own pages: the code segment can be R+X only */
    }
    uint32_t dataaddr = BASE + dataoff;
    int dataend = (PG.ndata + 15) & ~15;
    uint32_t bssaddr = dataaddr + dataend;
    int bad = 0;

#define SYMADDR(s, out)                                                                            \
    do {                                                                                           \
        if ((s)->kind == SY_FUNC) {                                                                \
            if ((s)->codepos < 0) {                                                                \
                linker_error("Undefined symbol _%s in module %s", (s)->name,                       \
                             file_name((mainf)->fileid));                                          \
                bad = 1;                                                                           \
                (out) = 0;                                                                         \
            } else                                                                                 \
                (out) = BASE + HDRSZ + (s)->codepos;                                               \
        } else if (!(s)->defined) {                                                                \
            linker_error("Undefined symbol _%s in module %s", (s)->name,                           \
                         file_name((mainf)->fileid));                                              \
            bad = 1;                                                                               \
            (out) = 0;                                                                             \
        } else                                                                                     \
            (out) = (s)->bss ? bssaddr + (s)->off : dataaddr + (s)->off;                           \
    } while (0)

    for (int i = 0; i < ncfix; i++) {
        CFix *f = &cfix[i];
        uint32_t a = 0;
        if (f->kind == 0) {
            a = dataaddr + (uint32_t)f->add;
        } else {
            SYMADDR(f->sym, a);
            a += (uint32_t)f->add;
        }
        if (f->kind == 3) {
            put32(code + f->pos, (uint32_t)((int)a - (int)(BASE + HDRSZ + f->pos + 4)));
        } else {
            put32(code + f->pos, a);
        }
    }
    for (int i = 0; i < PG.ndrel; i++) {
        uint32_t a = 0;
        if (PG.drel[i].kind == 0) {
            a = dataaddr + (uint32_t)PG.drel[i].add;
        } else {
            SYMADDR(PG.drel[i].fn ? PG.drel[i].fn : NULL, a);
            a += (uint32_t)PG.drel[i].add;
        }
        put64(PG.data + PG.drel[i].off, a);
    }
    if (bad) {
        return 1;
    }
    dbg_build(dataaddr, bssaddr);

    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if ((f->reach & 1) && f->defined && !f->is_builtin) {
            note_module(f->fileid, f->codelen);
        }
    }
    for (Sym *y = global_syms(); y; y = y->next) {
        if (y->kind == SY_VAR && y->defined && (y->reach & 1)) {
            note_module(y->fileid, y->ty->size > 0 ? y->ty->size : 0);
        }
    }
    if (mapmode > 0) {
        write_map(outpath, mapmode, BASE + HDRSZ, ncode, dataaddr, PG.ndata, bssaddr, PG.bss,
                  BASE + HDRSZ);
    }

    int filesz = dataoff + dataend;
    uint64_t memsz = filesz + PG.bss + 16;
    int symoff = 0, symsz = 0, stroff = 0, strsz = 0, shoff = 0, nsec = 0;
    uint8_t *symtab = NULL, *strtab = NULL;
    static const char shstr[] = "\0.text\0.data\0.bss\0.symtab\0.strtab\0.shstrtab";
    if (CX->opt.elf_symtab) {
        build_symtab(&symtab, &symsz, &strtab, &strsz, dataaddr, bssaddr);
    }
    int total = filesz;
    if (symtab) {
        symoff = (total + 7) & ~7;
        total = symoff + symsz;
        stroff = total;
        total += strsz;
        total += (int)sizeof shstr;
        shoff = (total + 7) & ~7;
        nsec = 7;
        total = shoff + nsec * 64;
    }
    uint8_t *img = calloc(1, total);
    memcpy(img + HDRSZ, code, ncode);
    memcpy(img + dataoff, PG.data, PG.ndata);
    uint8_t *h = img;
    memcpy(h,
           "\x7f"
           "ELF\2\1\1\0",
           8);
    put32(h + 16, 0);
    h[16] = 2;
    h[18] = 0x3E;
    h[20] = 1;                   /* ET_EXEC, x86-64, version */
    put64(h + 24, BASE + HDRSZ); /* entry */
    put64(h + 32, 64);           /* phoff */
    put32(h + 52, 0);
    h[52] = 64;
    h[54] = 56;
    h[56] = CX->opt.wx ? 2 : 1; /* ehsize, phentsize, phnum */
    uint8_t *p = h + 64;
    if (CX->opt.wx) {
        put32(p, 1);
        put32(p + 4, 5); /* PT_LOAD, R+X: header and code */
        put64(p + 8, 0);
        put64(p + 16, BASE);
        put64(p + 24, BASE);
        put64(p + 32, HDRSZ + codeend);
        put64(p + 40, HDRSZ + codeend);
        put64(p + 48, 0x1000);
        p += 56;
        put32(p, 1);
        put32(p + 4, 6); /* PT_LOAD, R+W: data and bss */
        put64(p + 8, dataoff);
        put64(p + 16, dataaddr);
        put64(p + 24, dataaddr);
        put64(p + 32, dataend);
        put64(p + 40, (uint64_t)dataend + PG.bss + 16);
        put64(p + 48, 0x1000);
    } else {
        put32(p, 1);
        put32(p + 4, 7); /* PT_LOAD, RWX */
        put64(p + 8, 0);
        put64(p + 16, BASE);
        put64(p + 24, BASE);
        put64(p + 32, filesz);
        put64(p + 40, memsz);
        put64(p + 48, 0x1000);
    }
    if (symtab) {
        memcpy(img + symoff, symtab, symsz);
        memcpy(img + stroff, strtab, strsz);
        int shstroff = stroff + strsz;
        memcpy(img + shstroff, shstr, sizeof shstr);
        put64(h + 40, shoff); /* e_shoff */
        h[58] = 64;
        h[60] = (uint8_t)nsec;
        h[62] = 6; /* shentsize, shnum, shstrndx */

        struct {
            int name, type;
            uint64_t flags, addr, off, size;
            int link, info;
            uint64_t align, entsize;
        } sh[7] = {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 6, BASE + HDRSZ, HDRSZ, (uint64_t)ncode, 0, 0, 16, 0}, /* .text   AX */
            {7, 1, 3, dataaddr, dataoff, (uint64_t)dataend, 0, 0, 16, 0}, /* .data   WA */
            {13, 8, 3, bssaddr, filesz, (uint64_t)PG.bss, 0, 0, 16, 0},   /* .bss    WA */
            {18, 2, 0, 0, symoff, (uint64_t)symsz, 5, 1, 8, 24},          /* .symtab */
            {26, 3, 0, 0, stroff, (uint64_t)strsz, 0, 0, 1, 0},           /* .strtab */
            {34, 3, 0, 0, shstroff, sizeof shstr, 0, 0, 1, 0},            /* .shstrtab */
        };

        for (int i = 0; i < 7; i++) {
            uint8_t *q = img + shoff + i * 64;
            put32(q, sh[i].name);
            put32(q + 4, sh[i].type);
            put64(q + 8, sh[i].flags);
            put64(q + 16, sh[i].addr);
            put64(q + 24, sh[i].off);
            put64(q + 32, sh[i].size);
            put32(q + 40, sh[i].link);
            put32(q + 44, sh[i].info);
            put64(q + 48, sh[i].align);
            put64(q + 56, sh[i].entsize);
        }
        free(symtab);
        free(strtab);
    }
    unlink(outpath);
    int fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        snprintf(err, errlen, "Cannot create '%s'", outpath);
        free(img);
        return 1;
    }
    ssize_t w = write(fd, img, total);
    close(fd);
    free(img);
    if (w != total) {
        snprintf(err, errlen, "Error writing output file");
        return 1;
    }
    chmod(outpath, 0755);
    return 0;
}

/* Clears the code generator state at the start of a compilation. */
void gen_reset(void)
{
    free(code);
    code = NULL;
    ncode = capcode = 0;
    free(cfix);
    cfix = NULL;
    ncfix = capcfix = 0;
    free(lpos);
    lpos = NULL;
    nlabels = caplabels = 0;
    free(jfix);
    jfix = NULL;
    njfix = capjfix = 0;
    free(gls);
    gls = NULL;
    ngl = capgl = 0;
    gerr_set = 0;
    ctl_sp = 0;
    mark_pos = last_push = pl_pos = -1;
    pl_n = 0;
    free(dln);
    dln = NULL;
    ndln = capdln = 0;
}
