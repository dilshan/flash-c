/* fcc - Flash C command-line compiler */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fc.h"

static const char *usage =
"Flash C " FC_VERSION " command-line compiler\n"
"Syntax is: fcc [ options ] file[s]     * = default; -x- = turn switch x off\n"
"  -C      Allow nested comments     -Dxxx   Define macro (-Dname[=value])\n"
"  -Ixxx   Include files directory   -K      Default char is unsigned\n"
"  -exxx   Executable file name      -w      Display all warnings\n"
"  -w-     Suppress all warnings     -jN     Stop after N errors\n"
"  -gN     Stop after N warnings     -iN     Max. identifier length\n"
"  -wxxx   Warning control (-w-use, -w-pro, -w-ret, -w-par, -w-rvl, -w-sus, -w-ucp, -w-cpt,\n"
"          -w-rch, -w-cln; -wamb, -wstv turn on the off-by-default warnings)\n"
"  -f-     No floating-point support -f/-f87  Floating-point support (default)\n"
"  -A      ANSI keywords only        -Uxxx   Undefine macro\n"
"  -b    * Treat enums as integers   -b-     Smallest integer type that fits\n"
"  -d      Merge duplicate strings   -a    * Align structure members (-a- packs)\n"
"  -N      Check stack overflow      -O      Optimize jumps\n"
"  -Z      Suppress redundant loads  -ls<KB> Stack size in KB\n"
"  -lt     Write an ELF symbol table -lw     Separate R+X code and R+W data segments\n"
"  -M      Create link map file      -Mp/-Md Map with publics / detailed segments\n"
"  -v      Record source-level debug information (for the IDE debugger)\n"
"  -ln     Do not search the standard libraries\n"
"  -lnm    Do not link the math library\n"
"  wildargs      as a file name links the wildcard argument expander\n"
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
        if (a[0] != '-') {
            const char *b = strrchr(a, '/'); b = b ? b + 1 : a;
            if (!strcasecmp(b, "wildargs") || !strcasecmp(b, "wildargs.obj")) { ctx.opt.wildargs = 1; continue; }
            if (nf < 64) files[nf++] = a;
            continue;
        }
        switch (a[1]) {
        case 'A': ctx.opt.ansi_keywords = a[2] != '-'; break;
        case 'C': ctx.opt.nested_comments = a[2] != '-'; break;
        case 'U': if (ctx.opt.nundef < 16) ctx.opt.undefs[ctx.opt.nundef++] = a + 2; break;
        case 'b': ctx.opt.enum_int = a[2] != '-'; break;
        case 'd': ctx.opt.merge_strings = a[2] != '-'; break;
        case 'a': ctx.opt.align_struct = a[2] != '-'; break;
        case 'N': ctx.opt.stack_check = a[2] != '-'; break;
        case 'O': ctx.opt.jump_opt = a[2] != '-'; break;
        case 'Z': ctx.opt.redundant_loads = a[2] != '-'; break;
        case 'D': if (ctx.opt.ndefine < 16) ctx.opt.defines[ctx.opt.ndefine++] = a + 2; break;
        case 'I': if (ctx.opt.nincdir < 16) ctx.opt.incdirs[ctx.opt.nincdir++] = a + 2; break;
        case 'K': ctx.opt.unsigned_char = a[2] != '-'; break;
        case 'e': out = a + 2; break;
        case 'f': ctx.opt.fp_mode = !strcmp(a + 2, "-") ? 0 : !strcmp(a + 2, "87") ? 2 : 1; break;
        case 'v': ctx.opt.debug = 1; break;
        case 'M': ctx.opt.map = a[2] == 'p' ? 2 : a[2] == 'd' ? 3 : a[2] == '-' ? 0 : 1; break;
        case 'l':
            if (!strcmp(a + 2, "n") || !strcmp(a + 2, "-d")) ctx.opt.nolibs = 1;
            else if (!strcmp(a + 2, "nm")) ctx.opt.nomath = 1;
            else if (a[2] == 's') ctx.opt.stack_kb = atoi(a + 3);
            else if (a[2] == 't') ctx.opt.elf_symtab = a[3] != '-';
            else if (a[2] == 'w') ctx.opt.wx = a[3] != '-';
            break;
        case 'j': ctx.opt.max_errors = atoi(a + 2); break;
        case 'g': ctx.opt.max_warnings = atoi(a + 2); break;
        case 'i': ctx.opt.ident_len = atoi(a + 2); break;
        case 'w':
            if (!a[2]) { ctx.opt.warn_level = 2; showwarn = 1; }
            else if (!strcmp(a + 2, "-")) { ctx.opt.warn_level = 0; showwarn = 0; }
            else {
                /* -wxxx turns a warning on, -w-xxx turns it off */
                int on = a[2] != '-'; const char *w = a + 2 + !on;
                if (!strcmp(w, "use") || !strcmp(w, "aus")) ctx.opt.w_unused = on;
                else if (!strcmp(w, "pro") || !strcmp(w, "nod")) ctx.opt.w_noproto = on;
                else if (!strcmp(w, "ret") || !strcmp(w, "rvl")) ctx.opt.w_noreturn = on;
                else if (!strcmp(w, "par") || !strcmp(w, "rpt")) ctx.opt.w_pointer = on;
                else if (!strcmp(w, "sus")) ctx.opt.w_susp = on;
                else if (!strcmp(w, "ucp")) ctx.opt.w_ucp = on;
                else if (!strcmp(w, "cpt")) ctx.opt.w_cpt = on;
                else if (!strcmp(w, "eff")) ctx.opt.w_noeffect = on;
                else if (!strcmp(w, "asn")) ctx.opt.w_assign = on;
                else if (!strcmp(w, "zdi")) ctx.opt.w_divzero = on;
                else if (!strcmp(w, "voi")) ctx.opt.w_voidret = on;
                else if (!strcmp(w, "rch")) ctx.opt.w_unreach = on;
                else if (!strcmp(w, "amb")) ctx.opt.w_amb = on;
                else if (!strcmp(w, "stv")) ctx.opt.w_structval = on;
                else if (!strcmp(w, "cln")) ctx.opt.w_cond = on;
            }
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
