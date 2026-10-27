/*
 * File:    fcc.c
 * Purpose: fcc - Flash C command-line compiler
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
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

/* Prints one compiler diagnostic in the classic "Error file line: message" format. */
static void diag(void *u, int sev, const char *file, int line, const char *msg)
{
    FcContext *c = u;
    if (sev == FC_WARNING) {
        if (!showwarn) {
            return;
        }
        fprintf(stderr, "Warning %s %d: %s\n", file, line, msg);
        if (c->opt.max_warnings && c->warnings >= c->opt.max_warnings) {
            fprintf(stderr, "Fatal %s %d: Too many error or warning messages\n", file, line);
            exit(1);
        }
    } else if (sev == FC_ERROR) {
        fprintf(stderr, "Error %s %d: %s\n", file, line, msg);
    } else if (sev == FC_FATAL) {
        fprintf(stderr, "Fatal %s %d: %s\n", file, line, msg);
    } else {
        fprintf(stderr, "Linker Error: %s\n", msg);
    }
}

/* fcc command line: parses the switches and files, compiles, and sets the exit status. */
int main(int argc, char **argv)
{
    FcContext ctx;
    memset(&ctx, 0, sizeof ctx);
    fc_options_default(&ctx.opt);
    ctx.diag = diag;
    ctx.user = &ctx;
    const char *files[64];
    int nf = 0;
    const char *out = NULL;
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-') {
            const char *b = strrchr(a, '/');
            b = b ? b + 1 : a;
            if (!strcasecmp(b, "wildargs") || !strcasecmp(b, "wildargs.obj")) {
                ctx.opt.wildargs = 1;
                continue;
            }
            if (nf < 64) {
                files[nf++] = a;
            }
            continue;
        }
        switch (a[1]) {
        case 'e':
            out = a + 2;
            break;
        case 'h':
        case '?':
            fputs(usage, stdout);
            return 0;
        default:
            fc_apply_switch(&ctx.opt, a); /* the switch table is shared with the IDE (compile.c) */
            if (a[1] == 'w') {
                showwarn = ctx.opt.warn_level != 0;
            }
            break;
        }
    }
    if (!nf) {
        fputs(usage, stdout);
        return 1;
    }
    char outbuf[1024];
    if (!out) {
        snprintf(outbuf, sizeof outbuf, "%s", files[0]);
        char *dot = strrchr(outbuf, '.'), *sl = strrchr(outbuf, '/');
        if (dot && (!sl || dot > sl)) {
            *dot = 0;
        } else {
            strcat(outbuf, ".out");
        }
        out = outbuf;
    }
    printf("Flash C Version %s\n", FC_VERSION);
    for (int i = 0; i < nf; i++) {
        printf("%s:\n", files[i]);
    }
    fflush(stdout);
    int rc = fc_compile(&ctx, files, nf, out);
    printf("\n\t\t%d lines, %d warnings, %d errors\n", ctx.lines, ctx.warnings, ctx.errors);
    if (rc) {
        printf("*** %d errors in Compile ***\n", ctx.errors);
        return 1;
    }
    (void)progname;
    return 0;
}
