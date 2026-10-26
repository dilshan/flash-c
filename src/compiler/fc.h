/* Flash C - public compiler interface */
#ifndef FC_H
#define FC_H

#define FC_VERSION "1.0"

enum { FC_WARNING = 0, FC_ERROR = 1, FC_FATAL = 2 };

typedef struct FcOptions {
    int nested_comments;     /* allow nested comments                    */
    int warn_level;          /* 0 = none, 1 = selected, 2 = all          */
    int max_errors;          /* stop after N errors   (default 25)       */
    int max_warnings;        /* stop after N warnings (default 100)      */
    int optimize;            /* fold constants / jump optimisation       */
    int unsigned_char;       /* plain char is unsigned                   */
    int ident_len;           /* significant identifier length (0 = all)  */
    int w_unused;            /* warn about unused variables              */
    int w_noproto;           /* warn about calls without prototype       */
    int w_noreturn;          /* warn when a function returns no value    */
    int w_pointer;           /* pointer conversion warnings              */
    int w_noeffect;          /* code has no effect                       */
    int w_assign;            /* possibly incorrect assignment            */
    int w_divzero;           /* division by zero                         */
    int w_voidret;           /* void function returns a value            */
    int nolibs;              /* do not search the default libraries (-ln) */
    int fp_mode;             /* 0 = no floating point (-f-), 1 = default, 2 = hardware (-f87) */
    int map;                 /* link map: 0 off, 1 segments, 2 publics, 3 detailed (-M) */
    int nomath;              /* do not link the math library (-lnm) */
    int wildargs;            /* link the wildcard argument expander (wildargs.obj) */
    int warn_dup;            /* warn when the program redefines a library function */
    int stack_warn;          /* warn when the program has no explicit stack size */
    int debug;               /* record source-level debug information (-v) */
    const char *incdirs[16]; /* include search path                      */
    int nincdir;
    const char *defines[16]; /* NAME or NAME=VALUE */
    int ndefine;
} FcOptions;

typedef void (*FcDiagFn)(void *user, int severity, const char *file, int line, const char *msg);
typedef void (*FcProgressFn)(void *user, const char *file, int lines);

typedef struct FcContext {
    FcOptions opt;
    FcDiagFn diag;
    FcProgressFn progress;
    void *user;
    int errors, warnings, lines;
} FcContext;

void fc_options_default(FcOptions *o);
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


/* ---- source-level debug information of the last successful compile (valid until the next compile) ---- */
enum { FCT_VOID, FCT_CHAR, FCT_SHORT, FCT_INT, FCT_LONG, FCT_FLOAT, FCT_DOUBLE, FCT_PTR, FCT_ARRAY, FCT_STRUCT, FCT_UNION, FCT_FUNC };
typedef struct FcDType FcDType;
typedef struct FcDMember { const char *name; FcDType *ty; int off; } FcDMember;
struct FcDType { int kind, size, uns, len; FcDType *base; const char *tag; FcDMember *mem; int nmem; };
typedef struct FcDVar { const char *name; FcDType *ty; int off; int is_param; int line0, line1; } FcDVar;  /* off: rbp offset */
typedef struct FcDFunc {
    const char *name; const char *file; unsigned long addr, end; int line0, line1;
    FcDType *ret; FcDVar *vars; int nvars; int nparams;
} FcDFunc;
typedef struct FcDLine { unsigned long addr; int line; int file; } FcDLine;      /* sorted by address */
typedef struct FcDGlobal { const char *name; FcDType *ty; unsigned long addr; const char *file; } FcDGlobal;
typedef struct FcDebug {
    FcDFunc *funcs; int nfuncs;
    FcDLine *lines; int nlines;
    FcDGlobal *globals; int nglobals;
    const char **files; int nfiles;
    unsigned long entry;
} FcDebug;
const FcDebug *fc_debug_info(void);      /* NULL when the last compile recorded none */

#endif
