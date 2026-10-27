/*
 * File:    dbgtest.c
 * Purpose: Headless driver for the debugger core. usage: dbgtest file.c < script
 *          script commands: b LINE | tb LINE | go | s | t | u LINE | e EXPR | set
 *          EXPR=VAL | bt | quit
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "dbg.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* Prints a compiler diagnostic to stderr. */
static void diag(void *u, int sev, const char *f, int l, const char *m)
{
    (void)u;
    fprintf(stderr, "%s:%d: %s\n", f, l, m);
    (void)sev;
}

/* Prints the current debug position. */
static void where(const char *what)
{
    char f[300];
    int l;
    if (dbg_state() == DS_EXITED) {
        printf("%s: exited code %d\n", what, dbg_exit_code());
        return;
    }
    if (dbg_where(f, sizeof f, &l) == 0) {
        const char *b = strrchr(f, '/');
        printf("%s: %s:%d\n", what, b ? b + 1 : f, l);
    } else {
        printf("%s: ?\n", what);
    }
}

/* Test driver for the debug information produced by the compiler. */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: dbgtest file.c < script\n");
        return 2;
    }
    FcContext c;
    memset(&c, 0, sizeof c);
    fc_options_default(&c.opt);
    c.opt.debug = 1;
    c.diag = diag;
    c.user = &c;
    const char *files[1] = {argv[1]};
    if (fc_compile(&c, files, 1, "/tmp/dbgtest.out")) {
        fprintf(stderr, "compile failed\n");
        return 1;
    }
    char src[PATH_MAX];
    if (!realpath(argv[1], src)) {
        strcpy(src, argv[1]);
    }
    if (dbg_start("/tmp/dbgtest.out", "")) {
        printf("start failed: %s\n", dbg_msg);
        return 1;
    }
    where("start");
    char ln[400];
    FILE *sf = argc > 2 ? fopen(argv[2], "r") : stdin;
    while (fgets(ln, sizeof ln, sf)) {
        ln[strcspn(ln, "\n")] = 0;
        char *a = ln + (ln[0] && strchr(ln, ' ') ? strcspn(ln, " ") + 1 : strlen(ln));
        int r = 0;
        if (!strncmp(ln, "b ", 2)) {
            printf("bp %d -> %d\n", atoi(a), dbg_bp_toggle(src, atoi(a)));
            continue;
        } else if (!strncmp(ln, "go", 2)) {
            r = dbg_go();
        } else if (!strncmp(ln, "s", 1) && (ln[1] == 0)) {
            r = dbg_step(0);
        } else if (!strncmp(ln, "t", 1) && (ln[1] == 0)) {
            r = dbg_step(1);
        } else if (!strncmp(ln, "u ", 2)) {
            r = dbg_goto_line(src, atoi(a));
        } else if (!strncmp(ln, "e ", 2)) {
            char o[400];
            int k = dbg_eval(a, o, sizeof o);
            printf("%s%s\n", k ? "ERR " : "", o);
            continue;
        } else if (!strncmp(ln, "set ", 4)) {
            char *eq = strchr(a, '=');
            *eq = 0;
            char o[400];
            dbg_assign(a, eq + 1, o, sizeof o);
            printf("%s\n", o);
            continue;
        } else if (!strncmp(ln, "bt", 2)) {
            for (int i = 0; i < dbg_nframes(); i++) {
                char o[300];
                dbg_frame_text(i, o, sizeof o);
                printf("#%d %s line %d\n", i, o, dbg_frame(i)->line);
            }
            continue;
        } else if (!strncmp(ln, "sel ", 4)) {
            dbg_select_frame(atoi(a));
            continue;
        } else if (!strncmp(ln, "quit", 4)) {
            break;
        }
        where(r == DR_BREAK ? "break" : r == DR_EXIT ? "exit" : r == DR_ERROR ? "error" : "stop");
        if (r == DR_ERROR) {
            printf("  %s\n", dbg_msg);
        }
        int n;
        const char *p = dbg_pending_output(&n);
        if (n) {
            printf("  output: %.*s", n, p);
            dbg_clear_output();
        }
        if (dbg_state() == DS_EXITED) {
            break;
        }
    }
    dbg_kill();
    return 0;
}
