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
static int gen_size; /* the function being generated is optimized for size */
static void prof_site(Node *n, int kind, int which);
static void prof_start(void);
static void mark(Sym *s);
static char gerr[256];
static int gerr_set;

#define IS_A64 (CX && CX->opt.target_arch == FC_ARCH_AARCH64)
#define BASE 0x400000
#define HDRSZ 176 /* ELF header + two program headers */

static void mov_rcx_rax(void);
static void put32(uint8_t *p, uint32_t v);

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

static void emit32(uint32_t v)
{
    d32((int)v);
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

/* AArch64 immediate loading */
static void a64_load_imm(int rd, long long v)
{
    if (v == 0) {
        emit32(0xaa1f03e0 | rd); /* mov xd, xzr */
        return;
    }
    uint64_t u = (uint64_t)v;
    uint16_t hw[4];
    hw[0] = u & 0xffff;
    hw[1] = (u >> 16) & 0xffff;
    hw[2] = (u >> 32) & 0xffff;
    hw[3] = (u >> 48) & 0xffff;
    int nzero = (hw[0] == 0) + (hw[1] == 0) + (hw[2] == 0) + (hw[3] == 0);

    uint64_t nu = ~u;
    uint16_t nhw[4];
    nhw[0] = nu & 0xffff;
    nhw[1] = (nu >> 16) & 0xffff;
    nhw[2] = (nu >> 32) & 0xffff;
    nhw[3] = (nu >> 48) & 0xffff;
    int n_nzero = (nhw[0] == 0) + (nhw[1] == 0) + (nhw[2] == 0) + (nhw[3] == 0);

    if (n_nzero > nzero) {
        int first = 1;
        for (int i = 0; i < 4; i++) {
            if (first) {
                if (nhw[i] != 0 || n_nzero == 4) {
                    emit32(0x92800000 | (i << 21) | ((uint32_t)nhw[i] << 5) | rd);
                    first = 0;
                }
            } else {
                if (hw[i] != 0xffff) {
                    emit32(0xf2800000 | (i << 21) | ((uint32_t)hw[i] << 5) | rd);
                }
            }
        }
    } else {
        int first = 1;
        for (int i = 0; i < 4; i++) {
            if (first) {
                if (hw[i] != 0 || nzero == 4) {
                    emit32(0xd2800000 | (i << 21) | ((uint32_t)hw[i] << 5) | rd);
                    first = 0;
                }
            } else {
                if (hw[i] != 0) {
                    emit32(0xf2800000 | (i << 21) | ((uint32_t)hw[i] << 5) | rd);
                }
            }
        }
    }
}

/* AArch64 address calculation: rd = rn + off */
static void a64_addr(int rd, int rn, long long off)
{
    if (off == 0) {
        if (rd != rn) {
            if (rn == 31 || rd == 31) {
                emit32(0x91000000 | (rn << 5) | rd); /* add xd, xn, #0 (mov with sp) */
            } else {
                emit32(0xaa0003e0 | (rn << 16) | rd); /* mov xd, xn */
            }
        }
    } else if (off > 0 && off <= 4095) {
        emit32(0x91000000 | ((uint32_t)off << 10) | (rn << 5) | rd); /* add xd, xn, #off */
    } else if (off < 0 && off >= -4095) {
        emit32(0xd1000000 | ((uint32_t)(-off) << 10) | (rn << 5) | rd); /* sub xd, xn, #-off */
    } else {
        int scratch = (rd != rn && rd != 31) ? rd : 2;
        a64_load_imm(scratch, off);
        if (rn == 31 || rd == 31) {
            emit32(0x8b2063e0 | (scratch << 16) | (rn << 5) | rd); /* add xd, sp, scratch */
        } else {
            emit32(0x8b000000 | (scratch << 16) | (rn << 5) | rd); /* add xd, xn, scratch */
        }
    }
}

/* AArch64 load/store with unscaled 9-bit offset, fallback to x2 */
static void a64_mem(uint32_t op_base, int rt, int rn, int off)
{
    if (off >= -256 && off <= 255) {
        emit32(op_base | (((uint32_t)off & 0x1ff) << 12) | (rn << 5) | rt);
    } else {
        a64_addr(2, rn, off);
        emit32(op_base | (2 << 5) | rt);
    }
}

/* CFix emitters for AArch64 */
static void a64_emit_addr_cfix(int kind, Sym *s, long long add, int rd)
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
    emit32(0xd2800000 | rd); /* movz rd, 0 */
    emit32(0xf2a00000 | rd); /* movk rd, 0, lsl 16 */
}

static void a64_emit_call_cfix(Sym *s, int tail)
{
    if (ncfix == capcfix) {
        capcfix = capcfix ? capcfix * 2 : 1024;
        cfix = realloc(cfix, capcfix * sizeof *cfix);
    }
    cfix[ncfix].pos = ncode;
    cfix[ncfix].sym = s;
    cfix[ncfix].add = 0;
    cfix[ncfix].kind = 3;
    ncfix++;
    emit32(tail ? 0x14000000 : 0x94000000); /* b or bl */
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
static int last_jump_pos = -1, last_jump_pinned; /* where the last jump starts, and whether a label
                                                    or line mark is at that spot */

/*
 * Binds label l to the current code position.  A jump that would only skip to this very
 * position is removed first when the jump optimisation is on.
 */
static void place(int l)
{
    if (IS_A64) {
        if (CX->opt.jump_opt && njfix && jfix[njfix - 1].label == l &&
            jfix[njfix - 1].pos == ncode - 4 && ncode >= 4 &&
            (*(uint32_t *)(code + ncode - 4) & 0xFC000000) == 0x14000000 &&
            !(pl_pos == ncode && pl_n >= 32)) {
            /* the previous instruction is 'b l' and l is placed right behind it: drop the jump */
            if (pl_pos == ncode) {
                for (int i = 0; i < pl_n; i++) {
                    lpos[pl_list[i]] = ncode - 4;
                }
            }
            if (ndln && dln_last_pos() == (uint32_t)ncode) {
                dln_set_last_pos(ncode - 4);
            }
            ncode -= 4;
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
        return;
    }
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
    if ((CX->opt.opt_flags & FCO_LAYOUT) && njfix >= 2 && ncode >= 11 && code[ncode - 5] == 0xE9 &&
        jfix[njfix - 1].pos == ncode - 4 && !jfix[njfix - 1].rel8 &&
        jfix[njfix - 2].pos == ncode - 9 && !jfix[njfix - 2].rel8 && jfix[njfix - 2].label == l &&
        code[ncode - 11] == 0x0F && (code[ncode - 10] & 0xF0) == 0x80 &&
        last_jump_pos == ncode - 5 && !last_jump_pinned && !(pl_pos == ncode && pl_n >= 32)) {
        /* branch folding:  jcc l; jmp m; l:  ->  jncc m; l: */
        code[ncode - 10] = 0x80 | ((code[ncode - 10] & 0x0F) ^ 1);
        jfix[njfix - 2].label = jfix[njfix - 1].label;
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
        if (mark_pos > ncode) {
            mark_pos = ncode;
        }
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

static int a64_cc(int x86_cc)
{
    switch (x86_cc) {
    case CC_E:
        return 0; /* EQ */
    case CC_NE:
        return 1; /* NE */
    case CC_AE:
        return 2; /* CS */
    case CC_B:
        return 3; /* CC */
    case CC_A:
        return 8; /* HI */
    case CC_BE:
        return 9; /* LS */
    case CC_GE:
        return 10; /* GE */
    case CC_L:
        return 11; /* LT */
    case CC_G:
        return 12; /* GT */
    case CC_LE:
        return 13; /* LE */
    default:
        return 0;
    }
}

static void jump_to(int l, int cond) /* cond < 0: jmp; else 0F 8x */
{
    last_jump_pos = ncode;
    last_jump_pinned = mark_pos == ncode;
    if (IS_A64) {
        if (njfix == capjfix) {
            capjfix = capjfix ? capjfix * 2 : 256;
            jfix = realloc(jfix, capjfix * sizeof *jfix);
        }
        jfix[njfix].pos = ncode;
        jfix[njfix].label = l;
        jfix[njfix].rel8 = 0;
        njfix++;
        if (cond < 0) {
            emit32(0x14000000); /* b */
        } else {
            emit32(0x54000000 | a64_cc(cond)); /* b.cond */
        }
        return;
    }
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

/* ---------- basic emitters ---------- */
static void push_rax(void)
{
    if (IS_A64) {
        emit32(0xf81f0fe0); /* str x0, [sp, #-16]! */
        last_push = ncode;
        return;
    }
    b1(0x50);
    last_push = ncode;
}

static int peep_push(void) /* -Z: the previous instruction is 'push rax' and nothing is pinned to
                              the code behind it */
{
    if (!CX->opt.redundant_loads || last_push != ncode || mark_pos == ncode) {
        return 0;
    }
    if (IS_A64) {
        ncode -= 4;
        last_push = -1;
        return 1;
    }
    ncode--;
    last_push = -1;
    return 1;
}

/* Emits pop rax, unless it cancels an immediately preceding push rax. */
static void pop_rax(void)
{
    if (!peep_push()) {
        if (IS_A64) {
            emit32(0xf84107e0); /* ldr x0, [sp], #16 */
        } else {
            b1(0x58);
        }
    }
}

/* Emits pop rcx; after a push rax this becomes the shorter mov rcx, rax. */
static void pop_rcx(void)
{
    if (peep_push()) {
        mov_rcx_rax();
    } else {
        if (IS_A64) {
            emit32(0xf84107e1); /* ldr x1, [sp], #16 */
        } else {
            b1(0x59);
        }
    }
} /* push rax; pop rcx  ->  mov rcx, rax */

/* Emits mov rcx, rax. */
static void mov_rcx_rax(void)
{
    if (IS_A64) {
        emit32(0xaa0003e1); /* mov x1, x0 */
    } else {
        b3(0x48, 0x89, 0xC1);
    }
}

/* Emits test rax, rax. */
static void test_rax(void)
{
    if (IS_A64) {
        emit32(0xf100001f); /* cmp x0, #0 */
    } else {
        b3(0x48, 0x85, 0xC0);
    }
}

/* Emits code that loads the constant v into rax, using the shortest encoding. */
static void load_imm(long long v)
{
    if (IS_A64) {
        a64_load_imm(0, v);
        return;
    }
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
    if (IS_A64) {
        a64_load_imm(1, v);
        return;
    }
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
    if (IS_A64) {
        emit32(0x9e670000); /* fmov d0, x0 */
        return;
    }
    b1(0x66);
    b4(0x48, 0x0F, 0x6E, 0xC0);
}

/* Emits movq xmm1, rcx. */
static void movq_x1_rcx(void)
{
    if (IS_A64) {
        emit32(0x9e670021); /* fmov d1, x1 */
        return;
    }
    b1(0x66);
    b4(0x48, 0x0F, 0x6E, 0xC9);
}

/* Emits movq rax, xmm0. */
static void movq_rax_x0(void)
{
    if (IS_A64) {
        emit32(0x9e660000); /* fmov x0, d0 */
        return;
    }
    b1(0x66);
    b4(0x48, 0x0F, 0x7E, 0xC0);
}

/* Emits the conversion of xmm0 from double to single precision. */
static void cvt_sd2ss(void)
{
    if (IS_A64) {
        emit32(0x1e624000); /* fcvt s0, d0 */
        return;
    }
    b4(0xF2, 0x0F, 0x5A, 0xC0);
}

/* Emits the conversion of xmm0 from single to double precision. */
static void cvt_ss2sd(void)
{
    if (IS_A64) {
        emit32(0x1e22c000); /* fcvt d0, s0 */
        return;
    }
    b4(0xF3, 0x0F, 0x5A, 0xC0);
}

static void gen_fop(int op) /* rax op rcx, both doubles -> rax */
{
    movq_x0_rax();
    movq_x1_rcx();
    if (IS_A64) {
        switch (op) {
        case B_ADD:
            emit32(0x1e612800);
            break; /* fadd d0, d0, d1 */
        case B_SUB:
            emit32(0x1e613800);
            break; /* fsub d0, d0, d1 */
        case B_MUL:
            emit32(0x1e610800);
            break; /* fmul d0, d0, d1 */
        case B_DIV:
            emit32(0x1e611800);
            break; /* fdiv d0, d0, d1 */
        }
        movq_rax_x0();
        return;
    }
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
    if (IS_A64) {
        emit32(0x1e612000); /* fcmp d0, d1 */
        int cond = 0;
        switch (op) {
        case B_EQ:
            cond = 0;
            break; /* eq */
        case B_NE:
            cond = 1;
            break; /* ne */
        case B_LT:
            cond = 4;
            break; /* mi */
        case B_LE:
            cond = 9;
            break; /* ls */
        case B_GT:
            cond = 12;
            break; /* gt */
        case B_GE:
            cond = 10;
            break; /* ge */
        }
        emit32(0x9a9f07e0 | ((cond ^ 1) << 12)); /* cset x0, cond */
        return;
    }
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
    if (IS_A64) {
        if (t->kind == TY_FLOAT) {
            emit32(0xbd400000); /* ldr s0, [x0] */
            emit32(0x1e22c000); /* fcvt d0, s0 */
            emit32(0x9e660000); /* fmov x0, d0 */
            return;
        }
        switch (t->size) {
        case 1:
            emit32(t->uns ? 0x39400000 : 0x39800000); /* ldrb w0 / ldrsb x0, [x0] */
            break;
        case 2:
            emit32(t->uns ? 0x79400000 : 0x79800000); /* ldrh w0 / ldrsh x0, [x0] */
            break;
        case 4:
            emit32(t->uns ? 0xb9400000 : 0xb9800000); /* ldr w0 / ldrsw x0, [x0] */
            break;
        default:
            emit32(0xf9400000); /* ldr x0, [x0] */
            break;
        }
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

/* reg (0 = rax, 1 = rcx) = local at [rbp+off] */
static void load_local_r(int off, Type *t, int reg)
{
    if (IS_A64) {
        if (t->kind == TY_FLOAT) {
            a64_mem(0xbc400000, 0, 29, off); /* ldur s0, [x29, #off] */
            emit32(0x1e22c000);              /* fcvt d0, s0 */
            emit32(0x9e660000 | reg);        /* fmov reg, d0 */
            return;
        }
        uint32_t op;
        switch (t->size) {
        case 1:
            op = t->uns ? 0x38400000 : 0x38800000;
            break; /* ldurb / ldursb */
        case 2:
            op = t->uns ? 0x78400000 : 0x78800000;
            break; /* ldurh / ldursh */
        case 4:
            op = t->uns ? 0xb8400000 : 0xb8800000;
            break; /* ldur w / ldursw */
        default:
            op = 0xf8400000;
            break; /* ldur x */
        }
        a64_mem(op, reg, 29, off);
        return;
    }
    int m = 0x85 | (reg << 3);
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
            b3(0x0F, 0xB6, m);
        } else {
            b4(0x48, 0x0F, 0xBE, m);
        }
        break;
    case 2:
        if (t->uns) {
            b3(0x0F, 0xB7, m);
        } else {
            b4(0x48, 0x0F, 0xBF, m);
        }
        break;
    case 4:
        if (t->uns) {
            b2(0x8B, m);
        } else {
            b3(0x48, 0x63, m);
        }
        break;
    default:
        b3(0x48, 0x8B, m);
    }
    d32(off);
}

/* rax = local at [rbp+off] */
static void load_local(int off, Type *t)
{
    load_local_r(off, t, 0);
}

/* ---- register variables: rbx, r12, r13, r14, r15 hold the locals chosen by opt_regalloc ---- */
static const int reg_num[6] = {0, 3, 12, 13, 14, 15};

/* rax = register variable r */
static void mov_rax_reg(int r)
{
    if (IS_A64) {
        emit32(0xaa0003e0 | ((18 + r) << 16)); /* mov x0, x(18+r) */
        return;
    }
    int n = reg_num[r];
    b3(0x48 | (n >= 8 ? 4 : 0), 0x89, 0xC0 | ((n & 7) << 3));
}

/* register variable r = rax */
static void mov_reg_rax(int r)
{
    if (IS_A64) {
        emit32(0xaa0003e0 | (18 + r)); /* mov x(18+r), x0 */
        return;
    }
    int n = reg_num[r];
    b3(0x48 | (n >= 8 ? 1 : 0), 0x89, 0xC0 | (n & 7));
}

/* rcx = register variable r */
static void mov_rcx_reg(int r)
{
    if (IS_A64) {
        emit32(0xaa0003e1 | ((18 + r) << 16)); /* mov x1, x(18+r) */
        return;
    }
    int n = reg_num[r];
    b3(0x48 | (n >= 8 ? 4 : 0), 0x89, 0xC1 | ((n & 7) << 3));
}

/* [rbp+off] = register r, or (load != 0) register r = [rbp+off] */
static void reg_frame(int r, int off, int load)
{
    if (IS_A64) {
        a64_mem(load ? 0xf8400000 : 0xf8000000, 18 + r, 29, off);
        return;
    }
    int n = reg_num[r];
    b3(0x48 | (n >= 8 ? 4 : 0), load ? 0x8B : 0x89, 0x85 | ((n & 7) << 3));
    d32(off);
}

/* Returns true for a local integer or pointer variable (in memory or in a register). */
static int simple_local(Node *n)
{
    return n->kind == N_VAR && n->sym->kind == SY_VAR && !n->sym->global &&
           (is_int(n->ty) || is_ptr(n->ty));
}

/* rax = value of the local variable n (see simple_local) */
static void load_simple(Node *n)
{
    if (n->sym->reg) {
        mov_rax_reg(n->sym->reg);
    } else {
        load_local(n->sym->off, n->ty);
    }
}

/* rcx = value of the local variable n, leaving rax alone */
static void load_simple_rcx(Node *n)
{
    if (n->sym->reg) {
        mov_rcx_reg(n->sym->reg);
    } else {
        load_local_r(n->sym->off, n->ty, 1);
    }
}

/* *(type*)rcx = rax */
static void store_ind(Type *t)
{
    if (IS_A64) {
        if (t->kind == TY_FLOAT) {
            emit32(0x9e670000); /* fmov d0, x0 */
            emit32(0x1e624000); /* fcvt s0, d0 */
            emit32(0xbd000020); /* str s0, [x1] */
            emit32(0x1e22c000); /* fcvt d0, s0 */
            emit32(0x9e660000); /* fmov x0, d0 */
            return;
        }
        switch (t->size) {
        case 1:
            emit32(0x39000020); /* strb w0, [x1] */
            break;
        case 2:
            emit32(0x79000020); /* strh w0, [x1] */
            break;
        case 4:
            emit32(0xb9000020); /* str w0, [x1] */
            break;
        default:
            emit32(0xf9000020); /* str x0, [x1] */
            break;
        }
        return;
    }
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
    if (IS_A64) {
        if (t->kind == TY_FLOAT) {
            emit32(0x9e670000);              /* fmov d0, x0 */
            emit32(0x1e624000);              /* fcvt s0, d0 */
            a64_mem(0xbc000000, 0, 29, off); /* stur s0, [x29, #off] */
            emit32(0x1e22c000);              /* fcvt d0, s0 */
            emit32(0x9e660000);              /* fmov x0, d0 */
            return;
        }
        uint32_t op;
        switch (t->size) {
        case 1:
            op = 0x38000000;
            break; /* sturb */
        case 2:
            op = 0x78000000;
            break; /* sturh */
        case 4:
            op = 0xb8000000;
            break; /* stur w */
        default:
            op = 0xf8000000;
            break; /* stur x */
        }
        a64_mem(op, 0, 29, off);
        return;
    }
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

/* The local variable n (see simple_local) = rax */
static void store_simple(Node *n)
{
    if (n->sym->reg) {
        mov_reg_rax(n->sym->reg);
    } else {
        store_local(n->sym->off, n->ty);
    }
}

/* ---------- bit fields: a field lives in a 32-bit unit at the member offset ---------- */
static int is_bf(Node *n)
{
    return n->kind == N_MEMBER && n->bw > 0;
}

static void bf_extract(Node *m) /* rax = unit value -> rax = field value */
{
    if (IS_A64) {
        if (m->buns) {
            emit32(0xd3400000 | (m->bsh << 16) |
                   ((m->bsh + m->bw - 1) << 10)); /* ubfx x0, x0, #sh, #w */
        } else {
            emit32(0x93400000 | (m->bsh << 16) |
                   ((m->bsh + m->bw - 1) << 10)); /* sbfx x0, x0, #sh, #w */
        }
        return;
    }
    b3(0x48, 0xC1, 0xE0);
    b1((uint8_t)(64 - m->bsh - m->bw)); /* shl rax,64-sh-w */
    b3(0x48, 0xC1, m->buns ? 0xE8 : 0xF8);
    b1((uint8_t)(64 - m->bw)); /* shr / sar rax,64-w */
}

static void bf_load(Node *m) /* rax = address of the unit -> rax = field value */
{
    if (IS_A64) {
        emit32(0xb9400000); /* ldr w0, [x0] */
    } else {
        b2(0x8B, 0x00); /* mov eax,[rax] */
    }
    bf_extract(m);
}

static void bf_store(Node *m) /* rax = value, rcx = address of the unit -> rax = stored value */
{
    if (IS_A64) {
        emit32(0xb9400022); /* ldr w2, [x1] */
        emit32(0xb3400000 | (((64 - m->bsh) & 63) << 16) | ((m->bw - 1) << 10) | (0 << 5) |
               2);          /* bfi x2, x0, #sh, #w */
        emit32(0xb9000022); /* str w2, [x1] */
        bf_extract(m);
        return;
    }
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
            if (IS_A64) {
                emit32(0x9e620000); /* scvtf d0, x0 */
                if (to->kind == TY_FLOAT) {
                    cvt_sd2ss();
                    cvt_ss2sd();
                }
                movq_rax_x0();
                return;
            }
            b1(0xF2);
            b4(0x48, 0x0F, 0x2A, 0xC0); /* cvtsi2sd xmm0,rax */
            if (to->kind == TY_FLOAT) {
                cvt_sd2ss();
                cvt_ss2sd();
            }
            movq_rax_x0();
        } else { /* floating -> integer */
            if (IS_A64) {
                movq_x0_rax();
                emit32(0x9e780000); /* fcvtzs x0, d0 */
                Type l = *ty_long;
                gen_cast(&l, to);
                return;
            }
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
    if (IS_A64) {
        switch (to->size) {
        case 1:
            emit32(to->uns ? 0x53001c00 : 0x93401c00); /* uxtb w0 / sxtb x0 */
            break;
        case 2:
            emit32(to->uns ? 0x53003c00 : 0x93403c00); /* uxth w0 / sxth x0 */
            break;
        case 4:
            emit32(to->uns ? 0x2a0003e0 : 0x93407c00); /* mov w0, w0 / sxtw x0 */
            break;
        }
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
        if (IS_A64) {
            movq_x0_rax();
            emit32(0x1e602008); /* fcmp d0, #0.0 */
            emit32(0x9a9f07e0); /* cset x0, ne */
            return;
        }
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
            if (IS_A64) {
                a64_emit_addr_cfix(2, s, 0, 0);
            } else {
                b1(0xB8);
                add_cfix(2, s, 0);
            }
        } else if (s->global) {
            if (IS_A64) {
                a64_emit_addr_cfix(1, s, 0, 0);
            } else {
                b1(0xB8);
                add_cfix(1, s, 0);
            }
        } else {
            if (IS_A64) {
                a64_addr(0, 29, s->off);
            } else {
                b3(0x48, 0x8D, 0x85);
                d32(s->off);
            }
        }
        return;
    }
    case N_DEREF:
        gen_expr(n->l);
        return;
    case N_MEMBER:
        gen_addr(n->l);
        if (n->val) {
            if (IS_A64) {
                a64_addr(0, 0, n->val);
            } else {
                b2(0x48, 0x05);
                d32((int)n->val);
            }
        }
        return;
    case N_STR:
        if (IS_A64) {
            a64_emit_addr_cfix(0, NULL, n->val, 0);
        } else {
            b1(0xB8);
            add_cfix(0, NULL, n->val);
        }
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

/* Returns true if v can be an immediate operand of a 64-bit instruction. */
static int fits_imm32(long long v)
{
    return v >= -0x80000000LL && v <= 0x7fffffffLL;
}

/* Evaluates the operands of the binary operation n: rax = left, rcx = right. */
static void gen_operands(Node *n)
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
    } else if ((CX->opt.opt_flags & FCO_ISEL) && simple_local(n->r)) {
        /* a local variable as right operand is loaded straight into rcx */
        gen_expr(n->l);
        load_simple_rcx(n->r);
    } else {
        gen_expr(n->l);
        push_rax();
        gen_expr(n->r);
        mov_rcx_rax();
        pop_rax();
    }
}

/* Returns the condition code that is true after cmp for the comparison operator op. */
static int cmp_cc(int op, int u)
{
    switch (op) {
    case B_EQ:
        return CC_E;
    case B_NE:
        return CC_NE;
    case B_LT:
        return u ? CC_B : CC_L;
    case B_LE:
        return u ? CC_BE : CC_LE;
    case B_GT:
        return u ? CC_A : CC_G;
    default:
        return u ? CC_AE : CC_GE;
    }
}

/* Emits the comparison n (integers or pointers) and returns the condition code of its result. */
static int gen_cmp(Node *n)
{
    if (IS_A64) {
        gen_operands(n);
        emit32(0xeb01001f); /* cmp x0, x1 */
        return cmp_cc(n->op, n->cmpty_uns);
    }
    if ((CX->opt.opt_flags & FCO_ISEL) && n->r->kind == N_NUM && fits_imm32(n->r->val)) {
        gen_expr(n->l);
        b2(0x48, 0x3D); /* cmp rax, imm32 */
        d32((int)n->r->val);
    } else {
        gen_operands(n);
        b3(0x48, 0x39, 0xC8); /* cmp rax, rcx */
    }
    return cmp_cc(n->op, n->cmpty_uns);
}

/* apply 'op' with rax = lhs (already converted), rcx = rhs */
static void gen_op_rr(int op, int u)
{
    if (IS_A64) {
        switch (op) {
        case B_ADD:
            emit32(0x8b010000);
            break; /* add x0, x0, x1 */
        case B_SUB:
            emit32(0xcb010000);
            break; /* sub x0, x0, x1 */
        case B_MUL:
            emit32(0x9b017c00);
            break; /* mul x0, x0, x1 */
        case B_DIV:
            emit32(u ? 0x9ac10800 : 0x9ac10c00); /* udiv / sdiv x0, x0, x1 */
            break;
        case B_MOD:
            emit32(u ? 0x9ac10802 : 0x9ac10c02); /* udiv / sdiv x2, x0, x1 */
            emit32(0x9b018040);                  /* msub x0, x2, x1, x0 */
            break;
        case B_AND:
            emit32(0x8a010000);
            break; /* and x0, x0, x1 */
        case B_OR:
            emit32(0xaa010000);
            break; /* orr x0, x0, x1 */
        case B_XOR:
            emit32(0xca010000);
            break; /* eor x0, x0, x1 */
        case B_SHL:
            emit32(0x9ac12000);
            break; /* lsl x0, x0, x1 */
        case B_SHR:
            emit32(u ? 0x9ac12400 : 0x9ac12800); /* lsr / asr x0, x0, x1 */
            break;
        }
        return;
    }
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

/* Emits the binary operation n on n->l and n->r; the result is left in rax. */
static void gen_bin(Node *n)
{
    if (IS_A64) {
        if (!is_flt(n->l->ty) && n->op >= B_EQ) {
            int cc = gen_cmp(n);
            int a64 = a64_cc(cc);
            emit32(0x9a9f07e0 | ((a64 ^ 1) << 12)); /* cset x0, a64 */
            return;
        }
        gen_operands(n);
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
            }
            return;
        }
        gen_op_rr(n->op, u);
        gen_norm(n->ty);
        return;
    }
    if (!is_flt(n->l->ty) && n->op >= B_EQ) {
        int cc = gen_cmp(n);
        b3(0x0F, 0x90 + cc, 0xC0);
        b3(0x0F, 0xB6, 0xC0);
        return;
    }
    if ((CX->opt.opt_flags & FCO_ISEL) && !is_flt(n->l->ty) && n->r->kind == N_NUM &&
        fits_imm32(n->r->val)) {
        long long v = n->r->val;
        int done = 1;
        switch (n->op) {
        case B_ADD:
        case B_SUB:
        case B_AND:
        case B_OR:
        case B_XOR:
            gen_expr(n->l);
            b2(0x48, n->op == B_ADD   ? 0x05
                     : n->op == B_SUB ? 0x2D
                     : n->op == B_AND ? 0x25
                     : n->op == B_OR  ? 0x0D
                                      : 0x35);
            d32((int)v);
            break;
        case B_MUL:
            gen_expr(n->l);
            b3(0x48, 0x69, 0xC0); /* imul rax, rax, imm32 */
            d32((int)v);
            break;
        case B_SHL:
        case B_SHR:
            if (v < 0 || v > 63) {
                done = 0;
                break;
            }
            gen_expr(n->l);
            b3(0x48, 0xC1, n->op == B_SHL ? 0xE0 : (n->cmpty_uns ? 0xE8 : 0xF8));
            b1((int)v);
            break;
        default:
            done = 0;
        }
        if (done) {
            gen_norm(n->ty);
            return;
        }
    }
    gen_operands(n);
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

static int a64_syscall_num(int x86_num)
{
    switch (x86_num) {
    case 0:
        return 63; /* read */
    case 1:
        return 64; /* write */
    case 3:
        return 57; /* close */
    case 5:
        return 80; /* fstat */
    case 8:
        return 62; /* lseek */
    case 9:
        return 222; /* mmap */
    case 11:
        return 215; /* munmap */
    case 12:
        return 214; /* brk */
    case 13:
        return 134; /* rt_sigaction */
    case 14:
        return 135; /* rt_sigprocmask */
    case 16:
        return 29; /* ioctl */
    case 22:
        return 59; /* pipe2 */
    case 32:
        return 23; /* dup */
    case 35:
        return 101; /* nanosleep */
    case 39:
        return 172; /* getpid */
    case 59:
        return 221; /* execve */
    case 60:
        return 93; /* exit */
    case 61:
        return 260; /* wait4 */
    case 62:
        return 129; /* kill */
    case 72:
        return 25; /* fcntl */
    case 73:
        return 32; /* flock */
    case 74:
        return 82; /* fsync */
    case 77:
        return 46; /* ftruncate */
    case 79:
        return 17; /* getcwd */
    case 80:
        return 49; /* chdir */
    case 91:
        return 52; /* fchmod */
    case 95:
        return 166; /* umask */
    case 96:
        return 169; /* gettimeofday */
    case 97:
        return 163; /* getrlimit */
    case 99:
        return 179; /* sysinfo */
    case 110:
        return 173; /* getppid */
    case 137:
        return 43; /* statfs */
    case 7:
        return 73; /* ppoll */
    case 160:
        return 164; /* setrlimit */
    case 164:
        return 170; /* settimeofday */
    case 217:
        return 61; /* getdents64 */
    case 228:
        return 113; /* clock_gettime */
    case 231:
        return 94; /* exit_group */
    case 280:
        return 88; /* utimensat */
    default:
        return x86_num;
    }
}

static inline void a64_mov_rr(int rd, int rm)
{
    emit32(0xaa0003e0 | (rm << 16) | rd); /* mov xd, xm */
}

static void gen_a64_syscall(int argc, Node **args)
{
    /* Evaluate args 1..argc-1 onto stack */
    for (int i = 1; i < argc; i++) {
        gen_expr(args[i]);
        push_rax();
    }
    /* Pop in reverse order to set up x0..x5 */
    for (int i = argc - 1; i >= 1; i--) {
        int reg = i - 1;
        emit32(0xf84107e0 | reg); /* ldr x(reg), [sp], #16 */
    }
    /* Check syscall number */
    int num = (argc > 0 && args[0]->kind == N_NUM) ? (int)args[0]->val : -1;
    switch (num) {
    case 7: {                  /* SYS_poll -> ppoll(fds, nfds, &ts, NULL, 0) */
        emit32(0xf100005f);  /* cmp x2, #0 */
        emit32(0x540000ea);  /* b.ge +28 (to L_pos) */
        emit32(0xaa1f03e2);  /* mov x2, xzr */
        emit32(0xaa1f03e3);  /* mov x3, xzr */
        emit32(0xaa1f03e4);  /* mov x4, xzr */
        a64_load_imm(8, 73); /* SYS_ppoll */
        emit32(0xd4000001);  /* svc #0 */
        emit32(0x1400000f);  /* b +60 (to L_done) */
        /* L_pos (offset +28 from b.ge): */
        emit32(0xd10043ff);       /* sub sp, sp, #16 */
        a64_load_imm(3, 1000);    /* mov x3, #1000 */
        emit32(0x9ac30c44);       /* sdiv x4, x2, x3 */
        emit32(0x9b038885);       /* msub x5, x4, x3, x2 */
        a64_load_imm(3, 1000000); /* mov x3, #1000000 */
        emit32(0x9b037ca5);       /* mul x5, x5, x3 */
        emit32(0xa90017e4);       /* stp x4, x5, [sp] */
        emit32(0x910003e2);       /* mov x2, sp */
        emit32(0xaa1f03e3);       /* mov x3, xzr */
        emit32(0xaa1f03e4);       /* mov x4, xzr */
        a64_load_imm(8, 73);      /* SYS_ppoll */
        emit32(0xd4000001);       /* svc #0 */
        emit32(0x910043ff);       /* add sp, sp, #16 */
        /* L_done: */
        return;
    }
    case 2:                    /* SYS_open -> openat(AT_FDCWD, path, flags, mode) */
        a64_mov_rr(3, 2);      /* mov x3, x2 (mode) */
        a64_mov_rr(2, 1);      /* mov x2, x1 (flags) */
        a64_mov_rr(1, 0);      /* mov x1, x0 (path) */
        a64_load_imm(0, -100); /* AT_FDCWD */
        a64_load_imm(8, 56);   /* SYS_openat */
        break;
    case 4:               /* SYS_stat -> newfstatat(AT_FDCWD, path, statbuf, 0) */
        a64_mov_rr(2, 1); /* mov x2, x1 (statbuf) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        emit32(0xaa1f03e3);  /* mov x3, xzr (flags=0) */
        a64_load_imm(8, 79); /* SYS_newfstatat */
        break;
    case 6:               /* SYS_lstat -> newfstatat(AT_FDCWD, path, statbuf, 0x100) */
        a64_mov_rr(2, 1); /* mov x2, x1 (statbuf) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        a64_load_imm(3, 0x100); /* AT_SYMLINK_NOFOLLOW */
        a64_load_imm(8, 79);
        break;
    case 21:              /* SYS_access -> faccessat(AT_FDCWD, path, mode, 0) */
        a64_mov_rr(2, 1); /* mov x2, x1 (mode) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        emit32(0xaa1f03e3);  /* mov x3, xzr */
        a64_load_imm(8, 48); /* SYS_faccessat */
        break;
    case 22:                 /* SYS_pipe -> pipe2(fds, 0) */
        emit32(0xaa1f03e1);  /* mov x1, xzr */
        a64_load_imm(8, 59); /* SYS_pipe2 */
        break;
    case 33:                 /* SYS_dup2 -> dup3(old, new, 0) */
        emit32(0xeb01001f);  /* cmp x0, x1 */
        emit32(0x54000060);  /* b.eq +12 (if eq return x0) */
        emit32(0xaa1f03e2);  /* mov x2, xzr */
        a64_load_imm(8, 24); /* SYS_dup3 */
        emit32(0xd4000001);  /* svc #0 */
        return;
    case 57:                  /* SYS_fork -> clone(17, 0, 0, 0, 0) */
        a64_load_imm(0, 17);  /* SIGCHLD */
        emit32(0xaa1f03e1);   /* mov x1, xzr */
        emit32(0xaa1f03e2);   /* mov x2, xzr */
        emit32(0xaa1f03e3);   /* mov x3, xzr */
        emit32(0xaa1f03e4);   /* mov x4, xzr */
        a64_load_imm(8, 220); /* SYS_clone */
        break;
    case 82:              /* SYS_rename -> renameat(AT_FDCWD, old, AT_FDCWD, new) */
        a64_mov_rr(3, 1); /* mov x3, x1 (new) */
        a64_mov_rr(1, 0); /* mov x1, x0 (old) */
        a64_load_imm(0, -100);
        a64_load_imm(2, -100);
        a64_load_imm(8, 38); /* SYS_renameat */
        break;
    case 83:              /* SYS_mkdir -> mkdirat(AT_FDCWD, path, mode) */
        a64_mov_rr(2, 1); /* mov x2, x1 (mode) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        a64_load_imm(8, 34); /* SYS_mkdirat */
        break;
    case 84:              /* SYS_rmdir -> unlinkat(AT_FDCWD, path, 0x200) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        a64_load_imm(2, 0x200); /* AT_REMOVEDIR */
        a64_load_imm(8, 35);    /* SYS_unlinkat */
        break;
    case 87:              /* SYS_unlink -> unlinkat(AT_FDCWD, path, 0) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        emit32(0xaa1f03e2);  /* mov x2, xzr */
        a64_load_imm(8, 35); /* SYS_unlinkat */
        break;
    case 89:              /* SYS_readlink -> readlinkat(AT_FDCWD, path, buf, bufsz) */
        a64_mov_rr(3, 2); /* mov x3, x2 (bufsz) */
        a64_mov_rr(2, 1); /* mov x2, x1 (buf) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        a64_load_imm(8, 78); /* SYS_readlinkat */
        break;
    case 90:              /* SYS_chmod -> fchmodat(AT_FDCWD, path, mode) */
        a64_mov_rr(2, 1); /* mov x2, x1 (mode) */
        a64_mov_rr(1, 0); /* mov x1, x0 (path) */
        a64_load_imm(0, -100);
        a64_load_imm(8, 53); /* SYS_fchmodat */
        break;
    case 201:                 /* SYS_time -> clock_gettime(0, &ts) */
        emit32(0xd10043ff);   /* sub sp, sp, #16 */
        emit32(0xaa1f03e0);   /* mov x0, xzr */
        emit32(0x910003e1);   /* mov x1, sp */
        a64_load_imm(8, 113); /* SYS_clock_gettime */
        emit32(0xd4000001);   /* svc #0 */
        emit32(0xf94003e0);   /* ldr x0, [sp] (tv_sec) */
        emit32(0x910043ff);   /* add sp, sp, #16 */
        return;
    default:
        if (num >= 0) {
            a64_load_imm(8, a64_syscall_num(num));
        } else if (argc > 0) {
            gen_expr(args[0]);
            emit32(0xaa0003e8); /* mov x8, x0 */
        }
        break;
    }
    emit32(0xd4000001); /* svc #0 */
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
    if (IS_A64) {
        if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__sqrt")) {
            if (argc > 0) {
                gen_expr(args[0]);
            }
            emit32(0x9e670000); /* fmov d0, x0 */
            emit32(0x1e61c000); /* fsqrt d0, d0 */
            emit32(0x9e660000); /* fmov x0, d0 */
            return;
        }
        if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__setjmp")) {
            if (argc > 0) {
                gen_expr(args[0]);
            }
            emit32(0xf900001d); /* str x29, [x0] */
            emit32(0x910003e1); /* mov x1, sp */
            emit32(0xf9000401); /* str x1, [x0, #8] */
            emit32(0x10000061); /* adr x1, 12 */
            emit32(0xf9000801); /* str x1, [x0, #16] */
            emit32(0xaa1f03e0); /* mov x0, xzr */
            return;
        }
        if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__longjmp")) {
            if (argc > 1) {
                gen_expr(args[0]);
                push_rax();
                gen_expr(args[1]);
                pop_rcx();
            } else {
                if (argc > 0) {
                    gen_expr(args[0]);
                }
                mov_rcx_rax();
                load_imm(1);
            }
            emit32(0xf100001f); /* cmp x0, #0 */
            emit32(0x54000041); /* b.ne +8 */
            emit32(0xd2800020); /* mov x0, #1 */
            emit32(0xf940003d); /* ldr x29, [x1] */
            emit32(0xf9400422); /* ldr x2, [x1, #8] */
            emit32(0x9100005f); /* mov sp, x2 */
            emit32(0xf940083e); /* ldr x30, [x1, #16] */
            emit32(0xd61f03c0); /* br x30 */
            return;
        }
        if (n->sym && n->sym->is_builtin) {
            gen_a64_syscall(argc, args);
            return;
        }
        /* Evaluate arguments right-to-left onto stack */
        for (int i = argc - 1; i >= 0; i--) {
            gen_expr(args[i]);
            if (args[i]->ty->kind == TY_FLOAT) {
                emit32(0x9e670000); /* fmov d0, x0 */
                emit32(0x1e624000); /* fcvt s0, d0 */
                emit32(0x1e260000); /* fmov w0, s0 */
            }
            push_rax(); /* str x0, [sp, #-16]! */
        }
        int total_bytes = 0;
        if (is_agg(n->ty)) {
            total_bytes += 8;
        }
        for (int i = 0; i < argc; i++) {
            total_bytes += arg_slot(args[i]->ty);
        }
        int alloc_bytes = (total_bytes + 15) & ~15;
        if (alloc_bytes) {
            a64_addr(31, 31, -alloc_bytes);
        }
        int cur_off = 0;
        if (is_agg(n->ty)) {
            a64_addr(0, 29, n->cmpty_uns);
            emit32(0xf90003e0); /* str x0, [sp] */
            cur_off += 8;
        }
        for (int i = 0; i < argc; i++) {
            int src_off = alloc_bytes + (i * 16);
            if (is_agg(args[i]->ty)) {
                a64_mem(0xf8400000, 1, 31, src_off); /* ldr x1, [sp, #src_off] (src ptr) */
                a64_addr(0, 31, cur_off);            /* dest */
                a64_load_imm(3, args[i]->ty->size);
                emit32(0x38401424); /* ldrb w4, [x1], #1 */
                emit32(0x38001404); /* strb w4, [x0], #1 */
                emit32(0xd1000463); /* sub x3, x3, #1 */
                emit32(0xb5ffffa3); /* cbnz x3, .-12 */
            } else if (args[i]->ty->kind == TY_FLOAT) {
                a64_mem(0xb8400000, 2, 31, src_off); /* ldr w2, [sp, #src_off] */
                a64_mem(0xb8000000, 2, 31, cur_off); /* str w2, [sp, #cur_off] */
            } else {
                a64_mem(0xf8400000, 0, 31, src_off); /* ldr x0, [sp, #src_off] */
                a64_mem(0xf8000000, 0, 31, cur_off); /* str x0, [sp, #cur_off] */
            }
            cur_off += arg_slot(args[i]->ty);
        }
        if (n->sym) {
            a64_emit_call_cfix(n->sym, 0);
        } else {
            gen_expr(n->l);
            emit32(0xd63f0000); /* blr x0 */
        }
        int cleanup_bytes = alloc_bytes + argc * 16;
        if (cleanup_bytes) {
            a64_addr(31, 31, cleanup_bytes);
        }
        return;
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
    if (simple_local(n->l) && ((CX->opt.opt_flags & FCO_ISEL) || n->l->sym->reg)) {
        load_simple(n->l); /* rax = old */
        if (!pre) {
            push_rax();
        }
        if (IS_A64) {
            a64_addr(0, 0, inc ? n->val : -n->val);
        } else {
            b2(0x48, inc ? 0x05 : 0x2D);
            d32((int)n->val);
        }
        gen_norm(t);
        store_simple(n->l);
        if (!pre) {
            pop_rax();
        }
        return;
    }
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
        load_imm_rcx(0x3FF0000000000000LL);
        movq_x1_rcx();
        if (IS_A64) {
            emit32(inc ? 0x1e612800 : 0x1e613800); /* fadd / fsub d0, d0, d1 */
        } else {
            if (inc) {
                b4(0xF2, 0x0F, 0x58, 0xC1);
            } else {
                b4(0xF2, 0x0F, 0x5C, 0xC1);
            }
        }
        movq_rax_x0();
    } else {
        if (IS_A64) {
            a64_addr(0, 0, inc ? n->val : -n->val);
        } else {
            if (inc) {
                b2(0x48, 0x05);
                d32((int)n->val);
            } else {
                b2(0x48, 0x2D);
                d32((int)n->val);
            }
        }
        gen_norm(t);
    }
    if (!pre) {
        if (IS_A64) {
            emit32(0xf9400be1); /* ldr x1, [sp, #16] */
        } else {
            b4(0x48, 0x8B, 0x4C, 0x24);
            b1(8); /* mov rcx,[rsp+8] */
        }
    } else {
        if (IS_A64) {
            emit32(0xf94003e1); /* ldr x1, [sp] */
        } else {
            b3(0x48, 0x8B, 0x0C);
            b1(0x24); /* mov rcx,[rsp] */
        }
    }
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
    if (simple_local(n->l) && !is_flt(ct) && ((CX->opt.opt_flags & FCO_ISEL) || n->l->sym->reg)) {
        gen_expr(n->r);
        mov_rcx_rax();
        load_simple(n->l);
        gen_cast(lt, ct);
        gen_op_rr(n->op, ct->uns);
        gen_norm(ct);
        gen_cast(ct, lt);
        store_simple(n->l);
        return;
    }
    gen_addr(n->l);
    push_rax();
    gen_expr(n->r);
    mov_rcx_rax();
    if (IS_A64) {
        emit32(0xf94003e0); /* ldr x0, [sp] */
    } else {
        b4(0x48, 0x8B, 0x04, 0x24); /* mov rax,[rsp] */
    }
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

/* Emits a jump to label when the truth value of c equals "when" (and runs on otherwise).  With
 * instruction selection a comparison becomes cmp + jcc, and ! && || turn into jumps. */
static void gen_branch(Node *c, int label, int when)
{
    if (!(CX->opt.opt_flags & FCO_ISEL)) {
        gen_truth(c);
        test_rax();
        jump_to(label, when ? CC_NE : CC_E);
        return;
    }
    switch (c->kind) {
    case N_NUM:
        if ((c->val != 0) == (when != 0)) {
            jump_to(label, -1);
        }
        return;
    case N_NOT:
        gen_branch(c->l, label, !when);
        return;
    case N_LOGAND:
    case N_LOGOR: {
        int and = c->kind == N_LOGAND;
        if (and== !when) { /* and-false or or-true: either operand decides */
            gen_branch(c->l, label, when);
            gen_branch(c->r, label, when);
        } else {
            int skip = newlabel();
            gen_branch(c->l, skip, !when);
            gen_branch(c->r, label, when);
            place(skip);
        }
        return;
    }
    case N_BIN:
        if (c->op >= B_EQ && !is_flt(c->l->ty)) {
            int cc = gen_cmp(c);
            jump_to(label, when ? cc : (cc ^ 1));
            return;
        }
        break;
    default:
        break;
    }
    gen_truth(c);
    test_rax();
    jump_to(label, when ? CC_NE : CC_E);
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
            if (n->sym->reg) {
                mov_rax_reg(n->sym->reg);
            } else {
                load_local(n->sym->off, n->ty);
            }
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
            if (IS_A64) {
                emit32(0xaa0003e2); /* mov x2, x0 (dest) */
                pop_rcx();          /* x1 = src */
                a64_load_imm(3, n->ty->size);
                emit32(0x38401424); /* ldrb w4, [x1], #1 */
                emit32(0x38001404); /* strb w4, [x0], #1 */
                emit32(0xd1000463); /* sub x3, x3, #1 */
                emit32(0xb5ffffa3); /* cbnz x3, .-12 */
                emit32(0xaa0203e0); /* mov x0, x2 */
            } else {
                b3(0x48, 0x89, 0xC7); /* mov rdi,rax */
                b1(0x5E);             /* pop rsi */
                b1(0xB9);
                d32(n->ty->size);     /* mov ecx,size */
                b2(0xF3, 0xA4);       /* rep movsb */
                b3(0x48, 0x89, 0xF8); /* mov rax,rdi */
            }
            return;
        }
        gen_expr(n->r);
        if (n->l->kind == N_VAR && !n->l->sym->global) {
            if (n->l->sym->reg) {
                gen_norm(n->l->ty);
                mov_reg_rax(n->l->sym->reg);
            } else {
                store_local(n->l->sym->off, n->l->ty);
            }
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
        gen_branch(n->c, lf, 0);
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
        if (IS_A64) {
            emit32(0x9a9f17e0); /* cset x0, eq */
        } else {
            b3(0x0F, 0x94, 0xC0);
            b3(0x0F, 0xB6, 0xC0);
        }
        return;
    case N_NEG:
        gen_expr(n->l);
        if (is_flt(n->ty)) {
            if (IS_A64) {
                movq_x0_rax();
                emit32(0x1e614000); /* fneg d0, d0 */
                movq_rax_x0();
                return;
            }
            b2(0x48, 0xB9);
            d64((long long)0x8000000000000000ULL);
            b3(0x48, 0x31, 0xC8);
            return;
        }
        if (IS_A64) {
            emit32(0xcb0003e0); /* neg x0, x0 */
        } else {
            b3(0x48, 0xF7, 0xD8);
        }
        gen_norm(n->ty);
        return;
    case N_BITNOT:
        gen_expr(n->l);
        if (IS_A64) {
            emit32(0xaa2003e0); /* mvn x0, x0 */
        } else {
            b3(0x48, 0xF7, 0xD0);
        }
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

/* Restores the callee-saved registers the function saved in its prologue. */
static void gen_restore_regs(void)
{
    for (int r = 1; r <= 5; r++) {
        if (gen_fn->regmask & (1 << (r - 1))) {
            reg_frame(r, gen_fn->rsave[r - 1], 1);
        }
    }
}

/* Tail-call optimization: "return g(args);" stores the arguments into the argument slots of this
 * function, restores the registers, removes the frame and jumps to g.  g then returns straight to
 * the caller of this function, which removes the slots.  Returns 0 if the call does not qualify. */
static int gen_tailcall(Node *call)
{
    Sym *g = call->sym;
    Type *ft = gen_fn->ty;
    if (!(CX->opt.opt_flags & FCO_TAILCALL) || dbg_rec || !gen_fn->tail_ok ||
        call->kind != N_CALL || !g || g->kind != SY_FUNC || g->is_builtin || is_agg(call->ty) ||
        is_agg(ft->ret) || ft->noproto) {
        return 0;
    }
    Node *args[256];
    int argc = 0;
    for (Node *a = call->r; a && argc < 256; a = a->next) {
        if (is_agg(a->ty)) {
            return 0;
        }
        args[argc++] = a;
    }
    if (argc > ft->nparams || argc >= 256) {
        return 0;
    }
    for (int i = argc - 1; i >= 0; i--) { /* as in a call: last argument first */
        gen_expr(args[i]);
        if (args[i]->ty->kind == TY_FLOAT) {
            if (IS_A64) {
                emit32(0x9e670000); /* fmov d0, x0 */
                emit32(0x1e624000); /* fcvt s0, d0 */
                emit32(0x1e260000); /* fmov w0, s0 */
            } else {
                movq_x0_rax();
                cvt_sd2ss();
                b1(0x66);
                b3(0x0F, 0x7E, 0xC0); /* movd eax,xmm0 */
            }
        }
        push_rax();
    }
    for (int i = 0; i < argc; i++) {
        pop_rax();
        if (IS_A64) {
            a64_mem(0xf8000000, 0, 29, 16 + 8 * i);
        } else {
            b3(0x48, 0x89, 0x85);
            d32(16 + 8 * i); /* mov [rbp+16+8*i], rax */
        }
    }
    gen_restore_regs();
    if (IS_A64) {
        emit32(0x910003bf);       /* mov sp, x29 */
        emit32(0xa8c17bfd);       /* ldp x29, x30, [sp], #16 */
        a64_emit_call_cfix(g, 1); /* b g */
    } else {
        b1(0xC9); /* leave */
        b1(0xE9);
        add_cfix(3, g, 0); /* jmp g */
    }
    return 1;
}

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
        if (IS_A64) {
            if (s->ty->size > 0) {
                a64_addr(0, 29, s->off);
                a64_load_imm(1, s->ty->size);
                emit32(0x3800141f); /* strb wzr, [x0], #1 */
                emit32(0xd1000421); /* sub x1, x1, #1 */
                emit32(0xb5ffffc1); /* cbnz x1, .-8 */
            }
        } else {
            b3(0x48, 0x8D, 0xBD);
            d32(s->off); /* lea rdi,[rbp+off] */
            b1(0xB9);
            d32(s->ty->size);
            b2(0x31, 0xC0);
            b2(0xF3, 0xAA);
        }
        return;
    }
    case S_IF: {
        int lelse = newlabel(), lend = newlabel();
        note_line(n->line);
        prof_site(n, 'I', 'E');
        gen_branch(n->c, lelse, 0);
        prof_site(n, 'I', 'T');
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
        prof_site(n, 'L', 'E');
        if ((CX->opt.opt_flags & FCO_LAYOUT) && !gen_size && !dbg_rec && n->c->kind != N_NUM) {
            /* loop rotation: the test sits at the bottom, one conditional jump per iteration */
            int lcond = newlabel();
            note_line(n->line);
            jump_to(lcond, -1);
            place(ltop);
            prof_site(n, 'L', 'T');
            brk_label[++ctl_sp] = lend;
            cont_label[ctl_sp] = lcond;
            gen_stmt(n->l);
            ctl_sp--;
            place(lcond);
            note_line(n->line);
            gen_branch(n->c, ltop, 1);
            place(lend);
            return;
        }
        place(ltop);
        note_line(n->line);
        gen_branch(n->c, lend, 0);
        prof_site(n, 'L', 'T');
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
        prof_site(n, 'L', 'E');
        place(ltop);
        prof_site(n, 'L', 'T');
        brk_label[++ctl_sp] = lend;
        cont_label[ctl_sp] = lcont;
        gen_stmt(n->l);
        ctl_sp--;
        place(lcont);
        note_line(n->c->line);
        gen_branch(n->c, ltop, 1);
        place(lend);
        return;
    }
    case S_FOR: {
        int ltop = newlabel(), lcont = newlabel(), lend = newlabel();
        note_line(n->line);
        prof_site(n, 'L', 'E');
        gen_list(n->d);
        if ((CX->opt.opt_flags & FCO_LAYOUT) && !gen_size && !dbg_rec && n->c &&
            n->c->kind != N_NUM) {
            /* loop rotation: the test sits at the bottom, one conditional jump per iteration */
            int lcond = newlabel();
            jump_to(lcond, -1);
            place(ltop);
            prof_site(n, 'L', 'T');
            brk_label[++ctl_sp] = lend;
            cont_label[ctl_sp] = lcont;
            gen_stmt(n->l);
            ctl_sp--;
            place(lcont);
            note_line(n->line);
            if (n->r) {
                gen_expr(n->r);
            }
            place(lcond);
            note_line(n->line);
            gen_branch(n->c, ltop, 1);
            place(lend);
            return;
        }
        place(ltop);
        note_line(n->line);
        if (n->c) {
            gen_branch(n->c, lend, 0);
        }
        prof_site(n, 'L', 'T');
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
        if (n->l && gen_tailcall(n->l)) {
            return;
        }
        if (n->l) {
            gen_expr(n->l);
        }
        if (n->l &&
            is_agg(gen_fn->ty
                       ->ret)) { /* copy the value into the caller's buffer, return its address */
            if (IS_A64) {
                emit32(0xaa0003e1); /* mov x1, x0 (src) */
                emit32(0xf9400ba0); /* ldr x0, [x29, #16] (dest buffer) */
                emit32(0xaa0003e2); /* mov x2, x0 */
                a64_load_imm(3, gen_fn->ty->ret->size);
                emit32(0x38401424); /* ldrb w4, [x1], #1 */
                emit32(0x38001444); /* strb w4, [x2], #1 */
                emit32(0xd1000463); /* sub x3, x3, #1 */
                emit32(0xb5ffffa3); /* cbnz x3, .-12 */
            } else {
                b3(0x48, 0x89, 0xC6);       /* mov rsi,rax */
                b4(0x48, 0x8B, 0x7D, 0x10); /* mov rdi,[rbp+16] */
                b1(0xB9);
                d32((int)gen_fn->ty->ret->size); /* mov ecx,size */
                b2(0xF3, 0xA4);                  /* rep movsb */
                b4(0x48, 0x8B, 0x45, 0x10);      /* mov rax,[rbp+16] */
            }
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
            if (IS_A64) {
                load_imm_rcx(c->val);
                emit32(0xeb01001f); /* cmp x0, x1 */
                jump_to(c->label, CC_E);
            } else {
                b2(0x48, 0xB9);
                d64(c->val);          /* mov rcx,imm64 */
                b3(0x48, 0x39, 0xC8); /* cmp rax,rcx */
                jump_to(c->label, CC_E);
            }
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
    if (IS_A64) {
        return;
    }
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
/* ---------- profile instrumentation (-Pg) ---------- */
enum { PROF_SLOTS = 2048, PROF_KEYLEN = 40 };

static int prof_on, prof_n;
static Sym *prof_ctr, *prof_keys, *prof_cnt, *prof_file;
static char prof_key[PROF_SLOTS][PROF_KEYLEN];

/* Makes the profile run-time (counters, names, the routine that writes the file) part of the
 * program when -Pg is on. */
static void prof_start(void)
{
    prof_on = 0;
    prof_n = 0;
    if (CX->opt.pgo_mode != 1) {
        return;
    }
    prof_ctr = global_lookup("__fc_prof_ctr");
    prof_keys = global_lookup("__fc_prof_keys");
    prof_cnt = global_lookup("__fc_prof_n");
    prof_file = global_lookup("__fc_prof_file");
    Sym *dump = global_lookup("__fc_prof_dump");
    if (!prof_ctr || !prof_keys || !prof_cnt || !prof_file || !dump) {
        return;
    }
    mark(prof_ctr);
    mark(prof_keys);
    mark(prof_cnt);
    mark(prof_file);
    mark(dump);
    prof_on = 1;
}

/* Adds one to the counter of a place: F for the entry of the function, I for an if statement, L for
 * a loop; E counts how often it is reached, T how often its then part or body runs. */
static void prof_site(Node *n, int kind, int which)
{
    if (!prof_on || dbg_rec || gen_fn->from_rt || prof_n >= PROF_SLOTS || (n && n->bsh <= 0)) {
        return;
    }
    snprintf(prof_key[prof_n], PROF_KEYLEN, "%.20s/%c/%d/%c", gen_fn->name, kind, n ? n->bsh : 0,
             which);
    if (IS_A64) {
        a64_emit_addr_cfix(1, prof_ctr, 8L * prof_n, 0); /* x0 = &prof_ctr[prof_n] */
        emit32(0xf9400001);                              /* ldr x1, [x0] */
        emit32(0x91000421);                              /* add x1, x1, #1 */
        emit32(0xf9000001);                              /* str x1, [x0] */
    } else {
        b4(0x48, 0x83, 0x04, 0x25); /* add qword [counter], 1 */
        add_cfix(1, prof_ctr, 8L * prof_n);
        b1(0x01);
    }
    prof_n++;
}

/* Stores the counter names and the file name in the program's data. */
static void prof_finish(void)
{
    if (!prof_on) {
        return;
    }
    if (!prof_keys->bss && !prof_cnt->bss && !prof_file->bss) {
        for (int i = 0; i < prof_n; i++) {
            memcpy(PG.data + prof_keys->off + (long)i * PROF_KEYLEN, prof_key[i], PROF_KEYLEN);
        }
        memcpy(PG.data + prof_cnt->off, &prof_n, sizeof prof_n);
        snprintf((char *)PG.data + prof_file->off, 259, "%s", CX->opt.pgo_file);
    }
}

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
    if (CX->opt.pgo_mode && !f->from_rt) {
        opt_pgo_number(f);
    }
    gen_size = opt_fn_size(f);
    opt_function(f, dbg_rec);
    opt_regalloc(f, dbg_rec);
    int framepos;
    if (IS_A64) {
        emit32(0xa9bf7bfd); /* stp x29, x30, [sp, #-16]! */
        emit32(0x910003fd); /* mov x29, sp */
        framepos = ncode;
        emit32(0xd2800010); /* movz x16, #0 */
        emit32(0xf2a00010); /* movk x16, #0, lsl #16 */
        emit32(0xcb3063ff); /* sub sp, sp, x16 */
    } else {
        b1(0x55);
        b3(0x48, 0x89, 0xE5);
        b3(0x48, 0x81, 0xEC);
        framepos = ncode;
        d32(0);
    }
    if (CX->opt.stack_check &&
        fid_class(f->fileid) == FC_USER) { /* -N:  if (rsp < __fc_stklimit) __fc_stkover(); */
        Sym *lim = global_lookup("__fc_stklimit"), *over = global_lookup("__fc_stkover");
        if (lim && over) {
            if (IS_A64) {
                a64_emit_addr_cfix(1, lim, 0, 1); /* x1 = &__fc_stklimit */
                emit32(0xf9400021);               /* ldr x1, [x1] */
                emit32(0xeb2163ff);               /* cmp sp, x1 */
                emit32(0x54000042);               /* b.cs +8 (skip call) */
                a64_emit_call_cfix(over, 0);      /* bl __fc_stkover */
            } else {
                b4(0x48, 0x3B, 0x24, 0x25);
                add_cfix(1, lim, 0); /* cmp rsp, [__fc_stklimit] */
                b2(0x73, 0x05);      /* jae +5 */
                b1(0xE8);
                add_cfix(3, over, 0); /* call __fc_stkover */
            }
        }
    }
    for (int r = 1; r <= 5; r++) { /* save the callee-saved registers used for variables */
        if (f->regmask & (1 << (r - 1))) {
            reg_frame(r, f->rsave[r - 1], 0);
        }
    }
    for (int i = 0; i < f->ndbg; i++) { /* parameters that live in registers */
        Sym *p = f->dbgvars[i];
        if (p->is_param && p->reg) {
            load_local(p->off, p->ty);
            mov_reg_rax(p->reg);
        }
    }
    ret_label = newlabel();
    prof_site(NULL, 'F', 'E');
    gen_stmt(f->body);
    if (IS_A64) {
        emit32(0xd2800000); /* mov x0, #0 (falling off the end returns 0) */
    } else {
        b2(0x31, 0xC0); /* falling off the end returns 0 */
    }
    place(ret_label);
    note_line(f->body->cmpty_uns); /* the closing brace */
    if (f->ty->ret && f->ty->ret->kind == TY_VOID && !strcmp(f->name, "main")) {
        if (IS_A64) {
            emit32(0xd2800000); /* void main exits with 0 */
        } else {
            b2(0x31, 0xC0); /* void main exits with 0 */
        }
    }
    gen_restore_regs();
    if (IS_A64) {
        emit32(0x910003bf); /* mov sp, x29 */
        emit32(0xa8c17bfd); /* ldp x29, x30, [sp], #16 */
        emit32(0xd65f03c0); /* ret */
    } else {
        b2(0xC9, 0xC3); /* leave; ret */
    }
    if (CX->opt.jump_opt) {
        relax_jumps(f->codepos, cfix0, dln0);
    }
    f->codelen = ncode - f->codepos;
    int fs = (f->frame + 15) & ~15;
    if (IS_A64) {
        uint32_t insn1 = 0xd2800010 | (((uint32_t)fs & 0xffff) << 5);
        uint32_t insn2 = 0xf2a00010 | ((((uint32_t)fs >> 16) & 0xffff) << 5);
        put32(code + framepos, insn1);
        put32(code + framepos + 4, insn2);
        for (int i = 0; i < njfix; i++) {
            int rel = lpos[jfix[i].label] - jfix[i].pos;
            int offset = rel >> 2;
            uint32_t insn = code[jfix[i].pos] | (code[jfix[i].pos + 1] << 8) |
                            (code[jfix[i].pos + 2] << 16) | (code[jfix[i].pos + 3] << 24);
            if ((insn & 0xfe000000) == 0x54000000) {
                insn = (insn & ~((uint32_t)0x7ffff << 5)) | (((uint32_t)offset & 0x7ffff) << 5);
            } else if ((insn & 0x7c000000) == 0x14000000) {
                insn = (insn & ~0x03ffffff) | ((uint32_t)offset & 0x03ffffff);
            }
            put32(code + jfix[i].pos, insn);
        }
    } else {
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
    if (IS_A64) {
        emit32(0xa9bf7bfd); /* stp x29, x30, [sp, #-16]! */
        emit32(0x910003fd); /* mov x29, sp */
        for (int i = 0; i < n; i++) {
            a64_emit_call_cfix(pragma_fn(&pp_pragmas[sel[i]]), 0);
        }
        emit32(0xa8c17bfd); /* ldp x29, x30, [sp], #16 */
        emit32(0xd65f03c0); /* ret */
    } else {
        b1(0x55);
        b3(0x48, 0x89, 0xE5);
        for (int i = 0; i < n; i++) {
            b1(0xE8);
            add_cfix(3, pragma_fn(&pp_pragmas[sel[i]]), 0);
        }
        b2(0x5D, 0xC3);
    }
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
        if (IS_A64) {
            emit32(0xd10043ff);       /* sub sp, sp, #16 */
            emit32(0xf90003e0);       /* str x0, [sp] (sig) */
            emit32(0xf90007fe);       /* str x30, [sp, #8] */
            a64_emit_call_cfix(d, 0); /* bl __sig_dispatch */
            emit32(0xf94007fe);       /* ldr x30, [sp, #8] */
            emit32(0x910043ff);       /* add sp, sp, #16 */
            emit32(0xd65f03c0);       /* ret -> __sigrestorer */
        } else {
            b1(0x57); /* push rdi  (signal number) */
            b1(0xE8);
            add_cfix(3, d, 0);          /* call __sig_dispatch */
            b4(0x48, 0x83, 0xC4, 0x08); /* add rsp,8 */
            b1(0xC3);                   /* ret -> __sigrestorer */
        }
        t->codelen = ncode - t->codepos;
    }
    if (r && (r->reach & 1)) {
        r->codepos = ncode;
        r->defined = 1;
        if (IS_A64) {
            emit32(0xd2801168); /* mov x8, #139 (rt_sigreturn) */
            emit32(0xd4000001); /* svc #0 */
        } else {
            b1(0xB8);
            d32(15);        /* mov eax,15 (rt_sigreturn) */
            b2(0x0F, 0x05); /* syscall */
        }
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
    if (CX->opt.pgo_mode == 2) {
        opt_profile_load(CX->opt.pgo_file);
        opt_profile_order();
    }
    opt_program(dbg_rec);
    mark(mainf);
    prof_start();
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
    if (IS_A64) {
        emit32(0xd10043ff);            /* sub sp, sp, #16 */
        emit32(0xf9400be0);            /* ldr x0, [sp, #16] (argc) */
        emit32(0x910063e1);            /* add x1, sp, #24 (argv) */
        emit32(0xf90003e0);            /* str x0, [sp] */
        emit32(0xf90007e1);            /* str x1, [sp, #8] */
        a64_emit_call_cfix(startf, 0); /* bl _fc_start */
        emit32(0xd4200000);            /* brk #0 */
    } else {
        b1(0x5F);
        b3(0x48, 0x89, 0xE6);
        b1(0x56);
        b1(0x57);
        b1(0xE8);
        add_cfix(3, startf, 0);
        b2(0x0F, 0x0B);
    }
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
    prof_finish();
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
        int page_align = IS_A64 ? 0xFFFF : 0xFFF;
        dataoff = (dataoff + page_align) &
                  ~page_align; /* data on its own pages: the code segment can be R+X only */
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
        if (IS_A64) {
            if (f->kind == 3) {
                int rel = (int)a - (int)(BASE + HDRSZ + f->pos);
                int offset = rel >> 2;
                uint32_t insn = code[f->pos] | (code[f->pos + 1] << 8) | (code[f->pos + 2] << 16) |
                                (code[f->pos + 3] << 24);
                insn = (insn & ~0x03ffffff) | ((uint32_t)offset & 0x03ffffff);
                put32(code + f->pos, insn);
            } else {
                uint32_t insn1 = code[f->pos] | (code[f->pos + 1] << 8) | (code[f->pos + 2] << 16) |
                                 (code[f->pos + 3] << 24);
                uint32_t insn2 = code[f->pos + 4] | (code[f->pos + 5] << 8) |
                                 (code[f->pos + 6] << 16) | (code[f->pos + 7] << 24);
                insn1 = (insn1 & ~((uint32_t)0xffff << 5)) | (((uint32_t)a & 0xffff) << 5);
                insn2 = (insn2 & ~((uint32_t)0xffff << 5)) | ((((uint32_t)a >> 16) & 0xffff) << 5);
                put32(code + f->pos, insn1);
                put32(code + f->pos + 4, insn2);
            }
        } else {
            if (f->kind == 3) {
                put32(code + f->pos, (uint32_t)((int)a - (int)(BASE + HDRSZ + f->pos + 4)));
            } else {
                put32(code + f->pos, a);
            }
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
    h[18] = IS_A64 ? 0xB7 : 0x3E; /* EM_AARCH64 (0xB7) or EM_X86_64 (0x3E) */
    h[20] = 1;                    /* ET_EXEC, machine, version */
    put64(h + 24, BASE + HDRSZ);  /* entry */
    put64(h + 32, 64);            /* phoff */
    put32(h + 52, 0);
    h[52] = 64;
    h[54] = 56;
    h[56] = CX->opt.wx ? 2 : 1; /* ehsize, phentsize, phnum */
    uint8_t *p = h + 64;
    uint64_t palign = IS_A64 ? 0x10000 : 0x1000;
    if (CX->opt.wx) {
        put32(p, 1);
        put32(p + 4, 5); /* PT_LOAD, R+X: header and code */
        put64(p + 8, 0);
        put64(p + 16, BASE);
        put64(p + 24, BASE);
        put64(p + 32, HDRSZ + codeend);
        put64(p + 40, HDRSZ + codeend);
        put64(p + 48, palign);
        p += 56;
        put32(p, 1);
        put32(p + 4, 6); /* PT_LOAD, R+W: data and bss */
        put64(p + 8, dataoff);
        put64(p + 16, dataaddr);
        put64(p + 24, dataaddr);
        put64(p + 32, dataend);
        put64(p + 40, (uint64_t)dataend + PG.bss + 16);
        put64(p + 48, palign);
    } else {
        put32(p, 1);
        put32(p + 4, 7); /* PT_LOAD, RWX */
        put64(p + 8, 0);
        put64(p + 16, BASE);
        put64(p + 24, BASE);
        put64(p + 32, filesz);
        put64(p + 40, memsz);
        put64(p + 48, palign);
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
