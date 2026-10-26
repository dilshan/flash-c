/* Flash C - compiler driver */
#include "fci.h"

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
    o->w_noeffect = o->w_assign = o->w_divzero = o->w_voidret = 1;
}

static const char *lib_dir(char kind)
{
    return kind == '0' ? "c0" : kind == '1' ? "libc" : kind == '2' ? "libm" : "obj";
}

static void rt_diag(void *u, int sev, const char *file, int line, const char *msg)
{
    FcContext *c = u;
    if (sev == FC_WARNING) return;
    fprintf(stderr, "flashc: internal runtime error %s %d: %s\n", file, line, msg);
    (void)c;
}

int fc_compile(FcContext *c, const char **files, int nfiles, const char *outpath)
{
    CX = c;
    c->errors = c->warnings = c->lines = 0;
    arena_reset(); files_reset(); gen_reset();
    parse_init(); pp_init();

    /* run-time library: the start-up module and the selected libraries are parsed first and stay hidden
     * from the program until it declares them (the "link" step then keeps only what is reachable) */
    {
        pp_snapshot();
        FcDiagFn sd = c->diag; void *su = c->user; FcProgressFn sp = c->progress;
        c->diag = rt_diag; c->user = c; c->progress = NULL;
        FcOptions so = c->opt;
        c->opt.warn_level = 0; c->opt.max_errors = 0;
        if (so.fp_mode == 0) pp_define_cmdline("_FC_NOFP", "1");
        TokVec rt = {0};
        for (const FcLibModule *m = fc_lib_modules; m->name; m++) {
            int want = m->kind == '0' || (m->kind == '1' && !so.nolibs) ||
                       (m->kind == '2' && !so.nolibs && !so.nomath && so.fp_mode != 0) || (m->kind == '3' && so.wildargs);
            if (!want) continue;
            char nm[96]; snprintf(nm, sizeof nm, "%s/%s.c", lib_dir(m->kind), m->name);
            pp_run_text(nm, m->text, &rt);
        }
        parse_unit(&rt, 1);
        free(rt.v);
        c->opt = so;
        c->diag = sd; c->user = su; c->progress = sp;
        if (c->errors) { c->errors = 1; return 1; }
        c->errors = c->warnings = c->lines = 0;
        parse_end_runtime();
        pp_restore();
    }
    for (int i = 0; i < c->opt.ndefine; i++) {
        char nm[128]; const char *eq = strchr(c->opt.defines[i], '=');
        if (eq) { int n = (int)(eq - c->opt.defines[i]); if (n > 127) n = 127; memcpy(nm, c->opt.defines[i], n); nm[n] = 0; pp_define_cmdline(nm, eq + 1); }
        else pp_define_cmdline(c->opt.defines[i], "1");
    }
    for (int i = 0; i < nfiles; i++) {
        TokVec tv = {0};
        if (pp_run(files[i], &tv)) { free(tv.v); continue; }
        parse_unit(&tv, 0);
        free(tv.v);
    }
    if (c->errors) return 1;
    char err[256] = "";
    if (gen_program(outpath, err, sizeof err, c->opt.map)) {
        if (err[0]) { c->errors++; if (c->diag) c->diag(c->user, FC_FATAL + 1, "", 0, err); }
        return 1;
    }
    return 0;
}

typedef struct { char msg[256]; int got; } EvalCtx;
static void eval_diag(void *u, int sev, const char *file, int line, const char *msg)
{
    EvalCtx *e = u;
    if (sev != FC_WARNING && !e->got) { snprintf(e->msg, sizeof e->msg, "%s", msg); e->got = 1; }
}

int fc_eval_expr(const char *text, long long *result, char *err, int errlen)
{
    FcContext ctx; EvalCtx ec; memset(&ec, 0, sizeof ec);
    memset(&ctx, 0, sizeof ctx);
    fc_options_default(&ctx.opt);
    ctx.diag = eval_diag; ctx.user = &ec;
    CX = &ctx;
    arena_reset(); files_reset();
    parse_init(); pp_init();
    TokVec tv = {0};
    pp_run_text("<expression>", text, &tv);
    Node *n = NULL;
    if (!ec.got) n = parse_expr_only(&tv);
    free(tv.v);
    int rc = 1;
    if (!ec.got && n) {
        if (n->k == N_NUM) { *result = n->val; rc = 0; }
        else snprintf(ec.msg, sizeof ec.msg, "Constant expression required");
    } else if (!ec.got) snprintf(ec.msg, sizeof ec.msg, "Expression syntax");
    if (rc && err) snprintf(err, errlen, "%s", ec.msg);
    arena_reset();
    return rc;
}
