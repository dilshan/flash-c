/*
 * File:    debugger.c
 * Purpose: Flash C IDE - source-level debugger user interface (commands, dialogs,
 *          execution marker)
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "ide.h"
#include "dbg.h"
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>

extern const char *ide_exe_name(char *buf, int n);

static char exec_file[300];
static int exec_line; /* 1-based; exec_line 0 = no marker */
static int user_shown;

/* ------------------------------------------------------------------ screen swapping */
static void wait_any_key(void)
{
    struct termios o, t;
    tcgetattr(0, &o);
    t = o;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    unsigned char c;
    ssize_t r = read(0, &c, 1);
    (void)r;
    tcsetattr(0, TCSAFLUSH, &o);
}

/*
 * Debugger callback invoked around program runs: shows the program's screen while it
 * runs and, after it ended, optionally waits for a key (see dbg_screen_hook).
 */
static int screen_hook(int mode)
{
    if (mode == 0) {
        if (!user_shown) {
            return 0;
        }
        if (dbg_state() == DS_EXITED && cfg.pause_after_run) {
            int code = dbg_exit_code();
            if (code >= 0) {
                printf("\n[Program exited with code %d]", code);
            } else {
                printf("\n[Program terminated by signal %d]", -code);
            }
            printf("\nPress any key to return to Flash C . . .");
            fflush(stdout);
            wait_any_key();
            printf("\n");
        }
        term_resume();
        user_shown = 0;
        return 0;
    }
    /* cfg.dbg_swap: 0 none, 1 smart, 2 always. A program that runs freely needs its screen; a step
     * only with "always". */
    if (mode == 2 && cfg.dbg_swap != 2) {
        return 0;
    }
    if (!user_shown) {
        term_suspend();
        fflush(stdout);
        user_shown = 1;
    }
    return 1;
}

/* program output that arrived while the IDE screen was displayed is written to the user screen */
static void flush_output(void)
{
    int n;
    const char *p = dbg_pending_output(&n);
    if (n <= 0) {
        return;
    }
    term_suspend();
    fwrite(p, 1, n, stdout);
    fflush(stdout);
    term_resume();
    dbg_clear_output();
}

/* ------------------------------------------------------------------ markers and positions */
int dbgui_line_pal(const char *file, int line)
{
    if (exec_line == line && !strcmp(exec_file, file)) {
        return P_ED_TRACK;
    }
    for (int i = 0; i < dbg_nbps; i++) {
        if (dbg_bps[i].line == line && !strcmp(dbg_bps[i].file, file)) {
            return P_ED_BPLINE;
        }
    }
    return -1;
}

/* Returns true while a debugging session is stopped and can be inspected. */
int dbgui_active(void)
{
    return dbg_state() == DS_STOPPED;
}

/* Ends the session (Run|Program reset) and removes the execution bar. */
void dbgui_reset(void)
{
    dbg_kill();
    exec_line = 0;
    exec_file[0] = 0;
}

/* Moves the editor to the line where the program stopped and marks it with the execution bar. */
static void show_position(void)
{
    char f[300];
    int l;
    if (dbg_where(f, sizeof f, &l)) {
        exec_line = 0;
        return;
    }
    snprintf(exec_file, sizeof exec_file, "%s", f);
    exec_line = l;
    Win *w = win_open_file(f);
    if (!w) {
        return;
    }
    /* keep the Watch and Register windows visible below the edit window */
    for (int i = 0; i < nwins; i++) {
        Win *o = wins[i];
        if ((o->kind == W_WATCH || o->kind == W_REG) && o->y > w->y + 5 && w->y + w->h > o->y &&
            o->x < w->x + w->w && w->x < o->x + o->w) {
            w->h = o->y - w->y;
        }
    }
    ed_goto(w, l - 1, 0);
    if (w->top > l - 1 || w->top + w->h - 3 < l - 1) {
        w->top = l - 1 - (w->h - 2) / 2;
        if (w->top < 0) {
            w->top = 0;
        }
    }
    ed_ensure_visible(w);
}

/* Returns the active editor window, or the topmost one. */
static Win *edit_win(void)
{
    Win *w = win_active();
    if (w && w->kind == W_EDIT) {
        return w;
    }
    for (int i = nwins - 1; i >= 0; i--) {
        if (wins[i]->kind == W_EDIT) {
            return wins[i];
        }
    }
    return NULL;
}

/* Reports the outcome r of an execution command (exit, signal, error) to the user. */
static void report(int r)
{
    flush_output();
    if (r == DR_EXIT) {
        exec_line = 0;
        int code = dbg_exit_code();
        char m[200];
        if (code >= 0) {
            snprintf(m, sizeof m, "Program terminated.\nProgram exit code: %d", code);
        } else {
            snprintf(m, sizeof m, "Program terminated by signal %d.", -code);
        }
        dbg_kill();
        msgbox(" Debugger ", m, MB_OK);
        return;
    }
    show_position();
    if (r == DR_SIGNAL) {
        char m[200];
        int s = dbg_signal();
        if (s == 2) {
            snprintf(m, sizeof m, "User break (signal %d)", s);
        } else {
            snprintf(m, sizeof m, "Program received signal %d", s);
        }
        msgbox(" Debugger ", m, MB_OK | MB_WARN);
    } else if (r == DR_ERROR) {
        msgbox(" Debugger ", dbg_msg[0] ? dbg_msg : "Debugger error", MB_OK | MB_ERROR);
    }
}

/* start the program under the debugger (building it first) and run to the first statement of main
 */
static int ensure_session(void)
{
    if (dbg_state() == DS_STOPPED) {
        return 0;
    }
    dbg_kill();
    if (cfg.dbg_source == 2) {
        msgbox(" Debugger ", "Source debugging is turned off\n(Options|Debugger|Source debugging).",
               MB_OK | MB_ERROR);
        return -1;
    }
    if (ide_build(BUILD_FOR_RUN)) {
        return -1;
    }
    if (!dbg_info_ok() && ide_build(BUILD_ALL)) {
        return -1;
    }
    if (!dbg_info_ok()) {
        msgbox(" Debugger ", "No debug information is available for this program.",
               MB_OK | MB_ERROR);
        return -1;
    }
    char exe[700];
    ide_exe_name(exe, sizeof exe);
    char *abs = realpath(exe, NULL);
    out_clear(); /* the Output window shows the output of this session only */
    int r = dbg_start(abs ? abs : exe, cfg.run_args);
    free(abs);
    if (r) {
        msgbox(" Debugger ", dbg_msg, MB_OK | MB_ERROR);
        return -1;
    }
    flush_output();
    return 1;
}

/* Connects the debugger core to the IDE: program screen, Output window and number format. */
void dbgui_init(void)
{
    dbg_screen_hook = screen_hook;
    dbg_output_hook = out_feed;
    dbg_radix = cfg.dbg_radix;
}

/* Ctrl+F9: with breakpoints (or while debugging) the program runs under the debugger */
int dbgui_wants_run(void)
{
    return dbg_state() == DS_STOPPED || (cfg.dbg_source != 2 && dbg_nbps > 0);
}

/* Run|Run under the debugger: starts the program or continues it. */
void dbgui_go(void)
{
    dbg_radix = cfg.dbg_radix;
    int fresh = ensure_session();
    if (fresh < 0) {
        return;
    }
    report(dbg_go());
}

/* Run|Step over (into = 0) or Trace into (into = 1). */
void dbgui_step(int into)
{
    dbg_radix = cfg.dbg_radix;
    int fresh = ensure_session();
    if (fresh < 0) {
        return;
    }
    if (fresh) {
        show_position();
        return;
    }
    report(dbg_step(into));
}

/* Run|Go to cursor: runs until the line the cursor is on. */
void dbgui_goto_cursor(void)
{
    Win *w = edit_win();
    if (!w || !w->buf->named) {
        return;
    }
    dbg_radix = cfg.dbg_radix;
    int fresh = ensure_session();
    if (fresh < 0) {
        return;
    }
    report(dbg_goto_line(w->buf->name, w->cur_line + 1));
}

/* Debug|Toggle breakpoint on the cursor line. */
void dbgui_toggle_bp(void)
{
    Win *w = win_active();
    if (!w || w->kind != W_EDIT || !w->buf->named) {
        return;
    }
    if (dbg_bp_toggle(w->buf->name, w->cur_line + 1) < 0) {
        msgbox(" Breakpoint ", dbg_msg, MB_OK | MB_WARN);
    }
}

/* ------------------------------------------------------------------ breakpoints dialogs */
static int bp_edit(DbgBP *b, int is_new)
{
    Dialog d;
    dlg_init(&d, is_new ? " New Breakpoint " : " Modify Breakpoint ");
    d.w = 66;
    d.h = 16;
    char cond[120], file[300], line[16], pass[16];
    snprintf(cond, sizeof cond, "%s", b->cond);
    snprintf(file, sizeof file, "%s", b->file);
    snprintf(line, sizeof line, "%d", b->line);
    snprintf(pass, sizeof pass, "%d", b->pass);
    dlg_input(&d, 3, 1, 58, "~C~ondition", cond, sizeof cond,
              "Expression that must be true (nonzero) for the breakpoint to stop the program");
    dlg_input(&d, 3, 4, 12, "~P~ass Count", pass, sizeof pass,
              "Number of times the breakpoint is reached before the program stops");
    dlg_input(&d, 3, 7, 58, "~F~ile Name", file, sizeof file,
              "Source file that contains the breakpoint");
    dlg_input(&d, 3, 10, 12, "~L~ine Number", line, sizeof line, "Line number of the breakpoint");
    dlg_button(&d, 3, 13, 12, is_new ? "~N~ew" : "~M~odify", cmOK, 1, NULL);
    dlg_button(&d, 17, 13, 12, "Cancel", cmCancel, 0, NULL);
    dlg_button(&d, 31, 13, 12, "Help", cmHelp, 0, NULL);
    if (dlg_run(&d) != cmOK) {
        return 0;
    }
    int ln = atoi(line);
    if (ln < 1) {
        msgbox(" Breakpoint ", "Invalid line number", MB_OK | MB_ERROR);
        return 0;
    }
    char *r = realpath(file, NULL);
    snprintf(b->file, sizeof b->file, "%s", r ? r : file);
    free(r);
    snprintf(b->cond, sizeof b->cond, "%s", cond);
    b->line = ln;
    b->pass = atoi(pass);
    b->hits = 0;
    return 1;
}

/* Asks for a function name and sets a breakpoint at its first line. */
static void bp_at_function(void)
{
    static char name[128];
    if (!inputbox(" Breakpoint at Function ", "~S~ymbol Name", name, sizeof name,
                  "Name of the function where the program should stop")) {
        return;
    }
    char f[300];
    int l;
    if (!dbg_info_ok() || dbg_find_function(name, f, sizeof f, &l)) {
        msgbox(" Breakpoint ", "Function not found (compile the program first)", MB_OK | MB_ERROR);
        return;
    }
    int k;
    for (k = 0; k < 40 && !dbg_has_code(f, l + k); k++)
        ;
    if (k == 40) {
        msgbox(" Breakpoint ", "No code for that function", MB_OK | MB_ERROR);
        return;
    }
    if (dbg_bp_find(f, l + k) < 0) {
        dbg_bp_add(f, l + k, NULL, 0);
    }
}

/* Shows the source line of breakpoint i in the editor. */
static void goto_bp(int i)
{
    if (i < 0 || i >= dbg_nbps) {
        return;
    }
    Win *w = win_open_file(dbg_bps[i].file);
    if (w) {
        ed_goto(w, dbg_bps[i].line - 1, 0);
        ed_ensure_visible(w);
    }
}

/* Shows the Breakpoints dialog (add, edit, delete, enable, view). */
void dbgui_breakpoints(void)
{
    static char *items[DBG_MAXBP];
    static char store[DBG_MAXBP][400];
    int sel = 0;
    for (;;) {
        Dialog d;
        dlg_init(&d, " Breakpoints ");
        d.w = 70;
        d.h = 17;
        for (int i = 0; i < dbg_nbps; i++) {
            const char *b = strrchr(dbg_bps[i].file, '/');
            b = b ? b + 1 : dbg_bps[i].file;
            char c[40];
            snprintf(c, sizeof c, "%s", dbg_bps[i].cond);
            snprintf(store[i], sizeof store[i], "%-14s %-6d %-22.22s %d", b, dbg_bps[i].line, c,
                     dbg_bps[i].pass);
            items[i] = store[i];
        }
        dlg_static(&d, 3, 1, "Breakpoint List   Line#  Condition              Pass");
        DItem *l = dlg_list(&d, 3, 2, 50, 11, items, dbg_nbps,
                            "Use cursor keys to examine the list of breakpoints");
        if (sel < dbg_nbps) {
            l->sel = sel;
        }
        dlg_button(&d, 55, 2, 12, "OK", cmOK, 1, NULL);
        dlg_button(&d, 55, 4, 12, "~E~dit", cmEdit, 0, "Modify the selected breakpoint");
        dlg_button(&d, 55, 6, 12, "~D~elete", cmDelete, 0, "Delete the selected breakpoint");
        dlg_button(&d, 55, 8, 12, "~V~iew", cmView, 0,
                   "Show the line of the selected breakpoint in the editor");
        dlg_button(&d, 55, 10, 12, "~A~t...", cmAt, 0, "Set a breakpoint at a function");
        dlg_button(&d, 55, 12, 12, "~N~ew", cmNew, 0, "Create a new breakpoint");
        int r = dlg_run(&d);
        sel = l->sel;
        if (r == cmEdit && dbg_nbps) {
            bp_edit(&dbg_bps[sel], 0);
        } else if (r == cmDelete && dbg_nbps) {
            dbg_bp_delete(sel);
            if (sel >= dbg_nbps) {
                sel = dbg_nbps - 1;
            }
            if (sel < 0) {
                sel = 0;
            }
        } else if (r == cmView && dbg_nbps) {
            goto_bp(sel);
            break;
        } else if (r == cmAt) {
            bp_at_function();
        } else if (r == cmNew && dbg_nbps < DBG_MAXBP) {
            DbgBP b;
            memset(&b, 0, sizeof b);
            Win *w = edit_win();
            if (w && w->buf->named) {
                snprintf(b.file, sizeof b.file, "%s", w->buf->name);
                b.line = w->cur_line + 1;
            }
            if (bp_edit(&b, 1)) {
                int k = dbg_bp_add(b.file, b.line, b.cond, b.pass);
                (void)k;
            }
        } else {
            break;
        }
    }
}

/* ------------------------------------------------------------------ call stack */
void dbgui_call_stack(void)
{
    static char *items[64];
    static char store[64][300];
    int n = dbg_nframes();
    if (!n) {
        msgbox(" Call Stack ", "No call stack available", MB_OK);
        return;
    }
    for (int i = 0; i < n; i++) {
        char t[260];
        dbg_frame_text(i, t, sizeof t);
        const DbgFrame *f = dbg_frame(i);
        snprintf(store[i], sizeof store[i], "%s  line %d", t, f->line);
        items[i] = store[i];
    }
    Dialog d;
    dlg_init(&d, " Call Stack ");
    d.w = 70;
    d.h = 17;
    DItem *l = dlg_list(&d, 3, 2, 62, 11, items, n,
                        "Use cursor keys to select a function; Enter shows its source");
    l->sel = dbg_selected_frame();
    dlg_button(&d, 3, 14, 12, "~S~ource", cmOK, 1, "Show the source of the selected call");
    dlg_button(&d, 17, 14, 12, "Cancel", cmCancel, 0, NULL);
    int r = dlg_run(&d);
    if (r == cmOK) {
        dbg_select_frame(l->sel);
        const DbgFrame *f = dbg_frame(l->sel);
        if (f) {
            snprintf(exec_file, sizeof exec_file, "%s", f->file);
            exec_line = f->line;
            show_position();
        }
    }
}

/* ------------------------------------------------------------------ inspector */
#define INS_MAX 128
static char ins_lines[INS_MAX][160], ins_exprs[INS_MAX][160];
static char *ins_items[INS_MAX];

/*
 * Opens an Inspect window for expr; members that are structs or pointers can be opened
 * further (depth limits the nesting).
 */
static void inspect_expr(const char *expr, int depth)
{
    if (depth > 8) {
        return;
    }
    char title[200];
    int n = dbg_inspect_lines(expr, ins_lines, ins_exprs, INS_MAX, title, sizeof title);
    if (n < 0) {
        msgbox(" Inspector ", title, MB_OK | MB_ERROR);
        return;
    }
    char cur[300];
    snprintf(cur, sizeof cur, "%s", expr);
    int sel = 0;
    for (;;) {
        char lines[INS_MAX][160], exprs[INS_MAX][160];
        n = dbg_inspect_lines(cur, lines, exprs, INS_MAX, title, sizeof title);
        if (n < 0) {
            msgbox(" Inspector ", title, MB_OK | MB_ERROR);
            return;
        }
        for (int i = 0; i < n; i++) {
            ins_items[i] = lines[i];
        }
        Dialog d;
        char hd[260];
        snprintf(hd, sizeof hd, " Inspecting %.40s ", cur);
        dlg_init(&d, hd);
        d.w = 72;
        d.h = 19;
        dlg_static(&d, 3, 1, title);
        DItem *l = dlg_list(&d, 3, 2, 64, 12, ins_items, n,
                            "Use cursor keys to examine; Enter inspects the selected element");
        if (sel < n) {
            l->sel = sel;
        }
        dlg_button(&d, 3, 15, 12, "~I~nspect", cmView, 1, "Inspect the selected element");
        dlg_button(&d, 17, 15, 12, "~M~odify", cmModify, 0,
                   "Change the value of the selected element");
        dlg_button(&d, 31, 15, 12, "~R~ange...", cmReplace, 0, "Inspect a range of array elements");
        dlg_button(&d, 45, 15, 12, "Close", cmCancel, 0, NULL);
        int r = dlg_run(&d);
        sel = l->sel;
        if (r == cmView && n) {
            if (strcmp(exprs[sel], cur)) {
                inspect_expr(exprs[sel], depth + 1);
            }
        } else if (r == cmModify && n) {
            char nv[200] = "", out[200];
            if (!dbg_can_modify(exprs[sel])) {
                msgbox(" Data Inspect ", "Cannot be modified", MB_OK | MB_ERROR);
                continue;
            }
            if (inputbox(" Modify Data ", "~N~ew Value", nv, sizeof nv,
                         "Enter the new value for the data element") &&
                nv[0]) {
                if (dbg_assign(exprs[sel], nv, out, sizeof out)) {
                    msgbox(" Data Inspect ", out, MB_OK | MB_ERROR);
                }
            }
        } else if (r == cmReplace) {
            char st[16] = "0", ct[16] = "10";
            Dialog rd;
            dlg_init(&rd, " Range Inspect ");
            rd.w = 40;
            rd.h = 10;
            dlg_input(&rd, 3, 1, 10, "~S~tarting Index", st, sizeof st,
                      "Index of the first element to inspect");
            dlg_input(&rd, 3, 4, 10, "~C~ount", ct, sizeof ct, "Number of elements to inspect");
            dlg_button(&rd, 3, 7, 10, "OK", cmOK, 1, NULL);
            dlg_button(&rd, 15, 7, 10, "Cancel", cmCancel, 0, NULL);
            if (dlg_run(&rd) == cmOK) {
                int s0 = atoi(st), c0 = atoi(ct);
                if (c0 < 1) {
                    c0 = 1;
                }
                if (c0 > INS_MAX) {
                    c0 = INS_MAX;
                }
                if (s0 < 0) {
                    s0 = 0;
                }
                static char rl[INS_MAX][160];
                static char *ri[INS_MAX];
                int rn = 0;
                for (int i = 0; i < c0; i++) {
                    char e[320], v[120];
                    snprintf(e, sizeof e, "(%s)[%d]", cur, s0 + i);
                    if (dbg_eval(e, v, sizeof v)) {
                        snprintf(rl[rn], 160, "[%d]  %s", s0 + i, v);
                        rn++;
                        break;
                    }
                    snprintf(rl[rn], 160, "[%d]  %s", s0 + i, v);
                    rn++;
                }
                for (int i = 0; i < rn; i++) {
                    ri[i] = rl[i];
                }
                Dialog ld;
                char t2[200];
                snprintf(t2, sizeof t2, " %.30s[%d..%d] ", cur, s0, s0 + c0 - 1);
                dlg_init(&ld, t2);
                ld.w = 60;
                ld.h = 18;
                dlg_list(&ld, 3, 1, 52, 13, ri, rn, NULL);
                dlg_button(&ld, 3, 15, 10, "OK", cmOK, 1, NULL);
                dlg_run(&ld);
            }
        } else {
            break;
        }
    }
}

/* Shows the Debug|Inspect window for the expression under the cursor or chosen by the user. */
void dbgui_inspect(void)
{
    char expr[200] = "";
    Win *e = win_active();
    if (e && e->kind == W_EDIT) {
        Line *L = &e->buf->lines[e->cur_line];
        int s = e->cur_col, t = e->cur_col;
        while (s > 0 && (isalnum((unsigned char)L->text[s - 1]) || L->text[s - 1] == '_')) {
            s--;
        }
        while (t < L->len && (isalnum((unsigned char)L->text[t]) || L->text[t] == '_')) {
            t++;
        }
        int n = t - s;
        if (n > 199) {
            n = 199;
        }
        memcpy(expr, L->text + s, n);
        expr[n] = 0;
    }
    if (!inputbox(" Inspect ", "~I~nspect", expr, sizeof expr,
                  "Enter the variable or expression to inspect") ||
        !expr[0]) {
        return;
    }
    inspect_expr(expr, 0);
}

/* ------------------------------------------------------------------ used by other modules */
/* evaluate for the Watch window / Evaluate dialog: 0 value in out, -1 error text in out, -2 no
 * program */
int dbgui_eval(const char *expr, char *out, int n)
{
    if (dbg_state() != DS_STOPPED) {
        return -2;
    }
    return dbg_eval(expr, out, n);
}

/* Assigns val to the object designated by expr (Debug|Evaluate/modify). */
int dbgui_assign(const char *expr, const char *val, char *out, int n)
{
    if (dbg_state() != DS_STOPPED) {
        snprintf(out, n, "Program is not running");
        return -1;
    }
    return dbg_assign(expr, val, out, n);
}

/* breakpoints and the desktop file */
void dbgui_save_bps(FILE *f)
{
    for (int i = 0; i < dbg_nbps; i++) {
        fprintf(f, "bp=%d %d %d %d|%s|%s\n", dbg_bps[i].line, dbg_bps[i].pass, dbg_bps[i].disabled,
                0, dbg_bps[i].cond, dbg_bps[i].file);
    }
}

/*
 * Restores one breakpoint from its line in the desktop file ("line pass disabled z|cond|file").
 */
void dbgui_load_bp(const char *s)
{
    int line, pass, dis, z, off = 0;
    if (sscanf(s, "%d %d %d %d|%n", &line, &pass, &dis, &z, &off) < 4 || !off) {
        return;
    }
    const char *cond = s + off;
    const char *bar = strchr(cond, '|');
    if (!bar) {
        return;
    }
    char c[120];
    int cl = (int)(bar - cond);
    if (cl > 119) {
        cl = 119;
    }
    memcpy(c, cond, cl);
    c[cl] = 0;
    int k = dbg_bp_add(bar + 1, line, c, pass);
    if (k >= 0) {
        dbg_bps[k].disabled = dis;
    }
}
