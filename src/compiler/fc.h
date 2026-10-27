/*
 * File:    fc.h
 * Purpose: Flash C - public compiler interface
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef FC_H
#define FC_H

#include <stdint.h>

#define FC_VERSION "1.0"

enum { FC_WARNING = 0, FC_ERROR = 1, FC_FATAL = 2 };

/* Optimization passes of the syntax-tree optimizer (see opt.c); each can be switched on its own. */
#define FCO_FOLD (1ULL << 0) /* constant folding */
#define FCO_ALGEBRA (1ULL << 1) /* algebraic simplification */
#define FCO_COMPARE (1ULL << 2) /* constant comparison evaluation */
#define FCO_CONSTPROP (1ULL << 3) /* local constant propagation */
#define FCO_COPYPROP (1ULL << 4) /* local copy propagation */
#define FCO_DEADSTORE (1ULL << 5) /* dead assignment elimination */
#define FCO_UNREACH (1ULL << 6) /* unreachable statement elimination */
#define FCO_CASTS (1ULL << 7) /* redundant cast elimination */
#define FCO_CANON (1ULL << 8) /* expression canonicalization */
#define FCO_CSE (1ULL << 9) /* local common subexpression elimination */
#define FCO_STRENGTH (1ULL << 10) /* strength reduction */
#define FCO_BOOL (1ULL << 11) /* boolean and conditional simplification */
#define FCO_CFG (1ULL << 12) /* basic-block construction (needed by the passes below) */
#define FCO_GCONST (1ULL << 13) /* global constant propagation */
#define FCO_CCP (1ULL << 14) /* conditional constant propagation */
#define FCO_CFS (1ULL << 15) /* control-flow simplification */
#define FCO_UBLOCK (1ULL << 16) /* unreachable basic-block elimination */
#define FCO_GDCE (1ULL << 17) /* global dead code elimination */
#define FCO_GCOPY (1ULL << 18) /* global copy propagation */
#define FCO_MERGE (1ULL << 19) /* basic-block merging */
#define FCO_THREAD (1ULL << 20) /* jump threading */
#define FCO_LIVE (1ULL << 21) /* live-variable analysis */
#define FCO_BRANCH (1ULL << 22) /* branch simplification */
#define FCO_TAILREC (1ULL << 23) /* tail-recursion elimination */
#define FCO_RD (1ULL << 24) /* reaching definitions analysis */
#define FCO_AVAIL (1ULL << 25) /* available expressions analysis */
#define FCO_GCSE (1ULL << 26) /* global common subexpression elimination */
#define FCO_PRE (1ULL << 27) /* partial redundancy elimination */
#define FCO_INLINE (1ULL << 28) /* function inlining */
#define FCO_TAILCALL (1ULL << 29) /* tail-call optimization */
#define FCO_LOOPS (1ULL << 30) /* loop detection */
#define FCO_LICM (1ULL << 31) /* loop-invariant code motion */
#define FCO_IV (1ULL << 32) /* induction-variable optimization */
#define FCO_REGALLOC (1ULL << 33) /* register allocation */
#define FCO_ISEL (1ULL << 34) /* instruction selection */
#define FCO_LAYOUT (1ULL << 35) /* branch folding and block layout */
#define FCO_SSA (1ULL << 36)      /* static single assignment form (dominator tree, phi nodes) */
#define FCO_SCCP (1ULL << 37)     /* sparse conditional constant propagation on the SSA form */
#define FCO_DOMTREE (1ULL << 38)  /* dominator tree and dominance frontiers */
#define FCO_ADCE (1ULL << 39)     /* aggressive dead-code elimination on the SSA form */
#define FCO_ALIAS (1ULL << 40)    /* alias analysis: loads of memory take part in global CSE */
#define FCO_IPCP (1ULL << 41)     /* interprocedural constant propagation */
#define FCO_IPDCE (1ULL << 42)    /* interprocedural dead-code elimination */
#define FCO_UNROLL (1ULL << 43)   /* loop unrolling */
#define FCO_UNSWITCH (1ULL << 44) /* loop unswitching */
#define FCO_RECUR (1ULL << 45)    /* recursive function analysis */
#define FCO_SROA (1ULL << 46)     /* scalarization of structures and arrays */
#define FCO_SIZE (1ULL << 47)     /* optimize for code size (not part of "all") */
#define FCO_ALL ((1ULL << 47) - 1)

/* All settings of one compilation; fc_options_default() gives the defaults. */
typedef struct FcOptions {
    int nested_comments; /* allow nested comments                    */
    int warn_level;      /* 0 = none, 1 = selected, 2 = all          */
    int max_errors;      /* stop after N errors   (default 25)       */
    int max_warnings;    /* stop after N warnings (default 100)      */
    int unsigned_char;   /* plain char is unsigned                   */
    int ident_len;       /* significant identifier length (0 = all)  */
    int w_unused;        /* warn about unused variables              */
    int w_noproto;       /* warn about calls without prototype       */
    int w_noreturn;      /* warn when a function returns no value    */
    int w_pointer;       /* non-portable pointer conversion          */
    int w_susp;          /* suspicious pointer conversion            */
    int w_noeffect;      /* code has no effect                       */
    int w_assign;        /* possibly incorrect assignment            */
    int w_divzero;       /* division by zero                         */
    int w_voidret;       /* void function returns a value            */
    int nolibs;          /* do not search the default libraries (-ln) */
    int fp_mode;         /* 0 = no floating point (-f-), 1 = default, 2 = hardware (-f87) */
    int map;             /* link map: 0 off, 1 segments, 2 publics, 3 detailed (-M) */
    int nomath;          /* do not link the math library (-lnm) */
    int wildargs;        /* link the wildcard argument expander (wildargs.obj) */
    int debug;           /* record source-level debug information (-v) */
    int enum_int;        /* enums are always int (-b, default); off: smallest type that fits */
    int merge_strings;   /* identical string literals share one copy (-d) */
    int align_struct;    /* pad members to their natural alignment (-a, default) */
    int stack_check;     /* test for stack overflow at function entry (-N) */
    int ansi_keywords;   /* ANSI only: no // comments; near/far/huge/cdecl/pascal are names (-A) */
    int jump_opt;        /* remove jumps to the next instruction (-O) */
    int redundant_loads; /* peephole: remove redundant push/pop pairs (-Z) */
    int stack_kb;        /* stack size in KB applied at start-up, 0 = system default (-ls) */
    int elf_symtab;      /* write section headers with .symtab/.strtab (-lt) */
    int wx;              /* separate R+X code and R+W data segments (-lw) */
    uint64_t opt_flags;     /* FCO_* optimization passes (-O:name) */
    int pgo_mode;           /* profile-guided optimization: 0 off, 1 instrument (-Pg), 2 use (-Pu) */
    char pgo_file[260];     /* the profile data file */
    int w_unreach;       /* unreachable code                         */
    int w_amb;           /* ambiguous operators need parentheses     */
    int w_structval;     /* structure passed by value                */
    int w_cond;          /* condition is always true/false           */
    int w_ucp;           /* mixing pointers to signed and unsigned char */
    int w_cpt;           /* non-portable pointer comparison          */
    const char *undefs[16]; /* macros to undefine (-U)                  */
    int nundef;
    const char *incdirs[16]; /* include search path                      */
    int nincdir;
    const char *defines[16]; /* NAME or NAME=VALUE */
    int ndefine;
    const char *nodebug_files[16]; /* sources without debug information (IDE: Local Options) */
    int nnodebug;
} FcOptions;

typedef void (*FcDiagFn)(void *user, int severity, const char *file, int line, const char *msg);
typedef void (*FcProgressFn)(void *user, const char *file, int lines);

/* One compilation: options, the diagnostic callbacks and the counters filled in by the compiler. */
typedef struct FcContext {
    FcOptions opt;
    FcDiagFn diag;
    FcProgressFn progress;
    void *user;
    int errors, warnings, lines;
} FcContext;

void fc_options_default(FcOptions *o);
/* Applies one command-line switch such as "-O" or "-DNAME=1" to the options. Returns 1 if the text
   is a switch the compiler knows (the string must stay valid until the compile is over), 0
   otherwise. */
int fc_apply_switch(FcOptions *o, const char *arg);
/* Compile one or more C files (as one program) into an executable. Returns 0 on success. */
int fc_compile(FcContext *c, const char **files, int nfiles, const char *outpath);
/* Evaluate an integer constant expression (used by the IDE evaluator). Returns 0 on success. */
int fc_eval_expr(const char *text, long long *result, char *err, int errlen);
const char *fc_builtin_header(const char *name);
/* enumerate the built-in headers (used by the online help); text may be NULL */
int fc_builtin_header_count(void);
const char *fc_builtin_header_at(int i, const char **text);
/* names of the run-time library modules that were linked by the last successful compile */
int fc_link_module_count(void);
const char *fc_link_module_at(int i, const char **lib, long *bytes);

/* ---- source-level debug information of the last successful compile (valid until the next compile)
 * ---- */
enum {
    FCT_VOID,
    FCT_CHAR,
    FCT_SHORT,
    FCT_INT,
    FCT_LONG,
    FCT_FLOAT,
    FCT_DOUBLE,
    FCT_PTR,
    FCT_ARRAY,
    FCT_STRUCT,
    FCT_UNION,
    FCT_FUNC
};
typedef struct FcDType FcDType;

typedef struct FcDMember {
    const char *name;
    FcDType *ty;
    int off;
    int bw, bsh, buns;
} FcDMember; /* bw > 0: bit field */

struct FcDType {
    int kind, size, uns, len;
    FcDType *base;
    const char *tag;
    FcDMember *mem;
    int nmem;
};

typedef struct FcDVar {
    const char *name;
    FcDType *ty;
    int off;
    int is_param;
    int line0, line1;
} FcDVar; /* off: rbp offset */

typedef struct FcDFunc {
    const char *name;
    const char *file;
    unsigned long addr, end;
    int line0, line1;
    FcDType *ret;
    FcDVar *vars;
    int nvars;
    int nparams;
} FcDFunc;

typedef struct FcDLine {
    unsigned long addr;
    int line;
    int file;
} FcDLine; /* sorted by address */

typedef struct FcDGlobal {
    const char *name;
    FcDType *ty;
    unsigned long addr;
    const char *file;
} FcDGlobal;

typedef struct FcDebug {
    FcDFunc *funcs;
    int nfuncs;
    FcDLine *lines;
    int nlines;
    FcDGlobal *globals;
    int nglobals;
    const char **files;
    int nfiles;
    unsigned long entry;
} FcDebug;

const FcDebug *fc_debug_info(void); /* NULL when the last compile recorded none */

#endif
