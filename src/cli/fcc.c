/* fcc - Flash C command-line compiler */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fc.h"

static const char *usage =
"Flash C " FC_VERSION " command-line compiler\n"
"Syntax is: fcc [ options ] file[s]     * = default; -x- = turn switch x off\n"
"  -C      Allow nested comments     -Dxxx   Define macro (-Dname[=value])\n"
"  -Ixxx   Include files directory   -K      Default char is unsigned\n"
"  -O      Optimize                  -S      (reserved)\n"
"  -exxx   Executable file name      -w      Display all warnings\n"
"  -w-     Suppress all warnings     -jN     Stop after N errors\n"
"  -gN     Stop after N warnings     -iN     Max. identifier length\n"
"  -wxxx   Warning control (-w-use, -w-pro, -w-ret, -w-par, -w-rvl)\n"
"  -h      This help\n";

static const char *progname = "fcc";
static int showwarn = 1;

static void diag(void *u, int sev, const char *file, int line, const char *msg)
{
    FcContext *c = u;
    if (sev == FC_WARNING) {
        if (!showwarn) return;
        fprintf(stderr, "Warning %s %d: %s\n", file, line, msg);
        if (c->opt.max_warnings && c->warnings >= c->opt.max_warnings) { fprintf(stderr, "Fatal %s %d: Too many error or warning messages\n", file, line); exit(1); }
    } else if (sev == FC_ERROR) fprintf(stderr, "Error %s %d: %s\n", file, line, msg);
    else if (sev == FC_FATAL) fprintf(stderr, "Fatal %s %d: %s\n", file, line, msg);
    else fprintf(stderr, "Linker Error: %s\n", msg);
}

int main(int argc, char **argv)
{
    FcContext ctx; memset(&ctx, 0, sizeof ctx);
    fc_options_default(&ctx.opt);
    ctx.diag = diag; ctx.user = &ctx;
    const char *files[64]; int nf = 0;
    const char *out = NULL;
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-') { if (nf < 64) files[nf++] = a; continue; }
        switch (a[1]) {
        case 'C': ctx.opt.nested_comments = a[2] != '-'; break;
        case 'D': if (ctx.opt.ndefine < 16) ctx.opt.defines[ctx.opt.ndefine++] = a + 2; break;
        case 'I': if (ctx.opt.nincdir < 16) ctx.opt.incdirs[ctx.opt.nincdir++] = a + 2; break;
        case 'K': ctx.opt.unsigned_char = a[2] != '-'; break;
        case 'O': ctx.opt.optimize = a[2] != '-'; break;
        case 'e': out = a + 2; break;
        case 'j': ctx.opt.max_errors = atoi(a + 2); break;
        case 'g': ctx.opt.max_warnings = atoi(a + 2); break;
        case 'i': ctx.opt.ident_len = atoi(a + 2); break;
        case 'w':
            if (!a[2]) { ctx.opt.warn_level = 2; showwarn = 1; }
            else if (!strcmp(a + 2, "-")) { ctx.opt.warn_level = 0; showwarn = 0; }
            else if (!strcmp(a + 2, "-use")) ctx.opt.w_unused = 0;
            else if (!strcmp(a + 2, "use")) ctx.opt.w_unused = 1;
            else if (!strcmp(a + 2, "-pro")) ctx.opt.w_noproto = 0;
            else if (!strcmp(a + 2, "-ret") || !strcmp(a + 2, "-rvl")) ctx.opt.w_noreturn = 0;
            else if (!strcmp(a + 2, "-par") || !strcmp(a + 2, "-rpt") || !strcmp(a + 2, "-sus")) ctx.opt.w_pointer = 0;
            break;
        case 'h': case '?': fputs(usage, stdout); return 0;
        default: break;   /* accepted for compatibility, no effect */
        }
    }
    if (!nf) { fputs(usage, stdout); return 1; }
    char outbuf[1024];
    if (!out) {
        snprintf(outbuf, sizeof outbuf, "%s", files[0]);
        char *dot = strrchr(outbuf, '.'), *sl = strrchr(outbuf, '/');
        if (dot && (!sl || dot > sl)) *dot = 0; else strcat(outbuf, ".out");
        out = outbuf;
    }
    printf("Flash C Version %s\n", FC_VERSION);
    for (int i = 0; i < nf; i++) printf("%s:\n", files[i]);
    fflush(stdout);
    int rc = fc_compile(&ctx, files, nf, out);
    printf("\n\t\t%d lines, %d warnings, %d errors\n", ctx.lines, ctx.warnings, ctx.errors);
    if (rc) { printf("*** %d errors in Compile ***\n", ctx.errors); return 1; }
    (void)progname;
    return 0;
}
