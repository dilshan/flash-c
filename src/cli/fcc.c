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
    "\n"
    "Usage: fcc [options] file...\n"
    "\n"
    "  A * marks a default.  A trailing - turns a switch off (-x-).\n"
    "\n"
    "Input\n"
    "  -Dname[=value]  Define a macro\n"
    "  -Uname          Undefine a macro\n"
    "  -Ipath          Directory to search for include files\n"
    "  -C              Allow nested comments\n"
    "  -A              ANSI keywords only\n"
    "  -K              Default char is unsigned\n"
    "  -iN             Maximum identifier length\n"
    "\n"
    "Output\n"
    "  -ename          Name of the executable\n"
    "  -M              Create a link map file\n"
    "  -Mp, -Md        Map with publics / with detailed segments\n"
    "  -v              Record source-level debug information (for the IDE debugger)\n"
    "  -lt             Write an ELF symbol table\n"
    "  -lw             Separate R+X code and R+W data segments\n"
    "\n"
    "Language and code generation\n"
    "  -b *            Treat enums as integers      (-b-  smallest type that fits)\n"
    "  -a *            Align structure members      (-a-  pack them)\n"
    "  -d              Merge duplicate strings\n"
    "  -N              Check for stack overflow\n"
    "  -ls<KB>         Stack size in KB\n"
    "  -f-             No floating-point support    (-f, -f87  floating point *)\n"
    "\n"
    "Optimization\n"
    "  -O              Optimize everything          (-O-  turn it all off)\n"
    "  -Os             Optimize for size\n"
    "  -O:name,-name   Switch single optimizations on or off (see the manual)\n"
    "  -Z              Suppress redundant loads\n"
    "  -Pg[file]       Write a profile (default fcprof.dat) when the program ends\n"
    "  -Pu[file]       Optimize using that profile\n"
    "\n"
    "Messages\n"
    "  -w, -w-         Display all warnings / suppress all warnings\n"
    "  -w-xxx          Warning off: use pro ret par rvl sus ucp cpt rch cln\n"
    "  -wxxx           Turn one warning on: amb stv (off by default)\n"
    "  -jN             Stop after N errors\n"
    "  -gN             Stop after N warnings\n"
    "\n"
    "Libraries\n"
    "  -ln             Do not search the standard libraries\n"
    "  -lnm            Do not link the math library\n"
    "  wildargs        As a file name: link the wildcard argument expander\n"
    "\n"
    "  -h              Show this help\n";

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
