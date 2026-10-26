/* Flash C - x86-64 code generator and ELF64 writer */
#include "fci.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static uint8_t *code; static int ncode, capcode;
typedef struct CFix { int pos; Sym *sym; long long add; int kind; } CFix;   /* kind 0 data off, 1 var, 2 func, 3 rel32 func */
static CFix *cfix; static int ncfix, capcfix;
static int *lpos; static int nlabels, caplabels;
typedef struct JFix { int pos, label; } JFix;
static JFix *jfix; static int njfix, capjfix;
typedef struct GL { char *name; int label; } GL;
static GL *gls; static int ngl, capgl;
static int brk_label[64], cont_label[64], ctl_sp;
static int ret_label;
static Sym *gen_fn;
static char gerr[256]; static int gerr_set;

#define BASE 0x400000
#define HDRSZ 120

static void b1(int x) { if (ncode == capcode) { capcode = capcode ? capcode * 2 : 65536; code = realloc(code, capcode); } code[ncode++] = (uint8_t)x; }
static void b2(int x, int y) { b1(x); b1(y); }
static void b3(int x, int y, int z) { b1(x); b1(y); b1(z); }
static void b4(int x, int y, int z, int w) { b1(x); b1(y); b1(z); b1(w); }
static void d32(int v) { for (int i = 0; i < 4; i++) b1((v >> (8 * i)) & 0xff); }
static void d64(long long v) { for (int i = 0; i < 8; i++) b1((int)((v >> (8 * i)) & 0xff)); }

static void add_cfix(int kind, Sym *s, long long add)
{
    if (ncfix == capcfix) { capcfix = capcfix ? capcfix * 2 : 1024; cfix = realloc(cfix, capcfix * sizeof *cfix); }
    cfix[ncfix].pos = ncode; cfix[ncfix].sym = s; cfix[ncfix].add = add; cfix[ncfix].kind = kind; ncfix++;
    d32(0);
}

static int newlabel(void)
{
    if (nlabels == caplabels) { caplabels = caplabels ? caplabels * 2 : 256; lpos = realloc(lpos, caplabels * sizeof(int)); }
    lpos[nlabels] = -1; return nlabels++;
}
static void place(int l) { lpos[l] = ncode; }
static void jump_to(int l, int cond)   /* cond < 0: jmp; else 0F 8x */
{
    if (cond < 0) b1(0xE9); else b2(0x0F, 0x80 + cond);
    if (njfix == capjfix) { capjfix = capjfix ? capjfix * 2 : 256; jfix = realloc(jfix, capjfix * sizeof *jfix); }
    jfix[njfix].pos = ncode; jfix[njfix].label = l; njfix++;
    d32(0);
}
enum { CC_E = 4, CC_NE = 5, CC_B = 2, CC_AE = 3, CC_BE = 6, CC_A = 7, CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G = 0xF };

/* ---------- basic emitters ---------- */
static void push_rax(void) { b1(0x50); }
static void pop_rax(void) { b1(0x58); }
static void pop_rcx(void) { b1(0x59); }
static void mov_rcx_rax(void) { b3(0x48, 0x89, 0xC1); }
static void test_rax(void) { b3(0x48, 0x85, 0xC0); }

static void load_imm(long long v)
{
    if (v == 0) b2(0x31, 0xC0);                         /* xor eax,eax */
    else if (v > 0 && v <= 0xffffffffLL) { b1(0xB8); d32((int)v); }
    else if (v >= -0x80000000LL && v < 0) { b3(0x48, 0xC7, 0xC0); d32((int)v); }
    else { b2(0x48, 0xB8); d64(v); }
}

/* ---- SSE2 helpers: a floating value lives in rax as its double bit pattern ---- */
static void movq_x0_rax(void) { b1(0x66); b4(0x48, 0x0F, 0x6E, 0xC0); }
static void movq_x1_rcx(void) { b1(0x66); b4(0x48, 0x0F, 0x6E, 0xC9); }
static void movq_rax_x0(void) { b1(0x66); b4(0x48, 0x0F, 0x7E, 0xC0); }
static void cvt_sd2ss(void) { b4(0xF2, 0x0F, 0x5A, 0xC0); }
static void cvt_ss2sd(void) { b4(0xF3, 0x0F, 0x5A, 0xC0); }

static void gen_fop(int op)      /* rax op rcx, both doubles -> rax */
{
    movq_x0_rax(); movq_x1_rcx();
    switch (op) {
    case B_ADD: b4(0xF2, 0x0F, 0x58, 0xC1); break;
    case B_SUB: b4(0xF2, 0x0F, 0x5C, 0xC1); break;
    case B_MUL: b4(0xF2, 0x0F, 0x59, 0xC1); break;
    case B_DIV: b4(0xF2, 0x0F, 0x5E, 0xC1); break;
    }
    movq_rax_x0();
}
static void gen_fcmp(int op)
{
    movq_x0_rax(); movq_x1_rcx();
    switch (op) {
    case B_EQ: b1(0x66); b3(0x0F, 0x2E, 0xC1); b3(0x0F, 0x94, 0xC0); b3(0x0F, 0x9B, 0xC1); b2(0x20, 0xC8); break;
    case B_NE: b1(0x66); b3(0x0F, 0x2E, 0xC1); b3(0x0F, 0x95, 0xC0); b3(0x0F, 0x9A, 0xC1); b2(0x08, 0xC8); break;
    case B_GT: b1(0x66); b3(0x0F, 0x2E, 0xC1); b3(0x0F, 0x97, 0xC0); break;
    case B_GE: b1(0x66); b3(0x0F, 0x2E, 0xC1); b3(0x0F, 0x93, 0xC0); break;
    case B_LT: b1(0x66); b3(0x0F, 0x2E, 0xC8); b3(0x0F, 0x97, 0xC0); break;
    case B_LE: b1(0x66); b3(0x0F, 0x2E, 0xC8); b3(0x0F, 0x93, 0xC0); break;
    }
    b3(0x0F, 0xB6, 0xC0);
}

static int is_mem_type(Type *t) { return t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_UNION || t->kind == TY_FUNC; }

/* rax = *(type*)rax */
static void load_ind(Type *t)
{
    if (is_mem_type(t)) return;
    if (t->kind == TY_FLOAT) { b4(0xF3, 0x0F, 0x10, 0x00); cvt_ss2sd(); movq_rax_x0(); return; }
    switch (t->size) {
    case 1: if (t->uns) b3(0x0F, 0xB6, 0x00); else b4(0x48, 0x0F, 0xBE, 0x00); break;
    case 2: if (t->uns) b3(0x0F, 0xB7, 0x00); else b4(0x48, 0x0F, 0xBF, 0x00); break;
    case 4: if (t->uns) b2(0x8B, 0x00); else b3(0x48, 0x63, 0x00); break;
    default: b3(0x48, 0x8B, 0x00);
    }
}
/* rax = local at [rbp+off] */
static void load_local(int off, Type *t)
{
    if (t->kind == TY_FLOAT) { b4(0xF3, 0x0F, 0x10, 0x85); d32(off); cvt_ss2sd(); movq_rax_x0(); return; }
    switch (t->size) {
    case 1: if (t->uns) b3(0x0F, 0xB6, 0x85); else b4(0x48, 0x0F, 0xBE, 0x85); break;
    case 2: if (t->uns) b3(0x0F, 0xB7, 0x85); else b4(0x48, 0x0F, 0xBF, 0x85); break;
    case 4: if (t->uns) b2(0x8B, 0x85); else b3(0x48, 0x63, 0x85); break;
    default: b3(0x48, 0x8B, 0x85);
    }
    d32(off);
}
/* *(type*)rcx = rax */
static void store_ind(Type *t)
{
    if (t->kind == TY_FLOAT) { movq_x0_rax(); cvt_sd2ss(); b4(0xF3, 0x0F, 0x11, 0x01); cvt_ss2sd(); movq_rax_x0(); return; }
    switch (t->size) {
    case 1: b2(0x88, 0x01); break;
    case 2: b3(0x66, 0x89, 0x01); break;
    case 4: b2(0x89, 0x01); break;
    default: b3(0x48, 0x89, 0x01);
    }
}
static void store_local(int off, Type *t)
{
    if (t->kind == TY_FLOAT) { movq_x0_rax(); cvt_sd2ss(); b4(0xF3, 0x0F, 0x11, 0x85); d32(off); cvt_ss2sd(); movq_rax_x0(); return; }
    switch (t->size) {
    case 1: b2(0x88, 0x85); break;
    case 2: b3(0x66, 0x89, 0x85); break;
    case 4: b2(0x89, 0x85); break;
    default: b3(0x48, 0x89, 0x85);
    }
    d32(off);
}

static void gen_cast(Type *from, Type *to)
{
    if (to->kind == TY_VOID || !is_scalar(to) || !is_scalar(from)) return;
    if (is_flt(to) || is_flt(from)) {
        if (is_flt(to) && is_flt(from)) {
            if (to->kind == TY_FLOAT && from->kind == TY_DOUBLE) { movq_x0_rax(); cvt_sd2ss(); cvt_ss2sd(); movq_rax_x0(); }
            return;
        }
        if (is_flt(to)) {                       /* integer -> floating */
            b1(0xF2); b4(0x48, 0x0F, 0x2A, 0xC0);   /* cvtsi2sd xmm0,rax */
            if (to->kind == TY_FLOAT) { cvt_sd2ss(); cvt_ss2sd(); }
            movq_rax_x0();
        } else {                                /* floating -> integer */
            movq_x0_rax();
            b1(0xF2); b4(0x48, 0x0F, 0x2C, 0xC0);   /* cvttsd2si rax,xmm0 */
            Type l = *ty_long; gen_cast(&l, to);
        }
        return;
    }
    if (to->size == 8) return;
    if (to->size == from->size && to->uns == from->uns) return;
    if (to->size > from->size && (!to->uns || from->uns)) return;
    switch (to->size) {
    case 1: if (to->uns) b3(0x0F, 0xB6, 0xC0); else b4(0x48, 0x0F, 0xBE, 0xC0); break;
    case 2: if (to->uns) b3(0x0F, 0xB7, 0xC0); else b4(0x48, 0x0F, 0xBF, 0xC0); break;
    case 4: if (to->uns) b2(0x89, 0xC0); else b3(0x48, 0x63, 0xC0); break;
    }
}
static void gen_norm(Type *t) { if (t->size < 8 && is_int(t)) { Type l = *ty_long; gen_cast(&l, t); } }

static void gen_expr(Node *n);
static void gen_addr(Node *n);
static void gen_truth(Node *c)      /* rax = value of c, nonzero iff true */
{
    gen_expr(c);
    if (is_flt(c->ty)) {
        movq_x0_rax(); b1(0x66); b3(0x0F, 0x57, 0xC9);   /* xorpd xmm1,xmm1 */
        b1(0x66); b3(0x0F, 0x2E, 0xC1);                                  /* ucomisd xmm0,xmm1 */
        b3(0x0F, 0x95, 0xC0); b3(0x0F, 0x9A, 0xC1); b2(0x08, 0xC8); b3(0x0F, 0xB6, 0xC0);
    }
}

static void gen_addr(Node *n)
{
    switch (n->k) {
    case N_VAR: {
        Sym *s = n->sym;
        if (s->kind == SY_FUNC) { b1(0xB8); add_cfix(2, s, 0); }
        else if (s->global) { b1(0xB8); add_cfix(1, s, 0); }
        else { b3(0x48, 0x8D, 0x85); d32(s->off); }
        return;
    }
    case N_DEREF: gen_expr(n->l); return;
    case N_MEMBER:
        gen_addr(n->l);
        if (n->val) { b2(0x48, 0x05); d32((int)n->val); }
        return;
    case N_STR: b1(0xB8); add_cfix(0, NULL, n->val); return;
    case N_ASSIGN: case N_COND: case N_COMMA: gen_expr(n); return;   /* aggregate rvalues yield their address */
    default: break;
    }
    if (!gerr_set) { snprintf(gerr, sizeof gerr, "internal: cannot take address (node %d)", n->k); gerr_set = 1; }
}

static void gen_bin(Node *n)
{
    gen_expr(n->l);
    push_rax();
    gen_expr(n->r);
    mov_rcx_rax();
    pop_rax();
    int u = n->cmpty_uns;
    if (is_flt(n->l->ty)) {
        if (n->op >= B_EQ) { gen_fcmp(n->op); return; }
        gen_fop(n->op);
        if (n->ty->kind == TY_FLOAT) { movq_x0_rax(); cvt_sd2ss(); cvt_ss2sd(); movq_rax_x0(); }   /* single precision result */
        return;
    }
    switch (n->op) {
    case B_ADD: b3(0x48, 0x01, 0xC8); break;
    case B_SUB: b3(0x48, 0x29, 0xC8); break;
    case B_MUL: b4(0x48, 0x0F, 0xAF, 0xC1); break;
    case B_DIV: case B_MOD:
        if (u) { b2(0x31, 0xD2); b3(0x48, 0xF7, 0xF1); }
        else { b2(0x48, 0x99); b3(0x48, 0xF7, 0xF9); }
        if (n->op == B_MOD) b3(0x48, 0x89, 0xD0);
        break;
    case B_AND: b3(0x48, 0x21, 0xC8); break;
    case B_OR: b3(0x48, 0x09, 0xC8); break;
    case B_XOR: b3(0x48, 0x31, 0xC8); break;
    case B_SHL: b3(0x48, 0xD3, 0xE0); break;
    case B_SHR: if (u) b3(0x48, 0xD3, 0xE8); else b3(0x48, 0xD3, 0xF8); break;
    default: {
        b3(0x48, 0x39, 0xC8);
        int cc = 0;
        switch (n->op) {
        case B_EQ: cc = CC_E; break; case B_NE: cc = CC_NE; break;
        case B_LT: cc = u ? CC_B : CC_L; break; case B_LE: cc = u ? CC_BE : CC_LE; break;
        case B_GT: cc = u ? CC_A : CC_G; break; case B_GE: cc = u ? CC_AE : CC_GE; break;
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
    case B_ADD: b3(0x48, 0x01, 0xC8); break;
    case B_SUB: b3(0x48, 0x29, 0xC8); break;
    case B_MUL: b4(0x48, 0x0F, 0xAF, 0xC1); break;
    case B_DIV: case B_MOD:
        if (u) { b2(0x31, 0xD2); b3(0x48, 0xF7, 0xF1); }
        else { b2(0x48, 0x99); b3(0x48, 0xF7, 0xF9); }
        if (op == B_MOD) b3(0x48, 0x89, 0xD0);
        break;
    case B_AND: b3(0x48, 0x21, 0xC8); break;
    case B_OR: b3(0x48, 0x09, 0xC8); break;
    case B_XOR: b3(0x48, 0x31, 0xC8); break;
    case B_SHL: b3(0x48, 0xD3, 0xE0); break;
    case B_SHR: if (u) b3(0x48, 0xD3, 0xE8); else b3(0x48, 0xD3, 0xF8); break;
    }
}

static void gen_call(Node *n)
{
    Node *args[256]; int argc = 0;
    for (Node *a = n->r; a && argc < 256; a = a->next) args[argc++] = a;
    for (int i = argc - 1; i >= 0; i--) {
        gen_expr(args[i]);
        if (args[i]->ty->kind == TY_FLOAT) { movq_x0_rax(); cvt_sd2ss(); b1(0x66); b3(0x0F, 0x7E, 0xC0); }   /* movd eax,xmm0 */
        push_rax();
    }
    if (n->sym && n->sym->is_builtin && !strcmp(n->sym->name, "__sqrt")) {
        pop_rax(); movq_x0_rax(); b4(0xF2, 0x0F, 0x51, 0xC0); movq_rax_x0();
        return;
    }
    if (n->sym && n->sym->is_builtin) {
        static const uint8_t pops[7][2] = {{0x58,0},{0x5F,0},{0x5E,0},{0x5A,0},{0x5A,0x41},{0x58,0x41},{0x59,0x41}};
        for (int i = 0; i < argc && i < 7; i++) { if (pops[i][1]) b1(pops[i][1]); b1(pops[i][0]); }
        b2(0x0F, 0x05);
        return;
    }
    if (n->sym) { b1(0xE8); add_cfix(3, n->sym, 0); }
    else { gen_expr(n->l); b2(0xFF, 0xD0); }
    if (argc) { b3(0x48, 0x81, 0xC4); d32(argc * 8); }
}

static void gen_incdec(Node *n)
{
    int pre = n->k == N_PREINC || n->k == N_PREDEC;
    int inc = n->k == N_PREINC || n->k == N_POSTINC;
    Type *t = n->l->ty;
    gen_addr(n->l);
    push_rax();                 /* [addr] */
    load_ind(t);                /* rax = old */
    if (!pre) push_rax();       /* [addr, old] */
    if (is_flt(t)) {
        movq_x0_rax(); b2(0x48, 0xB9); d64(0x3FF0000000000000LL); movq_x1_rcx();
        if (inc) b4(0xF2, 0x0F, 0x58, 0xC1); else b4(0xF2, 0x0F, 0x5C, 0xC1);
        movq_rax_x0();
    } else {
        if (inc) { b2(0x48, 0x05); d32((int)n->val); } else { b2(0x48, 0x2D); d32((int)n->val); }
        gen_norm(t);
    }
    if (!pre) { b4(0x48, 0x8B, 0x4C, 0x24); b1(8); }       /* mov rcx,[rsp+8] */
    else { b3(0x48, 0x8B, 0x0C); b1(0x24); }               /* mov rcx,[rsp] */
    store_ind(t);
    if (!pre) { pop_rax(); pop_rcx(); }                    /* rax = old; drop addr */
    else pop_rcx();
}

static void gen_assignop(Node *n)
{
    Type *lt = n->l->ty, *ct = n->cty;
    gen_addr(n->l);
    push_rax();
    gen_expr(n->r);
    mov_rcx_rax();
    b4(0x48, 0x8B, 0x04, 0x24);          /* mov rax,[rsp] */
    load_ind(lt);
    gen_cast(lt, ct);
    if (is_flt(ct)) { gen_fop(n->op); if (ct->kind == TY_FLOAT) { movq_x0_rax(); cvt_sd2ss(); cvt_ss2sd(); movq_rax_x0(); } } else { gen_op_rr(n->op, ct->uns); gen_norm(ct); }
    gen_cast(ct, lt);
    pop_rcx();
    store_ind(lt);
}

static void gen_expr(Node *n)
{
    switch (n->k) {
    case N_NUM: load_imm(n->val); return;
    case N_FNUM: { double dv = n->fval; long long bits; memcpy(&bits, &dv, 8); load_imm(bits); return; }
    case N_STR: gen_addr(n); return;
    case N_VAR:
        if (is_mem_type(n->ty)) { gen_addr(n); return; }
        if (!n->sym->global && n->sym->kind == SY_VAR) { load_local(n->sym->off, n->ty); return; }
        gen_addr(n); load_ind(n->ty); return;
    case N_ADDR: gen_addr(n->l); return;
    case N_DEREF: gen_expr(n->l); load_ind(n->ty); return;
    case N_MEMBER: gen_addr(n); load_ind(n->ty); return;
    case N_ASSIGN:
        if (is_agg(n->ty)) {
            gen_expr(n->r); push_rax();
            gen_addr(n->l);
            b3(0x48, 0x89, 0xC7);               /* mov rdi,rax */
            b1(0x5E);                           /* pop rsi */
            b1(0xB9); d32(n->ty->size);         /* mov ecx,size */
            b2(0xF3, 0xA4);                     /* rep movsb */
            b3(0x48, 0x89, 0xF8);               /* mov rax,rdi */
            return;
        }
        gen_expr(n->r);
        if (n->l->k == N_VAR && !n->l->sym->global) { store_local(n->l->sym->off, n->l->ty); return; }
        push_rax();
        gen_addr(n->l);
        mov_rcx_rax();
        pop_rax();
        store_ind(n->l->ty);
        return;
    case N_ASSIGNOP: gen_assignop(n); return;
    case N_COND: {
        int lf = newlabel(), le = newlabel();
        gen_truth(n->c); test_rax(); jump_to(lf, CC_E);
        gen_expr(n->l); jump_to(le, -1);
        place(lf); gen_expr(n->r);
        place(le); return;
    }
    case N_COMMA: gen_expr(n->l); gen_expr(n->r); return;
    case N_LOGAND: case N_LOGOR: {
        int lshort = newlabel(), le = newlabel();
        int and = n->k == N_LOGAND;
        gen_truth(n->l); test_rax(); jump_to(lshort, and ? CC_E : CC_NE);
        gen_truth(n->r); test_rax(); jump_to(lshort, and ? CC_E : CC_NE);
        load_imm(and ? 1 : 0); jump_to(le, -1);
        place(lshort); load_imm(and ? 0 : 1);
        place(le); return;
    }
    case N_NOT: gen_truth(n->l); test_rax(); b3(0x0F, 0x94, 0xC0); b3(0x0F, 0xB6, 0xC0); return;
    case N_NEG: gen_expr(n->l);
        if (is_flt(n->ty)) { b2(0x48, 0xB9); d64((long long)0x8000000000000000ULL); b3(0x48, 0x31, 0xC8); return; }
        b3(0x48, 0xF7, 0xD8); gen_norm(n->ty); return;
    case N_BITNOT: gen_expr(n->l); b3(0x48, 0xF7, 0xD0); gen_norm(n->ty); return;
    case N_BIN: gen_bin(n); return;
    case N_CALL: gen_call(n); return;
    case N_CAST: gen_expr(n->l); gen_cast(n->l->ty, n->ty); return;
    case N_PREINC: case N_POSTINC: case N_PREDEC: case N_POSTDEC: gen_incdec(n); return;
    default:
        if (!gerr_set) { snprintf(gerr, sizeof gerr, "internal: unknown expression node %d", n->k); gerr_set = 1; }
    }
}

/* ---------- statements ---------- */
static int goto_label(const char *name)
{
    for (int i = 0; i < ngl; i++) if (gls[i].name == name) return gls[i].label;
    if (ngl == capgl) { capgl = capgl ? capgl * 2 : 16; gls = realloc(gls, capgl * sizeof *gls); }
    gls[ngl].name = (char *)name; gls[ngl].label = newlabel();
    return gls[ngl++].label;
}

static void gen_stmt(Node *n);
static void gen_list(Node *n) { for (; n; n = n->next) gen_stmt(n); }

static void gen_stmt(Node *n)
{
    switch (n->k) {
    case S_NOP: return;
    case S_EXPR: gen_expr(n->l); return;
    case S_BLOCK: gen_list(n->l); return;
    case S_ZERO: {
        Sym *s = n->sym;
        b3(0x48, 0x8D, 0xBD); d32(s->off);      /* lea rdi,[rbp+off] */
        b1(0xB9); d32(s->ty->size);
        b2(0x31, 0xC0);
        b2(0xF3, 0xAA);
        return;
    }
    case S_IF: {
        int lelse = newlabel(), lend = newlabel();
        gen_truth(n->c); test_rax(); jump_to(lelse, CC_E);
        gen_stmt(n->l);
        if (n->r) { jump_to(lend, -1); place(lelse); gen_stmt(n->r); place(lend); }
        else place(lelse);
        return;
    }
    case S_WHILE: {
        int ltop = newlabel(), lend = newlabel();
        place(ltop);
        gen_truth(n->c); test_rax(); jump_to(lend, CC_E);
        brk_label[++ctl_sp] = lend; cont_label[ctl_sp] = ltop;
        gen_stmt(n->l);
        ctl_sp--;
        jump_to(ltop, -1); place(lend);
        return;
    }
    case S_DO: {
        int ltop = newlabel(), lcont = newlabel(), lend = newlabel();
        place(ltop);
        brk_label[++ctl_sp] = lend; cont_label[ctl_sp] = lcont;
        gen_stmt(n->l);
        ctl_sp--;
        place(lcont);
        gen_truth(n->c); test_rax(); jump_to(ltop, CC_NE);
        place(lend);
        return;
    }
    case S_FOR: {
        int ltop = newlabel(), lcont = newlabel(), lend = newlabel();
        gen_list(n->d);
        place(ltop);
        if (n->c) { gen_truth(n->c); test_rax(); jump_to(lend, CC_E); }
        brk_label[++ctl_sp] = lend; cont_label[ctl_sp] = lcont;
        gen_stmt(n->l);
        ctl_sp--;
        place(lcont);
        if (n->r) gen_expr(n->r);
        jump_to(ltop, -1); place(lend);
        return;
    }
    case S_RETURN:
        if (n->l) gen_expr(n->l);
        jump_to(ret_label, -1);
        return;
    case S_BREAK: jump_to(brk_label[ctl_sp], -1); return;
    case S_CONTINUE: {
        int i = ctl_sp;
        while (i > 0 && cont_label[i] < 0) i--;
        jump_to(cont_label[i], -1); return;
    }
    case S_SWITCH: {
        int lend = newlabel(), ldef = -1;
        gen_expr(n->c);
        for (Node *c = n->cases; c; c = c->r) {
            c->label = newlabel();
            if (c->k == S_DEFAULT) { ldef = c->label; continue; }
            b2(0x48, 0xB9); d64(c->val);          /* mov rcx,imm64 */
            b3(0x48, 0x39, 0xC8);                 /* cmp rax,rcx */
            jump_to(c->label, CC_E);
        }
        jump_to(ldef >= 0 ? ldef : lend, -1);
        brk_label[++ctl_sp] = lend; cont_label[ctl_sp] = -1;
        gen_stmt(n->l);
        ctl_sp--;
        place(lend);
        return;
    }
    case S_CASE: case S_DEFAULT: place(n->label); gen_stmt(n->l); return;
    case S_LABEL: place(goto_label(n->str)); gen_stmt(n->l); return;
    case S_GOTO: jump_to(goto_label(n->str), -1); return;
    default:
        if (!gerr_set) { snprintf(gerr, sizeof gerr, "internal: unknown statement node %d", n->k); gerr_set = 1; }
    }
}

static void gen_function(Sym *f)
{
    f->codepos = ncode;
    gen_fn = f; ngl = 0; ctl_sp = 0; nlabels = 0; njfix = 0;
    cont_label[0] = -1;
    b1(0x55); b3(0x48, 0x89, 0xE5);
    b3(0x48, 0x81, 0xEC);
    int framepos = ncode; d32(0);
    ret_label = newlabel();
    gen_stmt(f->body);
    b2(0x31, 0xC0);                           /* falling off the end returns 0 */
    place(ret_label);
    if (f->ty->ret && f->ty->ret->kind == TY_VOID && !strcmp(f->name, "main")) b2(0x31, 0xC0);  /* void main exits with 0 */
    b2(0xC9, 0xC3);                           /* leave; ret */
    int fs = (f->frame + 15) & ~15;
    for (int i = 0; i < 4; i++) code[framepos + i] = (fs >> (8 * i)) & 0xff;
    for (int i = 0; i < njfix; i++) {
        int rel = lpos[jfix[i].label] - (jfix[i].pos + 4);
        for (int k = 0; k < 4; k++) code[jfix[i].pos + k] = (rel >> (8 * k)) & 0xff;
    }
}

/* ---------- program ---------- */
static void mark(Sym *s)
{
    if (s->reach & 1) return;
    s->reach |= 1;
    for (int i = 0; i < s->nrefs; i++) mark(s->refs[i]);
}

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xff; }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (v >> (8 * i)) & 0xff; }

static void linker_error(const char *fmt, ...)
{
    char msg[300]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    CX->errors++;
    if (CX->diag) CX->diag(CX->user, FC_FATAL + 1, "", 0, msg);
}

int gen_program(const char *outpath, char *err, int errlen)
{
    Sym *mainf = global_lookup("main"), *startf = global_lookup("_fc_start");
    if (!mainf || mainf->kind != SY_FUNC || !mainf->defined) { linker_error("Undefined symbol _main in module C0"); return 1; }
    if (!startf || !startf->defined) { snprintf(err, errlen, "runtime start-up code missing"); return 1; }
    mark(mainf); mark(startf);
    for (int i = 0; i < PG.nfuncs; i++) if (PG.funcs[i]->reach & 2) mark(PG.funcs[i]);
    /* any symbol referenced from a global initialiser keeps its function alive */
    for (int i = 0; i < PG.nfuncs; i++) if (PG.funcs[i]->reach & 2) mark(PG.funcs[i]);
    for (int i = 0; i < PG.ndrel; i++) if (PG.drel[i].kind == 2 && PG.drel[i].fn->kind == SY_FUNC) mark(PG.drel[i].fn);

    /* entry stub */
    b1(0x5F); b3(0x48, 0x89, 0xE6); b1(0x56); b1(0x57);
    b1(0xE8); add_cfix(3, startf, 0);
    b2(0x0F, 0x0B);
    for (int i = 0; i < PG.nfuncs; i++) {
        Sym *f = PG.funcs[i];
        if (!(f->reach & 1) || !f->defined) continue;
        if (f->is_builtin) continue;
        gen_function(f);
    }
    if (gerr_set) { snprintf(err, errlen, "%s", gerr); return 1; }

    int codeend = (ncode + 15) & ~15;
    uint32_t dataaddr = BASE + HDRSZ + codeend;
    int dataend = (PG.ndata + 15) & ~15;
    uint32_t bssaddr = dataaddr + dataend;
    int bad = 0;

    #define SYMADDR(s, out) do { \
        if ((s)->kind == SY_FUNC) { \
            if ((s)->codepos < 0) { linker_error("Undefined symbol _%s in module %s", (s)->name, file_name((mainf)->fileid)); bad = 1; (out) = 0; } \
            else (out) = BASE + HDRSZ + (s)->codepos; \
        } else if (!(s)->defined) { linker_error("Undefined symbol _%s in module %s", (s)->name, file_name((mainf)->fileid)); bad = 1; (out) = 0; } \
        else (out) = (s)->bss ? bssaddr + (s)->off : dataaddr + (s)->off; } while (0)

    for (int i = 0; i < ncfix; i++) {
        CFix *f = &cfix[i];
        uint32_t a = 0;
        if (f->kind == 0) a = dataaddr + (uint32_t)f->add;
        else { SYMADDR(f->sym, a); a += (uint32_t)f->add; }
        if (f->kind == 3) put32(code + f->pos, (uint32_t)((int)a - (int)(BASE + HDRSZ + f->pos + 4)));
        else put32(code + f->pos, a);
    }
    for (int i = 0; i < PG.ndrel; i++) {
        uint32_t a = 0;
        if (PG.drel[i].kind == 0) a = dataaddr + (uint32_t)PG.drel[i].add;
        else { SYMADDR(PG.drel[i].fn ? PG.drel[i].fn : NULL, a); a += (uint32_t)PG.drel[i].add; }
        put64(PG.data + PG.drel[i].off, a);
    }
    if (bad) return 1;

    int filesz = HDRSZ + codeend + dataend;
    uint64_t memsz = filesz + PG.bss + 16;
    uint8_t *img = calloc(1, filesz);
    memcpy(img + HDRSZ, code, ncode);
    memcpy(img + HDRSZ + codeend, PG.data, PG.ndata);
    uint8_t *h = img;
    memcpy(h, "\x7f" "ELF\2\1\1\0", 8);
    put32(h + 16, 0); h[16] = 2; h[18] = 0x3E; h[20] = 1;      /* ET_EXEC, x86-64, version */
    put64(h + 24, BASE + HDRSZ);                               /* entry */
    put64(h + 32, 64);                                         /* phoff */
    put32(h + 52, 0); h[52] = 64; h[54] = 56; h[56] = 1;       /* ehsize, phentsize, phnum */
    uint8_t *p = h + 64;
    put32(p, 1); put32(p + 4, 7);                              /* PT_LOAD, RWX */
    put64(p + 8, 0); put64(p + 16, BASE); put64(p + 24, BASE);
    put64(p + 32, filesz); put64(p + 40, memsz); put64(p + 48, 0x1000);
    unlink(outpath);
    int fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) { snprintf(err, errlen, "Cannot create '%s'", outpath); free(img); return 1; }
    ssize_t w = write(fd, img, filesz);
    close(fd); free(img);
    if (w != filesz) { snprintf(err, errlen, "Error writing output file"); return 1; }
    chmod(outpath, 0755);
    return 0;
}

void gen_reset(void)
{
    free(code); code = NULL; ncode = capcode = 0;
    free(cfix); cfix = NULL; ncfix = capcfix = 0;
    free(lpos); lpos = NULL; nlabels = caplabels = 0;
    free(jfix); jfix = NULL; njfix = capjfix = 0;
    free(gls); gls = NULL; ngl = capgl = 0;
    gerr_set = 0; ctl_sp = 0;
}
