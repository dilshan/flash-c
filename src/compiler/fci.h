/* Flash C - compiler internals */
#ifndef FCI_H
#define FCI_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdarg.h>
#include "fc.h"

/* ---------- memory ---------- */
void *xalloc(size_t n);               /* zeroed, arena owned */
char *xstrdup(const char *s);
char *xstrndup(const char *s, int n);
char *intern(const char *s, int n);
void arena_reset(void);

/* ---------- diagnostics ---------- */
extern FcContext *CX;
void fc_error(int fileid, int line, const char *fmt, ...);   /* longjmps to recovery */
void fc_error_nj(int fileid, int line, const char *fmt, ...); /* no longjmp */
void fc_warn(int fileid, int line, const char *fmt, ...);
int  file_register(const char *name);
const char *file_name(int id);

/* ---------- tokens ---------- */
enum {
    TK_EOF = 0, TK_ID, TK_NUM, TK_CHR, TK_STR, TK_PP,
    /* punctuators */
    P_LPAR, P_RPAR, P_LBRK, P_RBRK, P_LBRC, P_RBRC, P_SEMI, P_COMMA, P_DOT, P_ARROW, P_ELLIPSIS,
    P_INC, P_DEC, P_AMP, P_STAR, P_PLUS, P_MINUS, P_TILDE, P_BANG, P_SLASH, P_PERCENT,
    P_SHL, P_SHR, P_LT, P_GT, P_LE, P_GE, P_EQ, P_NE, P_CARET, P_BAR, P_ANDAND, P_OROR,
    P_QUEST, P_COLON, P_ASSIGN, P_MULA, P_DIVA, P_MODA, P_ADDA, P_SUBA, P_SHLA, P_SHRA,
    P_ANDA, P_XORA, P_ORA, P_HASH, P_HASHHASH,
    /* keywords */
    K_AUTO, K_BREAK, K_CASE, K_CHAR, K_CONST, K_CONTINUE, K_DEFAULT, K_DO, K_DOUBLE, K_ELSE,
    K_ENUM, K_EXTERN, K_FLOAT, K_FOR, K_GOTO, K_IF, K_INT, K_LONG, K_REGISTER, K_RETURN,
    K_SHORT, K_SIGNED, K_SIZEOF, K_STATIC, K_STRUCT, K_SWITCH, K_TYPEDEF, K_UNION, K_UNSIGNED,
    K_VOID, K_VOLATILE, K_WHILE, K_IGNORE /* near far huge cdecl pascal */,
    K_COUNT
};

typedef struct Tok {
    int k;
    int line, fileid;
    char *s;          /* identifier (interned) or string bytes */
    int slen;
    long long val;
    double dval;      /* floating literal */
    int uns, lng, fsuf; /* literal suffix flags */
    unsigned bol:1, spc:1, noexp:1, isfloat:1;
} Tok;

typedef struct TokVec { Tok *v; int n, cap; } TokVec;
void tv_push(TokVec *t, Tok x);

/* lexer / preprocessor */
void pp_init(void);
void pp_define_cmdline(const char *name, const char *val);
int  pp_run(const char *path, TokVec *out);   /* returns 0 ok */
int  pp_run_text(const char *name, const char *text, TokVec *out);
int  lex_buffer(const char *buf, int len, int fileid, TokVec *out);
const char *tok_spell(Tok *t);

/* ---------- types ---------- */
typedef struct Type Type;
typedef struct Member { char *name; Type *ty; int off; struct Member *next; } Member;
typedef struct Param { char *name; Type *ty; struct Param *next; } Param;
enum { TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_LONG, TY_FLOAT, TY_DOUBLE, TY_PTR, TY_ARRAY, TY_STRUCT, TY_UNION, TY_FUNC };
struct Type {
    int kind, size, align, uns;
    Type *base;           /* ptr, array element */
    int len;              /* array length, -1 = unknown */
    Member *members; int complete; char *tag;
    Type *ret; Param *params; int nparams, variadic, noproto;
};
extern Type *ty_void, *ty_char, *ty_uchar, *ty_short, *ty_ushort, *ty_int, *ty_uint, *ty_long, *ty_ulong, *ty_float, *ty_double;
Type *ptr_to(Type *b);
Type *array_of(Type *b, int len);
int is_int(Type *t); int is_ptr(Type *t); int is_flt(Type *t); int is_scalar(Type *t); int is_agg(Type *t);
int same_type(Type *a, Type *b);
const char *type_str(Type *t);

/* ---------- symbols & AST ---------- */
enum { SY_VAR, SY_FUNC, SY_TYPEDEF, SY_ENUM };
enum { SC_NONE, SC_STATIC, SC_EXTERN };
typedef struct Node Node;
typedef struct Sym {
    char *name; Type *ty; int kind;
    int global;           /* 1 = static storage */
    int off;              /* local: rbp offset; global: section offset */
    int bss;              /* global in bss section */
    int storage;          /* SC_* */
    int defined;
    long long val;        /* enum constant */
    int used, assigned, is_param, from_rt, line, fileid;
    int is_builtin;       /* __syscall */
    int hidden;           /* runtime symbol not yet declared by user code */
    Node *body; int frame; /* function */
    struct Sym **refs; int nrefs, caprefs;
    int reach;            /* reachable from main */
    int codepos;          /* start in code buffer, -1 if not generated */
    struct Sym *next, *hnext;
} Sym;

enum {
    N_NUM, N_STR, N_VAR, N_ADDR, N_DEREF, N_MEMBER, N_ASSIGN, N_ASSIGNOP, N_COND, N_COMMA,
    N_LOGAND, N_LOGOR, N_NOT, N_NEG, N_BITNOT, N_BIN, N_CALL, N_CAST,
    N_PREINC, N_POSTINC, N_PREDEC, N_POSTDEC, N_FUNCADDR, N_FNUM,
    /* statements */
    S_EXPR, S_IF, S_WHILE, S_DO, S_FOR, S_RETURN, S_BREAK, S_CONTINUE, S_BLOCK, S_SWITCH,
    S_CASE, S_DEFAULT, S_GOTO, S_LABEL, S_NOP, S_ZERO
};
enum { B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_AND, B_OR, B_XOR, B_SHL, B_SHR,
       B_EQ, B_NE, B_LT, B_LE, B_GT, B_GE };

struct Node {
    int k, op; Type *ty;
    Node *l, *r, *c, *d;   /* operands; for stmts: cond/then/else/body */
    Node *next;            /* statement list / argument list */
    long long val; double fval;
    Sym *sym; char *str; int slen;
    int line, cmpty_uns;
    Type *cty;             /* computation type for assignop */
    Node *cases;           /* switch: list of S_CASE/S_DEFAULT */
    int label;             /* codegen label id */
};

/* parser */
typedef struct Program {
    Sym **funcs; int nfuncs, capfuncs;
    Sym *globals;
    uint8_t *data; int ndata, capdata;
    int bss;
    struct DReloc { int off; int kind; Sym *fn; int sec; long long add; } *drel; int ndrel, capdrel;
} Program;
extern Program PG;
int  parse_unit(TokVec *toks, int is_runtime);
void parse_end_runtime(void);
void gen_reset(void);
void files_reset(void);
void pp_snapshot(void);
void pp_restore(void);
const char *fc_runtime_source(void);
void parse_init(void);
Sym *global_lookup(const char *name);
Node *parse_expr_only(TokVec *tv);

/* codegen */
int gen_program(const char *outpath, char *err, int errlen);
#endif
