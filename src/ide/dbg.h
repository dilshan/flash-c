/*
 * File:    dbg.h
 * Purpose: Flash C - source-level debugger core (ptrace on Linux x86-64)
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
/*
 * The program is started under ptrace with its terminal on a pseudo terminal, so it behaves as if
 * it ran on a console while the IDE keeps its own screen. The compiler (option -v) supplies line
 * tables, function and variable descriptions (FcDebug in fc.h). Breakpoints are int3 instructions
 * that are inserted only while the program runs and removed whenever it stops.
 */
#ifndef DBG_H
#define DBG_H
#include "fc.h"

enum { DS_NONE, DS_STOPPED, DS_EXITED };

/* results of the execution commands */
enum { DR_STEP = 1, DR_BREAK, DR_GOTO, DR_SIGNAL, DR_EXIT, DR_ERROR };

#define DBG_MAXBP 64

typedef struct DbgBP {
    char file[300];
    int line; /* editor file (absolute path) and 1-based line */
    char cond[120];
    int pass, hits, disabled;
} DbgBP;

extern DbgBP dbg_bps[DBG_MAXBP];
extern int dbg_nbps;
extern int dbg_radix;     /* 0 decimal, 1 hexadecimal, 2 both (inspectors and watches) */
extern char dbg_msg[200]; /* text of the last error or stop reason */
/* Called before the program runs (mode 1: free run, 2: inside a step) and after it stopped (mode
 * 0). Returns nonzero when the program's screen is now displayed (its output and keyboard input are
 * then connected to the terminal); otherwise output is collected and read with dbg_pending_output.
 */
extern int (*dbg_screen_hook)(int mode);

int dbg_info_ok(void); /* the last compile recorded debug information */
int dbg_state(void);
int dbg_start(const char *exe, const char *args); /* runs to the first line of main; 0 on success */
void dbg_kill(void);                              /* program reset */
int dbg_exit_code(void); /* exit code, or -signal when it was killed by a signal */
int dbg_signal(void);    /* signal that stopped the program (after DR_SIGNAL) */
int dbg_go(void);
int dbg_step(int into);
int dbg_goto_line(const char *file, int line);
int dbg_run_to_return(void); /* not used by the menus; finishes the current function */

/* position of the selected frame */
int dbg_where(char *file, int n, int *line);
int dbg_has_code(const char *file, int line); /* the line produces code */
int dbg_find_function(const char *name, char *file, int n, int *line);

/* program output produced while the IDE screen was displayed */
const char *dbg_pending_output(int *len);
/* called with every piece of program output as it is read from the pseudo terminal (the IDE copies
 * it to the Output window) */
extern void (*dbg_output_hook)(const char *data, int len);
void dbg_clear_output(void);

/* breakpoints */
int dbg_bp_find(const char *file, int line);
int dbg_bp_toggle(const char *file,
                  int line); /* returns 1 set, 0 cleared, -1 no code on that line */
int dbg_bp_add(const char *file, int line, const char *cond, int pass);
void dbg_bp_delete(int i);
int dbg_line_has_bp(const char *file, int line); /* 0 none, 1 enabled, 2 disabled */

/* frames */
typedef struct DbgFrame {
    unsigned long pc, fp;
    const FcDFunc *fn;
    int line;
    char file[300];
} DbgFrame;

int dbg_nframes(void);
const DbgFrame *dbg_frame(int i);
int dbg_frame_text(int i, char *out, int n); /* "add(a=1, b=2)" */
void dbg_select_frame(int i);
int dbg_selected_frame(void);

/* data */
int dbg_eval(const char *expr, char *out, int n); /* formatted value; 0 ok, -1 error text in out */
int dbg_assign(const char *expr, const char *value, char *out, int n); /* expr = value */
int dbg_inspect_lines(const char *expr, char (*lines)[160], char (*exprs)[160], int max,
                      char *title, int tn);
int dbg_can_modify(const char *expr);
int dbg_nregs(void);
int dbg_reg_text(int i, char *out, int n);

#endif
