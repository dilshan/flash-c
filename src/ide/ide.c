/*
 * File:    ide.c
 * Purpose: Flash C IDE - commands, build/run integration, main loop
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <pty.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "ide.h"
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <stdarg.h>

extern int undo_cursor_line, undo_cursor_col;
extern Win *win_get_special(int kind);

/* Returns true if the path exists. */
int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

/* Copies the file-name part of a path into out (size n). */
void base_name(const char *path, char *out, int n)
{
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    snprintf(out, n, "%s", s);
}

static int quit_flag;
static int key_queue[16], nkq;

/* Queues a key press as if typed (used to deliver commands from menus and the status line). */
void ide_inject_key(int k)
{
    if (nkq < 16) {
        key_queue[nkq++] = k;
    }
}

/* Requests a full repaint on the next screen update. */
void ide_refresh(void)
{
    scr_invalidate();
}

/* Shows a printf-style message box. */
void ide_message(const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    msgbox(" Message ", b, MB_OK);
}

static int last_lines, last_errors, last_warnings;
static int last_was_replace;
static int project_dirty;

/* Returns the active window if it is an editor-like window, else NULL. */
static Win *cur_edit(void)
{
    Win *w = win_active();
    if (w && (w->kind == W_EDIT || w->kind == W_CLIP || w->kind == W_NOTES)) {
        return w;
    }
    return NULL;
}

/* Returns the topmost editor window, or NULL. */
static Win *any_edit(void)
{
    for (int i = nwins - 1; i >= 0; i--) {
        if (wins[i]->kind == W_EDIT) {
            return wins[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* enabling of menu items                                              */
/* ------------------------------------------------------------------ */
int cmd_enabled(int cmd)
{
    Win *e = cur_edit();
    switch (cmd) {
    case cmUndo:
        return e && e->buf->undo_pos > 0;
    case cmRedo:
        return e && e->buf->undo_pos < e->buf->undo_count;
    case cmCopy: {
        Win *a = win_active();
        if (a && a->kind == W_HELP) {
            return help_has_selection(a);
        }
        return e && ed_has_block(e);
    }
    case cmCut:
    case cmClear:
        return e && ed_has_block(e);
    case cmPaste:
        return e && !(clipbuf->n == 1 && clipbuf->lines[0].len == 0);
    case cmCopyExample: {
        char *t = help_example_text();
        int ok = t != NULL;
        free(t);
        return ok;
    }
    case cmSaveFile:
    case cmSaveAs:
        return e != NULL && e->kind == W_EDIT;
    case cmSaveAll:
        return any_edit() != NULL;
    case cmPrint:
        return e != NULL;
    case cmFind:
    case cmReplaceText:
    case cmSearchAgain:
    case cmGotoLine:
    case cmLocateFn:
        return e != NULL;
    case cmPrevError:
    case cmNextError:
        return nmsgs > 0;
    case cmProgReset:
        return dbgui_active();
    case cmGotoCursor:
    case cmTraceInto:
    case cmStepOver:
        return any_edit() != NULL || nproj > 0 || dbgui_active();
    case cmInspect:
    case cmCallStack:
        return dbgui_active();
    case cmToggleBP:
        return e != NULL && e->kind == W_EDIT;
    case cmBreakpoints:
        return 1;
    case cmCompile:
    case cmMake:
    case cmLink:
    case cmBuildAll:
    case cmRun:
        return any_edit() != NULL || nproj > 0;
    case cmCloseProject:
    case cmAddItem:
        return project_name[0] != 0;
    case cmDelItem:
    case cmLocalOpts:
    case cmIncludeFiles:
        return nproj > 0;
    case cmDelWatch:
    case cmEditWatch:
    case cmRemoveWatches:
        return nwatch > 0;
    case cmSizeMove:
    case cmZoom:
    case cmNextWin:
    case cmCloseWin:
    case cmCloseAll:
        return nwins > 0;
    case cmTile:
    case cmCascade:
        return nwins > 1;
    case cmWinList:
        return nwins > 0;
    case cmHelpPrev:
        return win_find_kind(W_HELP) != NULL;
    case cmRemoveMsgs:
        return nmsgs > 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* messages                                                            */
/* ------------------------------------------------------------------ */
void msg_goto(int idx, int focus_editor)
{
    if (idx < 0 || idx >= nmsgs) {
        return;
    }
    Msg *m = &msgs[idx];
    msgsel = idx;
    if (m->header || m->line <= 0 || !m->file[0]) {
        return;
    }
    Win *mw = win_find_kind(W_MSG);
    for (int i = 0; i < nwins; i++) {
        if (wins[i]->kind == W_EDIT) {
            wins[i]->error_line = -1;
        }
    }
    if (!file_exists(m->file)) {
        return;
    }
    Win *w = win_open_file(m->file);
    if (!w) {
        return;
    }
    w->error_line = m->line - 1;
    w->error_version = w->buf->version;
    ed_goto(w, m->line - 1, 0);
    if (w->cur_line > 0) {
        w->top = w->cur_line - (w->h - 2) / 2;
        if (w->top < 0) {
            w->top = 0;
        }
        ed_ensure_visible(w);
    }
    if (!focus_editor && mw) {
        win_activate(mw);
    } else {
        win_activate(w);
    }
}

/* Jumps to the previous (dir = -1) or next (dir = 1) message that has a source position. */
static void goto_error(int dir)
{
    if (!nmsgs) {
        return;
    }
    int i = msgsel;
    for (int k = 0; k < nmsgs; k++) {
        i += dir;
        if (i < 0 || i >= nmsgs) {
            break;
        }
        if (!msgs[i].header && msgs[i].line > 0) {
            msg_goto(i, 1);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* build                                                               */
/* ------------------------------------------------------------------ */
void ide_fill_options(FcOptions *o)
{
    fc_options_default(o);
    o->debug = cfg.dbg_source != 2;
    o->nested_comments = cfg.nested_comments;
    o->unsigned_char = cfg.unsigned_char;
    o->ident_len = cfg.ident_len >= 32 ? 0 : cfg.ident_len;
    o->max_errors = cfg.max_errors;
    o->max_warnings = cfg.max_warnings;
    o->warn_level = cfg.warn_display == 0 ? 0 : 1;
    int all = cfg.warn_display == 2;
    o->w_pointer = all || cfg.warn[0];
    o->w_susp = all || cfg.warn[8];
    o->w_cpt = all || cfg.warn[1];
    o->w_ucp = all || cfg.warn[2];
    o->w_unreach = all || cfg.warn[40];
    o->w_amb = all || cfg.warn[41];
    o->w_structval = all || cfg.warn[42];
    o->w_cond = all || cfg.warn[44];
    o->enum_int = cfg.enum_int;
    o->merge_strings = cfg.merge_strings;
    o->align_struct = cfg.align_struct;
    o->stack_check = cfg.stack_check;
    o->ansi_keywords = cfg.ansi_kw;
    o->jump_opt = cfg.jump_opt;
    o->opt_flags = 0;
    for (int i = 0; i < NOPT; i++) {
        if (cfg.opt[i]) {
            o->opt_flags |= 1u << i;
        }
    }
    o->redundant_loads = cfg.red_loads;
    o->stack_kb = cfg.stack_kb > 0 ? cfg.stack_kb : 0;
    o->elf_symtab = cfg.elf_symtab;
    o->wx = cfg.wx_segments;
    o->w_noreturn = all || cfg.warn[32];
    o->w_unused = all || cfg.warn[37] || cfg.warn[48];
    o->w_noproto = all || cfg.warn[43];
    o->w_noeffect = all || cfg.warn[34];
    o->w_assign = all || cfg.warn[38];
    o->w_divzero = all || cfg.warn[15];
    o->w_voidret = all || cfg.warn[6];
    o->fp_mode = cfg.fp ? 1 : 0;
    o->map = cfg.map_file < 0 ? 0 : cfg.map_file > 3 ? 3 : cfg.map_file;
    o->nolibs = !cfg.lnk_default_libs || !cfg.lib_stdrt;
    o->nomath = !cfg.lib_math;
    o->wildargs = cfg.lib_wild;
    static char incbuf[260];
    snprintf(incbuf, sizeof incbuf, "%s", cfg.incdir);
    o->nincdir = 0;
    for (char *t = strtok(incbuf, ";:"); t && o->nincdir < 15; t = strtok(NULL, ";:")) {
        if (*t) {
            o->incdirs[o->nincdir++] = t;
        }
    }
    static char defbuf[220];
    snprintf(defbuf, sizeof defbuf, "%s", cfg.defines);
    o->ndefine = 0;
    for (char *t = strtok(defbuf, ";, "); t && o->ndefine < 16; t = strtok(NULL, ";, ")) {
        o->defines[o->ndefine++] = t;
    }
    static char undefbuf[124];
    snprintf(undefbuf, sizeof undefbuf, "%s", cfg.undefs);
    o->nundef = 0;
    for (char *t = strtok(undefbuf, ";, "); t && o->nundef < 16; t = strtok(NULL, ";, ")) {
        o->undefs[o->nundef++] = t;
    }
}

typedef struct {
    const char *cur_file;
    int hdr_added;
} BuildCtx;

static BuildCtx bctx;

/* Writes p relative to the current directory (when it lies below it) into out. */
static void rel_path(const char *p, char *out, int n)
{
    char cwd[512];
    if (getcwd(cwd, sizeof cwd)) {
        size_t l = strlen(cwd);
        if (!strncmp(p, cwd, l) && p[l] == '/') {
            snprintf(out, n, "%s", p + l + 1);
            return;
        }
    }
    snprintf(out, n, "%s", p);
}

/* Compiler callback: turns one diagnostic into an entry of the Message window. */
static void build_diag(void *u, int sev, const char *file, int line, const char *msg)
{
    (void)u;
    char f[256];
    snprintf(f, sizeof f, "%s", file ? file : "");
    int is_user = f[0] && file_exists(f);
    (void)is_user;
    if (sev == FC_WARNING) {
        msg_add(0, f, line, msg, 0);
    } else if (sev == FC_ERROR) {
        msg_add(1, f, line, msg, 0);
    } else if (sev == FC_FATAL) {
        msg_add(2, f, line, msg, 0);
    } else {
        msg_add(3, "", 0, msg, 0);
    }
}

/* Returns the name of the executable the current project or file builds. */
const char *ide_exe_name(char *buf, int n)
{
    char src[512] = "a";
    if (nproj > 0) {
        if (project_name[0]) {
            snprintf(src, sizeof src, "%s", project_name);
        } else {
            snprintf(src, sizeof src, "%s", proj_files[0]);
        }
    } else {
        Win *w = any_edit();
        if (w) {
            snprintf(src, sizeof src, "%s", w->buf->name);
        }
    }
    char *dot = strrchr(src, '.'), *sl = strrchr(src, '/');
    if (dot && (!sl || dot > sl)) {
        *dot = 0;
    }
    char nm[300];
    base_name(src, nm, sizeof nm);
    /* the Output Path of a project item (Project|Local options) wins over Options|Directories */
    for (int i = 0; i < nproj; i++) {
        const char *op = proj_opts[i].out;
        if (!op[0] || proj_opts[i].nolink) {
            continue;
        }
        struct stat st;
        size_t len = strlen(op);
        int is_dir = op[len - 1] == '/' || (!stat(op, &st) && S_ISDIR(st.st_mode));
        if (is_dir) {
            while (len > 1 && op[len - 1] == '/') {
                len--;
            }
            snprintf(buf, n, "%.*s/%s", (int)len, op, nm);
        } else {
            snprintf(buf, n, "%s", op);
        }
        return buf;
    }
    if (cfg.outdir[0]) {
        snprintf(buf, n, "%s/%s", cfg.outdir, nm);
    } else if (sl) {
        snprintf(buf, n, "%s", src);
    } else {
        snprintf(buf, n, "./%s", nm);
    }
    return buf;
}

/* Shows the Compiling status window with the line, error and warning counts of a build. */
static void show_build_status(const char *main_file, int lines, int errors, int warnings, int ok,
                              const char *note)
{
    Dialog d;
    dlg_init(&d, " Compiling ");
    d.w = 58;
    d.h = 13;
    char t[200];
    snprintf(t, sizeof t, "Main file: %s", main_file);
    dlg_static(&d, 3, 1, t);
    dlg_static(&d, 3, 3, "Total");
    char l1[200], l2[200], l3[200];
    snprintf(l1, sizeof l1, "Lines compiled:  %6d", lines);
    dlg_static(&d, 3, 4, l1);
    snprintf(l2, sizeof l2, "Warnings:        %6d", warnings);
    dlg_static(&d, 3, 5, l2);
    snprintf(l3, sizeof l3, "Errors:          %6d", errors);
    dlg_static(&d, 3, 6, l3);
    long avail = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char l[128];
        while (fgets(l, sizeof l, f)) {
            if (sscanf(l, "MemAvailable: %ld", &avail) == 1) {
                break;
            }
        }
        fclose(f);
    }
    char l4[100];
    snprintf(l4, sizeof l4, "Available memory: %ld K", avail);
    dlg_static(&d, 3, 8, l4);
    char l5[200];
    snprintf(l5, sizeof l5, "%s : Press any key", ok ? "Success" : (errors ? "Errors" : "Done"));
    dlg_static(&d, 3, 10, note && *note ? note : l5);
    if (note && *note) {
        dlg_static(&d, 3, 11, l5);
    }
    for (;;) {
        ide_draw();
        /* draw without using dlg_run (no buttons: any key closes) */
        d.x = (SW - d.w) / 2;
        d.y = (SH - d.h) / 2 - 1;
        scr_fill(d.x, d.y, d.w, d.h, ' ', pal[P_DLG_NORM]);
        scr_box(d.x, d.y, d.w, d.h, BOX_DOUBLE, pal[P_DLG_BORDER]);
        scr_str(d.x + (d.w - 11) / 2, d.y, d.title, pal[P_DLG_TITLE]);
        scr_shadow(d.x + d.w, d.y + 1, 2, d.h);
        scr_shadow(d.x + 2, d.y + d.h, d.w, 1);
        for (int i = 0; i < d.n; i++) {
            scr_str(d.x + d.it[i].x, d.y + d.it[i].y, d.it[i].text, pal[P_DLG_NORM]);
        }
        scr_cursor(0, 0, 0);
        scr_flush();
        int k = key_get(500);
        if (k != KEY_IDLE && k != KEY_NONE && k != KEY_RESIZE &&
            !(k == KEY_MOUSE && ms.ev != MS_PRESS)) {
            break;
        }
    }
}

/* Returns the modification time of a file, or 0 if it does not exist. */
static time_t mtime_of(const char *p)
{
    struct stat st;
    return stat(p, &st) ? 0 : st.st_mtime;
}

/* Saves the modified source windows before a build (asking first unless auto-save is on).
 * Returns true if the build must be abandoned. */
static int save_for_build(void)
{
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        if (w->kind != W_EDIT || !w->buf->modified) {
            continue;
        }
        if (nproj > 0) {
            int in = 0;
            for (int k = 0; k < nproj; k++) {
                char *a = realpath(proj_files[k], NULL);
                if (a && !strcmp(a, w->buf->name)) {
                    in = 1;
                }
                free(a);
            }
            if (!in) {
                continue;
            }
        } else if (w != any_edit()) {
            continue;
        }
        if (!w->buf->named) {
            ide_command(cmSaveAs);
            if (!w->buf->named) {
                return 1;
            }
            continue;
        }
        if (!cfg.as_editor) {
            char m[600];
            snprintf(m, sizeof m, "%s has been modified.\nSave it before compiling?", w->title);
            int r = msgbox(" Save ", m, MB_YESNOCANCEL);
            if (r == cmCancel) {
                return 1;
            }
            if (r == cmNo) {
                continue;
            }
        }
        if (buf_save(w->buf, w->buf->name, cfg.backup)) {
            msgbox(" Error ", "Cannot save the file", MB_OK | MB_ERROR);
            return 1;
        }
    }
    return 0;
}

/* Creates the missing directories in front of the last component of a path (an Output Path may name
 * new ones). */
static void make_parent_dirs(const char *path)
{
    char dir[700];
    snprintf(dir, sizeof dir, "%s", path);
    for (char *slash = strchr(dir + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        mkdir(dir, 0777); /* existing directories simply fail, which is fine */
        *slash = '/';
    }
}

/* Adds the command line options of the project items to the build. The compiler keeps pointers to
   the switch texts, so they are copied into storage that outlives the compile. */
static void apply_item_switches(FcOptions *opt)
{
    static char text[64][200];
    int used = 0;
    for (int i = 0; i < nproj; i++) {
        if (proj_opts[i].nolink || !proj_opts[i].cmd[0]) {
            continue;
        }
        char line[sizeof proj_opts[i].cmd];
        snprintf(line, sizeof line, "%s", proj_opts[i].cmd);
        for (char *tok = strtok(line, " \t"); tok && used < 64; tok = strtok(NULL, " \t")) {
            snprintf(text[used], sizeof text[used], "%s", tok);
            if (!fc_apply_switch(opt, text[used])) {
                char warning[300];
                snprintf(warning, sizeof warning, "Unknown option '%s' in the Local Options of %s",
                         tok, proj_files[i]);
                msg_add(0, "", 0, warning, 0);
            }
            used++;
        }
    }
}

/*
 * Compiles and links the program according to mode (BUILD_*), filling the Message window.
 * Returns 0 on success.  BUILD_FOR_RUN is a quiet make that leaves the window closed on success.
 */
int ide_build(int mode)
{
    const char *files[64];
    int nf = 0;
    char *owned[64];
    const char *nodebug_src[64];
    int nnodebug = 0; /* project items without debug information */
    if (nproj > 0) {
        for (int i = 0; i < nproj && nf < 64; i++) {
            const char *p = proj_files[i];
            const char *dot = strrchr(p, '.');
            if (dot && !strcasecmp(dot, ".c") && !proj_opts[i].nolink) {
                files[nf++] = p; /* "Exclude from link" items are skipped */
            }
            if (dot && !strcasecmp(dot, ".c") && !proj_opts[i].nolink && proj_opts[i].nodebug) {
                nodebug_src[nnodebug++] = p;
            }
        }
    } else {
        Win *w = any_edit();
        if (!w) {
            msgbox(" Error ", "Nothing to compile", MB_OK | MB_ERROR);
            return 1;
        }
        if (cur_edit() && cur_edit()->kind == W_EDIT) {
            w = cur_edit();
        }
        if (save_for_build()) {
            return 1;
        }
        if (!w->buf->named) {
            return 1;
        }
        files[nf++] = w->buf->name;
    }
    if (nproj > 0 && save_for_build()) {
        return 1;
    }
    if (!nf) {
        msgbox(" Error ", "No source files in the project", MB_OK | MB_ERROR);
        return 1;
    }
    for (int i = 0; i < nf; i++) {
        char r[600];
        rel_path(files[i], r, sizeof r);
        owned[i] = strdup(r);
        files[i] = owned[i];
    }
    /* the same names as handed to the compiler, so that it can recognise the files */
    static char nodebug_names[16][600];
    for (int i = 0; i < nnodebug && i < 16; i++) {
        rel_path(nodebug_src[i], nodebug_names[i], sizeof nodebug_names[i]);
    }
    char exe[700];
    ide_exe_name(exe, sizeof exe);
    make_parent_dirs(exe);
    char mainfile[300];
    base_name(files[0], mainfile, sizeof mainfile);
    /* up to date? */
    if (mode == BUILD_MAKE || mode == BUILD_FOR_RUN) {
        time_t et = mtime_of(exe);
        int fresh = et != 0;
        for (int i = 0; i < nf && fresh; i++) {
            if (mtime_of(files[i]) > et) {
                fresh = 0;
            }
        }
        if (fresh && cfg.check_autodep) {
            /* also look at headers next to the sources */
            for (int i = 0; i < nf && fresh; i++) {
                FILE *f = fopen(files[i], "r");
                if (!f) {
                    continue;
                }
                char line[400];
                while (fgets(line, sizeof line, f)) {
                    char *q = strstr(line, "#include \"");
                    if (!q) {
                        continue;
                    }
                    q += 10;
                    char *e = strchr(q, '"');
                    if (!e) {
                        continue;
                    }
                    *e = 0;
                    char hp[700];
                    const char *sl = strrchr(files[i], '/');
                    if (sl) {
                        snprintf(hp, sizeof hp, "%.*s/%s", (int)(sl - files[i]), files[i], q);
                    } else {
                        snprintf(hp, sizeof hp, "%s", q);
                    }
                    if (mtime_of(hp) > et) {
                        fresh = 0;
                        break;
                    }
                }
                fclose(f);
            }
        }
        if (fresh) {
            for (int i = 0; i < nf; i++) {
                free(owned[i]);
            }
            if (mode == BUILD_MAKE) {
                show_build_status(mainfile, 0, 0, 0, 1, "Make: the program is up to date.");
            }
            return 0;
        }
    }
    if (!cfg.save_old_msgs) {
        msg_clear();
    }
    Win *mw = win_find_kind(W_MSG);
    char hdr[400];
    snprintf(hdr, sizeof hdr, "%s %s:",
             mode == BUILD_ALL ? "Building" : (mode == BUILD_LINK ? "Linking" : "Compiling"),
             files[0]);
    int first_msg = nmsgs;
    msg_add(5, "", 0, hdr, 1);

    dbgui_reset();
    FcContext cx;
    memset(&cx, 0, sizeof cx);
    ide_fill_options(&cx.opt);
    for (int i = 0; i < nnodebug && i < 16; i++) {
        cx.opt.nodebug_files[cx.opt.nnodebug++] = nodebug_names[i];
    }
    apply_item_switches(&cx.opt);
    cx.diag = build_diag;
    int rc = fc_compile(&cx, files, nf, exe);
    last_lines = cx.lines;
    last_errors = cx.errors;
    last_warnings = cx.warnings;
    for (int i = 0; i < nf; i++) {
        free(owned[i]);
    }

    int ok = rc == 0;
    if (cx.errors || cx.warnings) {
        mw = win_get_special(W_MSG);
        msgsel = first_msg + 1 < nmsgs ? first_msg + 1 : first_msg;
    } else if (nmsgs == first_msg + 1) {
        nmsgs = first_msg; /* nothing to report */
    }
    if (!(ok && mode == BUILD_FOR_RUN && !cx.warnings)) {
        show_build_status(mainfile, cx.lines, cx.errors, cx.warnings, ok, NULL);
    }
    if (!ok) {
        /* jump to the first error */
        for (int i = first_msg; i < nmsgs; i++) {
            if (!msgs[i].header && msgs[i].type >= 1 && msgs[i].type <= 2 && msgs[i].line > 0) {
                msg_goto(i, 0);
                break;
            }
        }
        mw = win_find_kind(W_MSG);
        if (mw) {
            win_activate(mw);
        }
    } else if (cx.warnings && nmsgs > first_msg + 1) {
        mw = win_find_kind(W_MSG);
        if (mw) {
            win_activate(mw);
        }
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* running programs                                                    */
/* ------------------------------------------------------------------ */
static void raw_wait_key(void)
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

/* Shows the program's output screen (Window|User screen) until a key is pressed. */
static void user_screen(void)
{
    term_suspend();
    printf("\0337\033[999;1H\033[7m Press any key to return to Flash C \033[0m\0338");
    fflush(stdout);
    raw_wait_key();
    printf("\033[999;1H\033[2K");
    fflush(stdout);
    term_resume();
}

/* Quotes s for use as a single shell word. */
static void shell_quote(const char *s, char *out, int n)
{
    int j = 0;
    out[j++] = '\'';
    for (; *s && j < n - 6; s++) {
        if (*s == '\'') {
            out[j++] = '\'';
            out[j++] = '\\';
            out[j++] = '\'';
            out[j++] = '\'';
        } else {
            out[j++] = *s;
        }
    }
    out[j++] = '\'';
    out[j] = 0;
}

/* Runs cmd on a pseudo-terminal: the output goes to the user screen as usual and a copy (without
 * escape sequences) is kept for Window|Output.  Keyboard input is forwarded to the program. */
static int run_on_pty(const char *cmd)
{
    struct winsize ws;
    int status = 0;
    if (ioctl(1, TIOCGWINSZ, &ws) != 0) {
        ws.ws_row = 25;
        ws.ws_col = 80;
        ws.ws_xpixel = ws.ws_ypixel = 0;
    }
    int mfd;
    pid_t pid = forkpty(&mfd, NULL, NULL, &ws);
    if (pid < 0) { /* no pty: run directly */
        pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        if (pid > 0) {
            waitpid(pid, &status, 0);
        }
        return status;
    }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    out_clear();
    struct termios o, raw;
    int have_t = tcgetattr(0, &o) == 0;
    if (have_t) {
        raw = o;
        cfmakeraw(&raw);
        tcsetattr(0, TCSAFLUSH, &raw);
    }
    for (;;) {
        struct pollfd pf[2] = {{mfd, POLLIN, 0}, {0, POLLIN, 0}};
        if (poll(pf, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pf[1].revents & POLLIN) {
            char kb[256];
            ssize_t k = read(0, kb, sizeof kb);
            if (k > 0) {
                ssize_t w = write(mfd, kb, k);
                (void)w;
            }
        }
        if (pf[0].revents & (POLLIN | POLLHUP)) {
            char buf[1024];
            ssize_t n = read(mfd, buf, sizeof buf);
            if (n <= 0) {
                break;
            }
            {
                ssize_t w = write(1, buf, n);
                (void)w;
            }
            out_feed(buf, (int)n);
        }
    }
    if (have_t) {
        tcsetattr(0, TCSAFLUSH, &o);
    }
    close(mfd);
    waitpid(pid, &status, 0);
    return status;
}

/* Runs the built program (Run|Run). */
static void run_program(void)
{
    char exe[700], q[1500], cmd[2000];
    ide_exe_name(exe, sizeof exe);
    if (!file_exists(exe)) {
        msgbox(" Error ", "The program has not been built", MB_OK | MB_ERROR);
        return;
    }
    char *abs = realpath(exe, NULL);
    shell_quote(abs ? abs : exe, q, sizeof q);
    free(abs);
    snprintf(cmd, sizeof cmd, "exec %s %s", q, cfg.run_args);
    term_suspend();
    fflush(stdout);
    void (*oldint)(int) = signal(SIGINT, SIG_IGN);
    int status = run_on_pty(cmd);
    signal(SIGINT, oldint);
    if (cfg.pause_after_run) {
        if (WIFEXITED(status)) {
            printf("\n[Program exited with code %d]", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("\n[Program terminated by signal %d]", WTERMSIG(status));
        }
        printf("\nPress any key to return to Flash C . . .");
        fflush(stdout);
        raw_wait_key();
        printf("\n");
    }
    term_resume();
}

/* Starts the user's shell (File|Shell) and returns to the IDE on exit. */
static void run_shell(void)
{
    term_suspend();
    printf("Type exit to return to Flash C . . .\n");
    fflush(stdout);
    const char *sh = getenv("SHELL");
    if (!sh) {
        sh = "/bin/sh";
    }
    void (*oldint)(int) = signal(SIGINT, SIG_IGN);
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        execl(sh, sh, (char *)NULL);
        _exit(127);
    }
    int st;
    if (pid > 0) {
        waitpid(pid, &st, 0);
    }
    signal(SIGINT, oldint);
    term_resume();
}

/* Runs entry k of the Transfer menu. */
static void run_transfer(int k)
{
    if (k < 0 || k >= cfg.nxfer) {
        return;
    }
    TransferItem *t = &cfg.xfer[k];
    Win *e = any_edit();
    char cmd[1200] = "", arg[400] = "";
    char edname[600] = "", name[300] = "";
    if (e) {
        snprintf(edname, sizeof edname, "%s", e->buf->name);
        base_name(e->buf->name, name, sizeof name);
    }
    const char *p = t->cmd;
    char prompt[256] = "";
    if (strstr(p, "$PROMPT")) {
        if (!inputbox(" Transfer ", "~E~nter Program Parameters", prompt, sizeof prompt,
                      "Parameters passed to the program")) {
            return;
        }
    }
    int n = 0;
    while (*p && n < (int)sizeof arg - 2) {
        if (!strncmp(p, "$EDNAME", 7)) {
            n += snprintf(arg + n, sizeof arg - n, "'%s'", edname);
            p += 7;
        } else if (!strncmp(p, "$NAME", 5)) {
            n += snprintf(arg + n, sizeof arg - n, "%s", name);
            p += 5;
        } else if (!strncmp(p, "$PROMPT", 7)) {
            n += snprintf(arg + n, sizeof arg - n, "%s", prompt);
            p += 7;
        } else if (*p == '$') {
            while (*p && *p != ' ') {
                p++;
            }
        } else {
            arg[n++] = *p++;
        }
    }
    arg[n] = 0;
    snprintf(cmd, sizeof cmd, "%s %s 2>&1", t->path, arg);
    if (!cfg.save_old_msgs) {
        msg_clear();
    }
    out_clear();
    FILE *f = popen(cmd, "r");
    if (!f) {
        msgbox(" Error ", "Cannot start the program", MB_OK | MB_ERROR);
        return;
    }
    char line[600];
    int nm = 0;
    char title[100];
    int tl = 0;
    for (const char *s = t->title; *s; s++) {
        if (*s != '~') {
            title[tl++] = *s;
        }
    }
    title[tl] = 0;
    char hdr[200];
    snprintf(hdr, sizeof hdr, "%s:", title);
    msg_add(5, "", 0, hdr, 1);
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = 0;
        }
        out_add(line);
        char *c1 = strchr(line, ':');
        if (c1) {
            *c1 = 0;
            char *c2 = strchr(c1 + 1, ':');
            if (c2 && isdigit((unsigned char)c1[1])) {
                *c2 = 0;
                msg_add(4, line, atoi(c1 + 1), c2 + 1, 0);
                nm++;
                continue;
            }
            *c1 = ':';
        }
        msg_add(5, "", 0, line, 1);
    }
    pclose(f);
    win_get_special(W_MSG);
    msgsel = nmsgs > 1 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */
static int save_window(Win *w)
{
    if (!w->buf->named) {
        char path[512] = "";
        snprintf(path, sizeof path, "%s", w->buf->name);
        int r = file_dialog(" Save File As ", "*.c", path, sizeof path, 1);
        if (!r) {
            return 1;
        }
        if (!strrchr(path, '.')) {
            strncat(path, cfg.defext, sizeof path - strlen(path) - 1);
        }
        if (buf_save(w->buf, path, 0)) {
            msgbox(" Error ", "Cannot create the file", MB_OK | MB_ERROR);
            return 1;
        }
        char *rp = realpath(path, NULL);
        if (rp) {
            snprintf(w->buf->name, sizeof w->buf->name, "%s", rp);
            free(rp);
        }
        win_set_title(w);
        return 0;
    }
    if (buf_save(w->buf, w->buf->name, cfg.backup)) {
        msgbox(" Error ", "Cannot write the file", MB_OK | MB_ERROR);
        return 1;
    }
    return 0;
}

static int maybe_save(Win *w) /* returns 1 if the operation should be cancelled */
{
    if (w->kind != W_EDIT || !w->buf->modified) {
        return 0;
    }
    char m[700];
    snprintf(m, sizeof m, "%s\nhas been modified. Save?", w->title);
    int r = msgbox(" Save ", m, MB_YESNOCANCEL);
    if (r == cmCancel) {
        return 1;
    }
    if (r == cmYes) {
        return save_window(w);
    }
    return 0;
}

/* Closes a window, offering to save an editor window that was changed. */
static void close_window(Win *w)
{
    if (maybe_save(w)) {
        return;
    }
    win_close(w);
}

/* ------------------------------------------------------------------ */
/* projects & desktop                                                  */
/* ------------------------------------------------------------------ */
/* ---- project notes: the text lives in the project file (a [notes] section, every line prefixed
   with |); without a project it lives in the desktop file. notes_text is the saved copy, the notes
   window the working copy. ---- */
static int notes_version = -1; /* version of the notes buffer that notes_text was taken from */

/* Writes the project notes as a [notes] section. */
static void notes_put(FILE *f)
{
    if (!notes_text || !*notes_text) {
        return;
    }
    fprintf(f, "[notes]\n");
    for (const char *line = notes_text;;) {
        const char *end = strchr(line, '\n');
        fprintf(f, "|%.*s\n", end ? (int)(end - line) : (int)strlen(line), line);
        if (!end) {
            break;
        }
        line = end + 1;
    }
}

/* one line of a [notes] section; returns 0 when the line does not belong to it */
static int notes_get(const char *line, char **text)
{
    if (line[0] != '|') {
        return 0;
    }
    int first = *text == NULL; /* lines are joined with a new-line, none before the first */
    size_t old = first ? 0 : strlen(*text), add = strlen(line + 1);
    *text = realloc(*text, old + add + 2);
    if (!first) {
        (*text)[old++] = '\n';
    }
    memcpy(*text + old, line + 1, add + 1);
    return 1;
}

/* the notes window shows the stored text (called when it opens and when another project is loaded)
 */
static void notes_show(Win *w)
{
    const char *t = notes_text ? notes_text : "";
    buf_set_data(w->buf, t, (long)strlen(t));
    notes_version = w->buf->version;
    w->cur_col = w->cur_line = w->top = 0;
}

/* Saves the notes as soon as they were changed (typing, pasting, undo): in the project file, or in
   the desktop file when no project is open. Called once per pass of the main loop. */
static void project_save(void);
static void desktop_save(void);

/* Keeps notes_text and the project/desktop file up to date while the notes are edited. */
static void notes_autosave(void)
{
    Win *w = win_find_kind(W_NOTES);
    if (!w || w->buf->version == notes_version) {
        return;
    }
    notes_version = w->buf->version;
    free(notes_text);
    notes_text = buf_all(w->buf, NULL);
    if (project_name[0]) {
        project_save();
    } else {
        desktop_save();
    }
}

/* Closes the current project and frees its items. */
static void project_free(void)
{
    for (int i = 0; i < nproj; i++) {
        free(proj_files[i]);
    }
    free(notes_text);
    notes_text = NULL;
    free(proj_files);
    proj_files = NULL;
    free(proj_opts);
    proj_opts = NULL;
    nproj = 0;
    proj_sel = 0;
    project_name[0] = 0;
}

/* Writes the project file. */
static void project_save(void)
{
    if (!project_name[0]) {
        return;
    }
    FILE *f = fopen(project_name, "w");
    if (!f) {
        return;
    }
    fprintf(f, "# Flash C project\n");
    for (int i = 0; i < nproj; i++) {
        ProjItem *it = &proj_opts[i];
        fprintf(f, "%s\n", proj_files[i]);
        /* the settings of an item follow its name, indented */
        if (it->cmd[0]) {
            fprintf(f, "\tcmd=%s\n", it->cmd);
        }
        if (it->out[0]) {
            fprintf(f, "\tout=%s\n", it->out);
        }
        if (it->nodebug) {
            fprintf(f, "\tnodebug=1\n");
        }
        if (it->nolink) {
            fprintf(f, "\tnolink=1\n");
        }
    }
    notes_put(f);
    fclose(f);
    project_dirty = 0;
}

/* Adds a source file to the project. */
static void project_add(const char *p)
{
    for (int i = 0; i < nproj; i++) {
        if (!strcmp(proj_files[i], p)) {
            return;
        }
    }
    proj_files = realloc(proj_files, (nproj + 1) * sizeof(char *));
    proj_opts = realloc(proj_opts, (nproj + 1) * sizeof *proj_opts);
    memset(&proj_opts[nproj], 0, sizeof *proj_opts);
    proj_files[nproj++] = strdup(p);
    project_dirty = 1;
}

/* Opens a project file. */
static void project_open(const char *path)
{
    project_free();
    char *rp = realpath(path, NULL);
    snprintf(project_name, sizeof project_name, "%s", rp ? rp : path);
    free(rp);
    FILE *f = fopen(project_name, "r");
    if (f) {
        char *line = NULL;
        size_t cap = 0;
        int in_notes = 0;
        while (getline(&line, &cap, f) >= 0) { /* notes lines can be longer than any fixed buffer */
            char *nl = strchr(line, '\n');
            if (nl) {
                *nl = 0;
            }
            if (!strcmp(line, "[notes]")) {
                in_notes = 1;
                continue;
            }
            if (in_notes) {
                notes_get(line, &notes_text);
                continue;
            }
            if (!line[0] || line[0] == '#') {
                continue;
            }
            if (line[0] == '\t' || line[0] == ' ') { /* a setting of the item above */
                if (!nproj) {
                    continue;
                }
                ProjItem *it = &proj_opts[nproj - 1];
                const char *kv = line + strspn(line, " \t");
                if (!strncmp(kv, "cmd=", 4)) {
                    snprintf(it->cmd, sizeof it->cmd, "%s", kv + 4);
                } else if (!strncmp(kv, "out=", 4)) {
                    snprintf(it->out, sizeof it->out, "%s", kv + 4);
                } else if (!strcmp(kv, "nodebug=1")) {
                    it->nodebug = 1;
                } else if (!strcmp(kv, "nolink=1")) {
                    it->nolink = 1;
                }
                continue;
            }
            project_add(line);
        }
        free(line);
        fclose(f);
    }
    if (!notes_text) { /* notes of older versions were kept in <project>.notes */
        char side[700];
        snprintf(side, sizeof side, "%s.notes", project_name);
        FILE *nf = file_exists(side) ? fopen(side, "r") : NULL;
        if (nf) {
            char buf[2000];
            while (fgets(buf, sizeof buf, nf)) {
                char *nl = strchr(buf, '\n');
                if (nl) {
                    *nl = 0;
                }
                char *prefixed = malloc(strlen(buf) + 2);
                prefixed[0] = '|';
                strcpy(prefixed + 1, buf);
                notes_get(prefixed, &notes_text);
                free(prefixed);
            }
            fclose(nf);
        }
    }
    {
        Win *nw = win_find_kind(W_NOTES);
        if (nw) {
            notes_show(nw);
        }
    } /* an open notes window follows the project */
    project_dirty = 0;
    snprintf(cfg.last_project, sizeof cfg.last_project, "%s", project_name);
    Win *w = win_get_special(W_PROJ);
    if (w) {
        char t[300];
        char b[260];
        base_name(project_name, b, sizeof b);
        snprintf(t, sizeof t, "Project: %s", b);
        snprintf(w->title, sizeof w->title, "%s", t);
    }
}

/* Saves the window layout and history for the next session. */
static void desktop_save(void)
{
    FILE *f = fopen("fcdef.dsk", "w");
    if (!f) {
        return;
    }
    fprintf(f, "# Flash C desktop\n");
    if (project_name[0]) {
        fprintf(f, "project=%s\n", project_name);
    }
    if (cfg.sv_open) {
        for (int i = 0; i < nwins; i++) {
            Win *w = wins[i];
            if (w->kind == W_EDIT && w->buf->named && !strncmp(w->buf->name, "/", 1)) {
                fprintf(f, "file=%d %d %d %d %d %d %s\n", w->cur_line, w->cur_col, w->top, w->x,
                        w->w, w->h, w->buf->name);
            }
        }
    }
    if (cfg.sv_watch) {
        for (int i = 0; i < nwatch; i++) {
            fprintf(f, "watch=%s\n", watches[i]);
        }
    }
    if (cfg.sv_bp) {
        dbgui_save_bps(f);
    }
    fprintf(f, "run_args=%s\n", cfg.run_args);
    if (!project_name[0]) {
        notes_put(f); /* with a project the notes are in the project file */
    }
    fclose(f);
}

/* Restores the saved desktop. */
static void desktop_load(void)
{
    FILE *f = fopen("fcdef.dsk", "r");
    if (!f) {
        return;
    }
    char *line = NULL;
    size_t cap = 0;
    int in_notes = 0;
    while (getline(&line, &cap, f) >= 0) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = 0;
        }
        if (!strcmp(line, "[notes]")) {
            in_notes = 1;
            continue;
        }
        if (in_notes) {
            if (!project_name[0]) {
                notes_get(line, &notes_text);
            }
            continue;
        }
        if (!strncmp(line, "project=", 8)) {
            project_open(line + 8);
        } else if (!strncmp(line, "watch=", 6)) {
            watches = realloc(watches, (nwatch + 1) * sizeof(char *));
            watches[nwatch++] = strdup(line + 6);
        } else if (!strncmp(line, "bp=", 3)) {
            dbgui_load_bp(line + 3);
        } else if (!strncmp(line, "run_args=", 9)) {
            snprintf(cfg.run_args, sizeof cfg.run_args, "%s", line + 9);
        } else if (!strncmp(line, "file=", 5)) {
            int cy, cx, top, x, w, h, off;
            if (sscanf(line + 5, "%d %d %d %d %d %d %n", &cy, &cx, &top, &x, &w, &h, &off) >= 6) {
                const char *path = line + 5 + off;
                if (!file_exists(path)) {
                    continue;
                }
                Win *win = win_open_file(path);
                if (win) {
                    ed_goto(win, cy, cx);
                    win->top = top;
                    ed_ensure_visible(win);
                }
            }
        }
    }
    free(line);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* editing helpers                                                     */
/* ------------------------------------------------------------------ */
static void word_at_cursor(Win *w, char *out, int n)
{
    out[0] = 0;
    if (!w || (w->kind != W_EDIT && w->kind != W_CLIP && w->kind != W_NOTES)) {
        return;
    }
    Line *L = &w->buf->lines[w->cur_line];
    int s = w->cur_col, e = w->cur_col;
    while (s > 0 && (isalnum((unsigned char)L->text[s - 1]) || L->text[s - 1] == '_')) {
        s--;
    }
    while (e < L->len && (isalnum((unsigned char)L->text[e]) || L->text[e] == '_')) {
        e++;
    }
    int l = e - s;
    if (l >= n) {
        l = n - 1;
    }
    memcpy(out, L->text + s, l);
    out[l] = 0;
}

/* Replaces the current match; with prompt set the user is asked first. */
static int do_replace_one(Win *w, int prompt)
{
    /* w has the match selected */
    if (prompt) {
        int r = msgbox(" Replace Prompt ", "Replace this occurrence?", MB_YESNOCANCEL);
        if (r == cmCancel) {
            return -1;
        }
        if (r == cmNo) {
            w->sel_active = 0;
            return 0;
        }
    }
    int l1, c1, l2, c2;
    ed_block_range(w, &l1, &c1, &l2, &c2);
    undo_cursor_line = w->cur_line;
    undo_cursor_col = w->cur_col;
    buf_group(w->buf);
    buf_delete(w->buf, l1, c1, l2, c2);
    int l = l1, c = c1;
    buf_insert(w->buf, &l, &c, repl_text, -1);
    w->sel_active = 0;
    w->block_active = 0;
    ed_goto(w, l, c);
    return 1;
}

/* Runs Search|Find (replace = 0) or Search|Replace (replace = 1). */
static void do_find(int replace)
{
    Win *w = cur_edit();
    if (!w) {
        return;
    }
    char wd[256];
    word_at_cursor(w, wd, sizeof wd);
    if (!find_text[0] && wd[0]) {
        snprintf(find_text, sizeof find_text, "%s", wd);
    }
    int r = dlg_find_text(replace);
    if (r != cmOK && r != cmChangeAll) {
        return;
    }
    last_was_replace = replace;
    w = cur_edit();
    if (!w) {
        return;
    }
    if (!replace) {
        if (!ed_find_next(w, -1)) {
            msgbox(" Search ", "Search string not found", MB_OK);
        }
        return;
    }
    int count = 0;
    int saved_origin = find_opts[6];
    if (r == cmChangeAll || find_opts[3] == 0) {
        int all = r == cmChangeAll;
        for (;;) {
            if (!ed_find_next(w, -1)) {
                if (!count) {
                    msgbox(" Search ", "Search string not found", MB_OK);
                }
                break;
            }
            find_opts[6] = 0;
            int q = do_replace_one(w, find_opts[3] && all ? 1 : 0);
            if (q < 0) {
                break;
            }
            if (q > 0) {
                count++;
            }
            if (!all) {
                break;
            }
        }
    } else {
        for (;;) {
            if (!ed_find_next(w, -1)) {
                if (!count) {
                    msgbox(" Search ", "Search string not found", MB_OK);
                }
                break;
            }
            find_opts[6] = 0;
            ide_draw();
            int q = do_replace_one(w, 1);
            if (q < 0) {
                break;
            }
            if (q > 0) {
                count++;
            }
            break;
        }
    }
    find_opts[6] = saved_origin;
}

/* Runs Search|Locate function. */
static void do_locate(void)
{
    Win *w = cur_edit();
    if (!w) {
        return;
    }
    static char name[128];
    if (!dlg_locate(name, sizeof name)) {
        return;
    }
    size_t nl = strlen(name);
    for (int l = 0; l < w->buf->n; l++) {
        Line *L = &w->buf->lines[l];
        for (int i = 0; i + (int)nl < L->len; i++) {
            if (strncmp(L->text + i, name, nl)) {
                continue;
            }
            if (i > 0 && (isalnum((unsigned char)L->text[i - 1]) || L->text[i - 1] == '_')) {
                continue;
            }
            int j = i + (int)nl;
            while (j < L->len && L->text[j] == ' ') {
                j++;
            }
            if (j < L->len && L->text[j] == '(' &&
                (i == 0 || L->text[i - 1] == ' ' || L->text[i - 1] == '*' ||
                 L->text[i - 1] == '\t')) {
                int k = L->len - 1;
                while (k >= 0 && (L->text[k] == ' ' || L->text[k] == '\t')) {
                    k--;
                }
                if (k >= 0 && L->text[k] == ';') {
                    continue;
                }
                ed_goto(w, l, i);
                return;
            }
        }
    }
    msgbox(" Locate Function ", "Function not found", MB_OK);
}

/* Interactive Window|Size/Move: arrow keys move, Shift+arrows resize, Enter accepts. */
static void size_move(void)
{
    Win *w = win_active();
    if (!w) {
        return;
    }
    int ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    for (;;) {
        ide_draw();
        status_hint("Arrow keys move, Shift+arrows resize the window; Enter accepts, Esc cancels");
        scr_flush();
        int k = key_get(1000);
        int b = k & KMASK, shift = k & K_SHIFT;
        if (k == KEY_ENTER) {
            break;
        }
        if (k == KEY_ESC) {
            w->x = ox;
            w->y = oy;
            w->w = ow;
            w->h = oh;
            break;
        }
        int dx = b == KEY_LEFT    ? -1
                 : b == KEY_RIGHT ? 1
                                  : 0,
            dy = b == KEY_UP     ? -1
                 : b == KEY_DOWN ? 1
                                 : 0;
        if (!dx && !dy) {
            continue;
        }
        w->zoomed = 0;
        if (shift) {
            if (w->w + dx >= 16 && w->x + w->w + dx <= SW) {
                w->w += dx;
            }
            if (w->h + dy >= 5 && w->y + w->h + dy <= SH - 1) {
                w->h += dy;
            }
        } else {
            w->x += dx;
            w->y += dy;
            if (w->y < 1) {
                w->y = 1;
            }
            if (w->y + w->h > SH - 1) {
                w->y = SH - 1 - w->h;
            }
        }
    }
    status_hint(NULL);
}

/* Opens the special window of the given kind, or brings it to the front. */
static void toggle_special(int kind)
{
    Win *w = win_find_kind(kind);
    if (w && win_active() == w) {
        win_close(w);
        return;
    }
    win_get_special(kind);
}

/* Asks for a watch expression and adds it (or edits the selected one when edit is set). */
static void add_watch(int edit)
{
    char buf[200] = "";
    Win *e = cur_edit();
    if (edit) {
        win_get_special(W_WATCH);
        Win *w = win_find_kind(W_WATCH);
        if (!nwatch || !w) {
            return;
        }
        snprintf(buf, sizeof buf, "%s", watches[w->sel]);
    } else if (e) {
        word_at_cursor(e, buf, sizeof buf);
    }
    if (!inputbox(edit ? " Edit Watch " : " Add Watch ", "~W~atch Expression", buf, sizeof buf,
                  "Enter expression to add as watch")) {
        return;
    }
    if (!buf[0]) {
        return;
    }
    Win *w = win_get_special(W_WATCH);
    if (edit) {
        free(watches[w->sel]);
        watches[w->sel] = strdup(buf);
    } else {
        watches = realloc(watches, (nwatch + 1) * sizeof(char *));
        watches[nwatch++] = strdup(buf);
        w->sel = nwatch - 1;
    }
}

/* Quits after offering to save the modified files; Cancel keeps the IDE running. */
static void quit_ide(void)
{
    for (int i = 0; i < nwins; i++) {
        Win *win = wins[i];
        if (win->kind != W_EDIT || !win->buf->modified) {
            continue;
        }
        win_activate(win);
        if (maybe_save(win)) {
            return; /* the user cancelled */
        }
        win->buf->modified = 0; /* saved, or deliberately discarded */
        i = -1;                 /* activating a window reorders the list: start over */
    }
    if (cfg.as_env) {
        cfg_save();
    }
    if (cfg.as_desktop) {
        desktop_save();
    }
    if (cfg.as_project && project_name[0]) {
        project_save();
    }
    quit_flag = 1;
}

/* ------------------------------------------------------------------ */
/* command dispatch                                                    */
/* ------------------------------------------------------------------ */
void ide_command(int cmd)
{
    Win *e = cur_edit();
    if (cmd >= cmWinNum1 && cmd < cmWinNum1 + 10) {
        Win *w = win_by_num(cmd - cmWinNum1);
        if (w) {
            win_activate(w);
        }
        return;
    }
    if (cmd >= cmTransfer0 && cmd < cmTransfer0 + 12) {
        run_transfer(cmd - cmTransfer0);
        return;
    }
    if (cmd >= cmOptCodeGen && cmd <= cmOptSave) {
        opt_command(cmd);
        return;
    }
    if (!cmd_enabled(cmd)) {
        return;
    }
    switch (cmd) {
    case cmAbout:
        dlg_about();
        break;
    case cmClearDesktop:
        win_close_all();
        break;
    case cmRepaint:
        scr_invalidate();
        break;
    case cmNewFile:
        win_new_edit(NULL);
        break;
    case cmOpenFile: {
        char path[512] = "*.c";
        int r = file_dialog(" Open a File ", "*.c", path, sizeof path, 0);
        if (r == 1) {
            win_open_file(path);
        } else if (r == 2 && e && e->kind == W_EDIT) {
            if (!maybe_save(e)) {
                if (buf_load(e->buf, path)) {
                    msgbox(" Error ", "Cannot read the file", MB_OK | MB_ERROR);
                }
                char *rp = realpath(path, NULL);
                if (rp) {
                    snprintf(e->buf->name, sizeof e->buf->name, "%s", rp);
                    free(rp);
                }
                e->cur_col = e->cur_line = e->top = e->left = 0;
                win_set_title(e);
            }
        } else if (r == 2) {
            win_open_file(path);
        }
        break;
    }
    case cmSaveFile:
        if (e && e->kind == W_EDIT) {
            save_window(e);
        }
        break;
    case cmSaveAs:
        if (e && e->kind == W_EDIT) {
            int named = e->buf->named;
            e->buf->named = 0;
            char old[512];
            snprintf(old, sizeof old, "%s", e->buf->name);
            if (save_window(e)) {
                e->buf->named = named;
                snprintf(e->buf->name, sizeof e->buf->name, "%s", old);
            }
        }
        break;
    case cmSaveAll:
        for (int i = 0; i < nwins; i++) {
            if (wins[i]->kind == W_EDIT && wins[i]->buf->modified) {
                save_window(wins[i]);
            }
        }
        break;
    case cmChangeDir: {
        char cwd[512] = "";
        if (!getcwd(cwd, sizeof cwd)) {
            cwd[0] = 0;
        }
        if (inputbox(" Change Directory ", "~D~irectory Name", cwd, sizeof cwd,
                     "Enter the directory path")) {
            if (chdir(cwd)) {
                msgbox(" Error ", "Invalid directory", MB_OK | MB_ERROR);
            }
        }
        break;
    }
    case cmPrint:
        if (e) {
            FILE *p = popen("lpr 2>/dev/null", "w");
            if (!p) {
                msgbox(" Print ", "No print command available", MB_OK);
                break;
            }
            char *t = buf_all(e->buf, NULL);
            fputs(t, p);
            free(t);
            if (pclose(p)) {
                msgbox(" Print ", "The print command failed", MB_OK);
            }
            break;
        }
    case cmShell:
        run_shell();
        break;
    case cmQuit:
        quit_ide();
        break;
    case cmUndo:
        if (e) {
            int l = e->cur_line, c = e->cur_col;
            if (buf_undo(e->buf, &l, &c)) {
                e->sel_active = 0;
                e->block_active = 0;
                ed_goto(e, l, c);
            }
        }
        break;
    case cmRedo:
        if (e) {
            int l = e->cur_line, c = e->cur_col;
            if (buf_redo(e->buf, &l, &c)) {
                e->sel_active = 0;
                ed_goto(e, l, c);
            }
        }
        break;
    case cmCut:
        if (e) {
            ed_copy(e, 1);
        }
        break;
    case cmCopy: {
        Win *a = win_active();
        if (a && a->kind == W_HELP) {
            help_copy(a);
        } else if (e) {
            ed_copy(e, 0);
        }
        break;
    }
    case cmPaste:
        if (e) {
            ed_paste(e);
        }
        break;
    case cmClear:
        if (e) {
            buf_group(e->buf);
            ed_clear_block(e);
        }
        break;
    case cmCopyExample: {
        char *t = help_example_text();
        if (t) {
            clip_set_text(t);
            free(t);
        }
        break;
    }
    case cmShowClip: {
        Win *w = win_find_kind(W_CLIP);
        if (!w) {
            if (nwins >= MAXWIN) {
                break;
            }
            w = calloc(1, sizeof *w);
            w->kind = W_CLIP;
            w->x = 4;
            w->y = 3;
            w->w = SW - 12;
            w->h = SH - 10;
            w->buf = clipbuf;
            w->error_line = -1;
            w->insert = 1;
            snprintf(w->title, sizeof w->title, "Clipboard");
            wins[nwins++] = w;
        } else {
            win_activate(w);
        }
        break;
    }
    case cmFind:
        do_find(0);
        break;
    case cmReplaceText:
        do_find(1);
        break;
    case cmSearchAgain:
        if (e) {
            if (last_was_replace && find_opts[3] == 0) { /* repeat the replacement */
                if (ed_find_next(e, -1)) {
                    do_replace_one(e, 0);
                } else {
                    msgbox(" Search ", "Search string not found", MB_OK);
                }
            } else if (!ed_find_next(e, -1)) {
                msgbox(" Search ", "Search string not found", MB_OK);
            }
        }
        break;
    case cmGotoLine:
        if (e) {
            int l = e->cur_line + 1;
            if (dlg_goto_line(&l)) {
                ed_goto(e, l - 1, 0);
            }
        }
        break;
    case cmLocateFn:
        do_locate();
        break;
    case cmPrevError:
        goto_error(-1);
        break;
    case cmNextError:
        goto_error(1);
        break;
    case cmRun:
        if (dbgui_wants_run()) {
            dbgui_go();
        } else if (!ide_build(BUILD_FOR_RUN)) {
            run_program();
        }
        break;
    case cmProgReset:
        dbgui_reset();
        break;
    case cmGotoCursor:
        dbgui_goto_cursor();
        break;
    case cmTraceInto:
        dbgui_step(1);
        break;
    case cmStepOver:
        dbgui_step(0);
        break;
    case cmArguments: {
        char a[256];
        snprintf(a, sizeof a, "%s", cfg.run_args);
        if (inputbox(" Program Arguments ", "~E~nter Program Parameters", a, sizeof a,
                     "Enter command-line arguments to be passed to your program")) {
            snprintf(cfg.run_args, sizeof cfg.run_args, "%s", a);
        }
        break;
    }
    case cmCompile:
        ide_build(BUILD_COMPILE);
        break;
    case cmMake:
        ide_build(BUILD_MAKE);
        break;
    case cmLink:
        ide_build(BUILD_LINK);
        break;
    case cmBuildAll:
        ide_build(BUILD_ALL);
        break;
    case cmInformation:
        dlg_compiler_info(last_lines, last_errors, last_warnings);
        break;
    case cmRemoveMsgs:
        msg_clear();
        {
            Win *m = win_find_kind(W_MSG);
            if (m) {
                win_close(m);
            }
        }
        break;
    case cmInspect:
        dbgui_inspect();
        break;
    case cmCallStack:
        dbgui_call_stack();
        break;
    case cmToggleBP:
        dbgui_toggle_bp();
        break;
    case cmBreakpoints:
        dbgui_breakpoints();
        break;
    case cmEvalMod:
        dlg_eval();
        break;
    case cmAddWatch:
        add_watch(0);
        break;
    case cmEditWatch:
        add_watch(1);
        break;
    case cmDelWatch: {
        Win *w = win_find_kind(W_WATCH);
        int i = w ? w->sel : 0;
        if (nwatch && i < nwatch) {
            free(watches[i]);
            memmove(&watches[i], &watches[i + 1], (nwatch - i - 1) * sizeof(char *));
            nwatch--;
        }
        break;
    }
    case cmRemoveWatches:
        for (int i = 0; i < nwatch; i++) {
            free(watches[i]);
        }
        nwatch = 0;
        break;
    case cmOpenProject: {
        char path[512] = "*.fcp";
        int r = file_dialog(" Open Project File ", "*.fcp", path, sizeof path, 0);
        if (r) {
            if (!strrchr(path, '.')) {
                strncat(path, ".fcp", sizeof path - strlen(path) - 1);
            }
            if (project_dirty) {
                project_save();
            }
            project_open(path);
            if (!file_exists(path)) {
                project_save();
            }
        }
        break;
    }
    case cmCloseProject:
        notes_autosave();
        if (project_dirty) {
            project_save();
        }
        project_free();
        {
            Win *w = win_find_kind(W_PROJ);
            if (w) {
                win_close(w);
            }
            w = win_find_kind(W_NOTES);
            if (w) {
                win_close(w);
            }
        }
        break;
    case cmAddItem: {
        char path[512] = "*.c";
        int r = file_dialog(" Add to Project List ", "*.c;*.h", path, sizeof path, 0);
        if (r) {
            char *rp = realpath(path, NULL);
            if (rp) {
                char rel[600];
                rel_path(rp, rel, sizeof rel);
                project_add(rel);
                free(rp);
                project_save();
            } else {
                msgbox(" Error ", "File not found", MB_OK | MB_ERROR);
            }
        }
        break;
    }
    case cmDelItem:
        if (nproj) {
            free(proj_files[proj_sel]);
            memmove(&proj_files[proj_sel], &proj_files[proj_sel + 1],
                    (nproj - proj_sel - 1) * sizeof(char *));
            memmove(&proj_opts[proj_sel], &proj_opts[proj_sel + 1],
                    (nproj - proj_sel - 1) * sizeof *proj_opts);
            nproj--;
            if (proj_sel >= nproj) {
                proj_sel = nproj ? nproj - 1 : 0;
            }
            project_save();
        }
        break;
    case cmLocalOpts:
        if (nproj && dlg_local_options(proj_files[proj_sel], &proj_opts[proj_sel])) {
            project_save();
        }
        break;
    case cmIncludeFiles:
        if (nproj) {
            dlg_include_files(proj_files[proj_sel]);
        }
        break;
    case cmSizeMove:
        size_move();
        break;
    case cmZoom:
        if (win_active()) {
            win_zoom(win_active());
        }
        break;
    case cmTile:
        win_tile();
        break;
    case cmCascade:
        win_cascade();
        break;
    case cmNextWin:
        if (nwins > 1) {
            Win *w = wins[0];
            win_activate(w);
        }
        break;
    case cmCloseWin:
        if (win_active()) {
            Win *w = win_active();
            if (w->kind == W_EDIT) {
                close_window(w);
            } else {
                win_close(w);
            }
        }
        break;
    case cmCloseAll:
        while (nwins) {
            Win *w = wins[nwins - 1];
            if (w->kind == W_EDIT && maybe_save(w)) {
                break;
            }
            win_close(w);
        }
        break;
    case cmWinMessage:
        toggle_special(W_MSG);
        break;
    case cmWinOutput:
        toggle_special(W_OUT);
        break;
    case cmWinWatch:
        toggle_special(W_WATCH);
        break;
    case cmWinRegister:
        toggle_special(W_REG);
        break;
    case cmWinProject:
        toggle_special(W_PROJ);
        break;
    case cmWinNotes: {
        Win *w = win_find_kind(W_NOTES);
        if (!w && nwins < MAXWIN) {
            w = calloc(1, sizeof *w);
            w->kind = W_NOTES;
            w->x = 6;
            w->y = 3;
            w->w = SW - 16;
            w->h = SH - 9;
            w->buf = buf_new();
            w->error_line = -1;
            w->insert = 1;
            snprintf(w->title, sizeof w->title, "Project notes");
            wins[nwins++] = w;
            notes_show(w);
        } else if (w) {
            win_activate(w);
        }
        break;
    }
    case cmUserScreen:
        user_screen();
        break;
    case cmWinList: {
        int action;
        int idx = dlg_window_list(&action);
        if (idx >= 0) {
            Win *w = wins[idx];
            if (action) {
                if (w->kind == W_EDIT) {
                    win_activate(w);
                    close_window(w);
                } else {
                    win_close(w);
                }
            } else {
                win_activate(w);
            }
        }
        break;
    }
    case cmHelpContents:
        help_contents();
        break;
    case cmHelpIndex:
        help_index();
        break;
    case cmHelpTopic: {
        char wd[100];
        word_at_cursor(e, wd, sizeof wd);
        help_topic_search(wd);
        break;
    }
    case cmHelpPrev:
        help_prev();
        break;
    case cmHelpOnHelp:
        help_show("Using the IDE");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* keyboard                                                            */
/* ------------------------------------------------------------------ */
static int key_to_cmd(int key)
{
    int b = key & KMASK, shift = key & K_SHIFT, ctrl = key & K_CTRL, alt = key & K_ALT;
    if (b >= KEY_F1 && b <= KEY_F12) {
        int f = b - KEY_F1 + 1;
        if (alt && !ctrl && !shift) {
            switch (f) {
            case 1:
                return cmHelpPrev;
            case 3:
                return cmCloseWin;
            case 4:
                return cmInspect;
            case 5:
                return cmUserScreen;
            case 7:
                return cmPrevError;
            case 8:
                return cmNextError;
            case 9:
                return cmCompile;
            }
        }
        if (ctrl && !alt && !shift) {
            switch (f) {
            case 1:
                return cmHelpTopic;
            case 2:
                return cmProgReset;
            case 3:
                return cmCallStack;
            case 4:
                return cmEvalMod;
            case 5:
                return cmSizeMove;
            case 7:
                return cmAddWatch;
            case 8:
                return cmToggleBP;
            case 9:
                return cmRun;
            }
        }
        if (shift && !ctrl && !alt) {
            switch (f) {
            case 1:
                return cmHelpIndex;
            case 5:
                return cmCascade;
            case 4:
                return cmCascade;
            }
        }
        if (!shift && !ctrl && !alt) {
            switch (f) {
            case 1:
                return cmHelp_;
            case 2:
                return cmSaveFile;
            case 3:
                return cmOpenFile;
            case 4:
                return cmGotoCursor;
            case 5:
                return cmZoom;
            case 6:
                return cmNextWin;
            case 7:
                return cmTraceInto;
            case 8:
                return cmStepOver;
            case 9:
                return cmMake;
            case 10:
                return cmMenu;
            }
        }
        return 0;
    }
    if (alt && !ctrl) {
        if (b == 'x') {
            return cmQuit;
        }
        if (b == '0') {
            return cmWinList;
        }
        if (b >= '1' && b <= '9') {
            return cmWinNum1 + (b - '0');
        }
    }
    if (ctrl && !alt && !shift && b == 'l' && cur_edit()) {
        return cmSearchAgain;
    }
    return 0;
}

/* Returns the next key, taking queued injected keys first. */
static int next_key(void)
{
    if (nkq) {
        int k = key_queue[0];
        memmove(key_queue, key_queue + 1, --nkq * sizeof(int));
        return k;
    }
    return key_get(1000);
}

/*
 * Main loop of the IDE: initialises the screen and configuration, opens the files named on
 * the command line and processes keys until the user quits.
 */
void ide_run(int argc, char **argv)
{
    cfg_defaults();
    pal_defaults();
    dbgui_init();
    cfg_load();
    clipbuf = buf_new();
    find_opts[0] = 0;
    term_init();
    if (cfg.screen_lines) {
        set_screen_lines(0);
    }
    menu_layout();
    int opened = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            continue;
        }
        const char *dot = strrchr(argv[i], '.');
        if (dot && !strcasecmp(dot, ".fcp")) {
            project_open(argv[i]);
            continue;
        }
        win_open_file(argv[i]);
        opened++;
    }
    if (!opened) {
        desktop_load();
    }
    while (!quit_flag) {
        notes_autosave();
        ide_draw();
        scr_flush();
        int key = next_key();
        if (key == KEY_IDLE || key == KEY_NONE) {
            continue;
        }
        if (key == KEY_RESIZE) {
            wm_resize();
            continue;
        }
        if (key == KEY_PASTE_BEGIN || key == KEY_PASTE_END) {
            continue;
        }
        if (key == KEY_MOUSE) {
            wm_mouse();
            continue;
        }
        Win *w = win_active();
        int cmd = key_to_cmd(key);
        if (cmd == cmMenu) {
            int c = menu_run(0 + 1 > 0 ? 1 : 1);
            if (c) {
                ide_command(c);
            }
            continue;
        }
        if (cmd == cmHelp_) {
            if (w && w->kind == W_HELP) {
                help_show("Using the IDE");
            } else {
                help_contents();
            }
            continue;
        }
        if (w && w->kind == W_HELP && (cmd == 0 || cmd == cmHelpPrev) && key != KEY_F1) {
            if (help_key(w, key)) {
                continue;
            }
        }
        if (cmd) {
            ide_command(cmd);
            continue;
        }
        int hk = menu_hotkey(key);
        if (hk >= 0) {
            int c = menu_run(hk);
            if (c) {
                ide_command(c);
            }
            continue;
        }
        if (!w) {
            continue;
        }
        if (w->kind == W_EDIT || w->kind == W_CLIP || w->kind == W_NOTES) {
            ed_key(w, key);
            continue;
        }
        if (w->kind == W_HELP) {
            help_key(w, key);
            continue;
        }
        list_win_key(w, key);
    }
    term_done();
    printf("Flash C %s\n", FC_VERSION);
}
