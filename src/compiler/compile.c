/*
 * File:    compile.c
 * Purpose: Flash C - compiler driver
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "fci.h"

/* Sets every compiler option to its default. */
void fc_options_default(FcOptions *o)
{
    memset(o, 0, sizeof *o);
    o->warn_level = 1;
    o->fp_mode = 1;
    o->max_errors = 25;
    o->max_warnings = 100;
    o->w_unused = 1;
    o->w_noproto = 1;
    o->w_noreturn = 1;
    o->w_pointer = 1;
    o->w_susp = 1;
    o->w_noeffect = o->w_assign = o->w_divzero = o->w_voidret = 1;
    o->enum_int = 1;
    o->align_struct = 1;
    o->w_unreach = o->w_cond = o->w_ucp = o->w_cpt = 1;
}

/* Names of the optimizations for -O:name (see FCO_* in fc.h). */
static const struct {
    const char *name;
    unsigned bit;
} opt_names[] = {
    {"fold", FCO_FOLD},         {"algebra", FCO_ALGEBRA},     {"compare", FCO_COMPARE},
    {"constprop", FCO_CONSTPROP}, {"copyprop", FCO_COPYPROP}, {"deadstore", FCO_DEADSTORE},
    {"unreachable", FCO_UNREACH}, {"casts", FCO_CASTS},       {"canon", FCO_CANON},
    {"cse", FCO_CSE},           {"strength", FCO_STRENGTH},   {"bool", FCO_BOOL},
};

/* Applies the list of an -O:name,name option: a name switches an optimization on, -name off.  The
 * names jumps and peephole stand for the -O and -Z code generator optimizations, all and none for
 * everything.  Returns 0 if a name is unknown. */
static int fc_apply_opt_list(FcOptions *o, const char *list)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", list);
    for (char *t = strtok(buf, ",;"); t; t = strtok(NULL, ",;")) {
        int on = t[0] != '-';
        const char *name = t + !on;
        unsigned bit = 0;
        if (!strcmp(name, "all") || !strcmp(name, "none")) {
            on = !strcmp(name, "all") ? on : !on;
            o->opt_flags = on ? FCO_ALL : 0;
            o->jump_opt = o->redundant_loads = on;
            continue;
        }
        if (!strcmp(name, "jumps")) {
            o->jump_opt = on;
            continue;
        }
        if (!strcmp(name, "peephole")) {
            o->redundant_loads = on;
            continue;
        }
        for (unsigned i = 0; i < sizeof opt_names / sizeof opt_names[0]; i++) {
            if (!strcmp(name, opt_names[i].name)) {
                bit = opt_names[i].bit;
            }
        }
        if (!bit) {
            return 0;
        }
        o->opt_flags = on ? o->opt_flags | bit : o->opt_flags & ~bit;
    }
    return 1;
}

/* Applies one command-line switch (the same switches fcc takes) to the options. */
int fc_apply_switch(FcOptions *o, const char *arg)
{
    if (arg[0] != '-') {
        return 0;
    }
    switch (arg[1]) {
    case 'A':
        o->ansi_keywords = arg[2] != '-';
        break;
    case 'C':
        o->nested_comments = arg[2] != '-';
        break;
    case 'U':
        if (o->nundef < 16) {
            o->undefs[o->nundef++] = arg + 2;
        }
        break;
    case 'b':
        o->enum_int = arg[2] != '-';
        break;
    case 'd':
        o->merge_strings = arg[2] != '-';
        break;
    case 'a':
        o->align_struct = arg[2] != '-';
        break;
    case 'N':
        o->stack_check = arg[2] != '-';
        break;
    case 'O':
        if (arg[2] == ':') { /* -O:name,name,-name  switches single optimizations */
            return fc_apply_opt_list(o, arg + 3);
        }
        o->jump_opt = arg[2] != '-';
        o->opt_flags = arg[2] != '-' ? FCO_ALL : 0;
        break;
    case 'Z':
        o->redundant_loads = arg[2] != '-';
        break;
    case 'D':
        if (o->ndefine < 16) {
            o->defines[o->ndefine++] = arg + 2;
        }
        break;
    case 'I':
        if (o->nincdir < 16) {
            o->incdirs[o->nincdir++] = arg + 2;
        }
        break;
    case 'K':
        o->unsigned_char = arg[2] != '-';
        break;
    case 'f':
        o->fp_mode = !strcmp(arg + 2, "-") ? 0 : !strcmp(arg + 2, "87") ? 2 : 1;
        break;
    case 'v':
        o->debug = 1;
        break;
    case 'M':
        o->map = arg[2] == 'p' ? 2 : arg[2] == 'd' ? 3 : arg[2] == '-' ? 0 : 1;
        break;
    case 'l':
        if (!strcmp(arg + 2, "n") || !strcmp(arg + 2, "-d")) {
            o->nolibs = 1;
        } else if (!strcmp(arg + 2, "nm")) {
            o->nomath = 1;
        } else if (arg[2] == 's') {
            o->stack_kb = atoi(arg + 3);
        } else if (arg[2] == 't') {
            o->elf_symtab = arg[3] != '-';
        } else if (arg[2] == 'w') {
            o->wx = arg[3] != '-';
        }
        break;
    case 'j':
        o->max_errors = atoi(arg + 2);
        break;
    case 'g':
        o->max_warnings = atoi(arg + 2);
        break;
    case 'i':
        o->ident_len = atoi(arg + 2);
        break;
    case 'w':
        if (!arg[2]) {
            o->warn_level = 2;
        } else if (!strcmp(arg + 2, "-")) {
            o->warn_level = 0;
        } else {
            /* -wxxx turns a warning on, -w-xxx turns it off */
            int on = arg[2] != '-';
            const char *w = arg + 2 + !on;
            if (!strcmp(w, "use") || !strcmp(w, "aus")) {
                o->w_unused = on;
            } else if (!strcmp(w, "pro") || !strcmp(w, "nod")) {
                o->w_noproto = on;
            } else if (!strcmp(w, "ret") || !strcmp(w, "rvl")) {
                o->w_noreturn = on;
            } else if (!strcmp(w, "par") || !strcmp(w, "rpt")) {
                o->w_pointer = on;
            } else if (!strcmp(w, "sus")) {
                o->w_susp = on;
            } else if (!strcmp(w, "ucp")) {
                o->w_ucp = on;
            } else if (!strcmp(w, "cpt")) {
                o->w_cpt = on;
            } else if (!strcmp(w, "eff")) {
                o->w_noeffect = on;
            } else if (!strcmp(w, "asn")) {
                o->w_assign = on;
            } else if (!strcmp(w, "zdi")) {
                o->w_divzero = on;
            } else if (!strcmp(w, "voi")) {
                o->w_voidret = on;
            } else if (!strcmp(w, "rch")) {
                o->w_unreach = on;
            } else if (!strcmp(w, "amb")) {
                o->w_amb = on;
            } else if (!strcmp(w, "stv")) {
                o->w_structval = on;
            } else if (!strcmp(w, "cln")) {
                o->w_cond = on;
            }
        }
        break;
    default:
        return 0; /* not a switch the compiler knows */
    }
    return 1;
}

/* Returns the name of the library directory for a run-time module kind (see FcLibModule). */
static const char *lib_dir(char kind)
{
    return kind == '0' ? "c0" : kind == '1' ? "libc" : kind == '2' ? "libm" : "obj";
}

/*
 * Diagnostic callback used while the run-time library is parsed: any error there is a bug in
 * the compiler package, so it is printed as an internal error.
 */
static void rt_diag(void *u, int sev, const char *file, int line, const char *msg)
{
    FcContext *c = u;
    if (sev == FC_WARNING) {
        return;
    }
    fprintf(stderr, "flashc: internal runtime error %s %d: %s\n", file, line, msg);
    (void)c;
}

/*
 * Compiles the given C files as one program into the executable outpath.  Returns 0 on
 * success; diagnostics go to c->diag.
 */
int fc_compile(FcContext *c, const char **files, int nfiles, const char *outpath)
{
    CX = c;
    c->errors = c->warnings = c->lines = 0;
    arena_reset();
    files_reset();
    gen_reset();
    parse_init();
    pp_init();

    /* run-time library: the start-up module and the selected libraries are parsed first and stay
     * hidden from the program until it declares them (the "link" step then keeps only what is
     * reachable) */
    {
        pp_snapshot();
        FcDiagFn sd = c->diag;
        void *su = c->user;
        FcProgressFn sp = c->progress;
        c->diag = rt_diag;
        c->user = c;
        c->progress = NULL;
        FcOptions so = c->opt;
        c->opt.warn_level = 0;
        c->opt.max_errors = 0;
        c->opt.ansi_keywords = 0;
        c->opt.stack_check = 0;
        c->opt.align_struct = 1;
        c->opt.enum_int = 1; /* the library is always compiled the same way */
        if (so.fp_mode == 0) {
            pp_define_cmdline("_FC_NOFP", "1");
        }
        if (so.stack_kb > 0) {
            char kb[16];
            snprintf(kb, sizeof kb, "%d", so.stack_kb);
            pp_define_cmdline("_FC_STKKB", kb);
        }
        TokVec rt = {0};
        for (const FcLibModule *m = fc_lib_modules; m->name; m++) {
            int want = m->kind == '0' || (m->kind == '1' && !so.nolibs) ||
                       (m->kind == '2' && !so.nolibs && !so.nomath && so.fp_mode != 0) ||
                       (m->kind == '3' && so.wildargs);
            if (!want) {
                continue;
            }
            char nm[96];
            snprintf(nm, sizeof nm, "%s/%s.c", lib_dir(m->kind), m->name);
            pp_run_text(nm, m->text, &rt);
        }
        parse_unit(&rt, 1);
        free(rt.v);
        c->opt = so;
        c->diag = sd;
        c->user = su;
        c->progress = sp;
        if (c->errors) {
            c->errors = 1;
            return 1;
        }
        c->errors = c->warnings = c->lines = 0;
        parse_end_runtime();
        pp_restore();
    }
    for (int i = 0; i < c->opt.ndefine; i++) {
        char nm[128];
        const char *eq = strchr(c->opt.defines[i], '=');
        if (eq) {
            int n = (int)(eq - c->opt.defines[i]);
            if (n > 127) {
                n = 127;
            }
            memcpy(nm, c->opt.defines[i], n);
            nm[n] = 0;
            pp_define_cmdline(nm, eq + 1);
        } else {
            pp_define_cmdline(c->opt.defines[i], "1");
        }
    }
    for (int i = 0; i < c->opt.nundef; i++) {
        pp_undef_cmdline(c->opt.undefs[i]);
    }
    for (int i = 0; i < nfiles; i++) {
        TokVec tv = {0};
        if (pp_run(files[i], &tv)) {
            free(tv.v);
            continue;
        }
        parse_unit(&tv, 0);
        free(tv.v);
    }
    if (c->errors) {
        return 1;
    }
    char err[256] = "";
    if (gen_program(outpath, err, sizeof err, c->opt.map)) {
        if (err[0]) {
            c->errors++;
            if (c->diag) {
                c->diag(c->user, FC_FATAL + 1, "", 0, err);
            }
        }
        return 1;
    }
    return 0;
}

typedef struct {
    char msg[256];
    int got;
} EvalCtx;

/* Diagnostic callback of fc_eval_expr(): remembers the first error message. */
static void eval_diag(void *u, int sev, const char *file, int line, const char *msg)
{
    EvalCtx *e = u;
    if (sev != FC_WARNING && !e->got) {
        snprintf(e->msg, sizeof e->msg, "%s", msg);
        e->got = 1;
    }
}

/*
 * Evaluates an integer constant expression such as "sizeof(int) * 4".  Returns 0 and stores
 * the value in *result, or 1 with a message in err.
 */
int fc_eval_expr(const char *text, long long *result, char *err, int errlen)
{
    FcContext ctx;
    EvalCtx ec;
    memset(&ec, 0, sizeof ec);
    memset(&ctx, 0, sizeof ctx);
    fc_options_default(&ctx.opt);
    ctx.diag = eval_diag;
    ctx.user = &ec;
    CX = &ctx;
    arena_reset();
    files_reset();
    parse_init();
    pp_init();
    TokVec tv = {0};
    pp_run_text("<expression>", text, &tv);
    Node *n = NULL;
    if (!ec.got) {
        n = parse_expr_only(&tv);
    }
    free(tv.v);
    int rc = 1;
    if (!ec.got && n) {
        if (n->kind == N_NUM) {
            *result = n->val;
            rc = 0;
        } else {
            snprintf(ec.msg, sizeof ec.msg, "Constant expression required");
        }
    } else if (!ec.got) {
        snprintf(ec.msg, sizeof ec.msg, "Expression syntax");
    }
    if (rc && err) {
        snprintf(err, errlen, "%s", ec.msg);
    }
    arena_reset();
    return rc;
}
