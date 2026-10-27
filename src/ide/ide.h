/*
 * File:    ide.h
 * Purpose: Flash C IDE - shared declarations
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef IDE_H
#define IDE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "term.h"
#include "fc.h"

/* ------------------------------------------------------------------ */
/* colour palette (CGA-style attributes: bg<<4 | fg)                   */
/* ------------------------------------------------------------------ */
enum {
    P_MENU_NORM,
    P_MENU_HOT,
    P_MENU_BAR,
    P_MENU_SELHOT,
    P_MENU_DIS,
    P_MENU_SELDIS,
    P_PULL_NORM,
    P_PULL_HOT,
    P_PULL_SEL,
    P_PULL_SELHOT,
    P_PULL_DIS,
    P_PULL_SELDIS,
    P_PULL_BORDER,
    P_STATUS,
    P_STATUS_HOT,
    P_DESKTOP,
    P_ED_NORM,
    P_ED_SEL,
    P_ED_BORDER,
    P_ED_ACTBORDER,
    P_ED_TITLE,
    P_ED_ACTTITLE,
    P_ED_ICON,
    P_ED_SCROLL,
    P_ED_ARROWS,
    P_ED_THUMB,
    P_ED_ERRLINE,
    P_ED_BPLINE,
    P_ED_TRACK,
    P_ED_COMMENT,
    P_ED_RESERVED,
    P_ED_IDENT,
    P_ED_SYMBOL,
    P_ED_STRING,
    P_ED_INTEGER,
    P_ED_PRE,
    P_ED_ILLEGAL,
    P_MSG_NORM,
    P_MSG_SEL,
    P_MSG_BORDER,
    P_MSG_ACTBORDER,
    P_MSG_ERR,
    P_WATCH_NORM,
    P_WATCH_SEL,
    P_OUT_NORM,
    P_HELP_NORM,
    P_HELP_LINK,
    P_HELP_LINKSEL,
    P_HELP_TITLE,
    P_HELP_BORDER,
    P_HELP_EXAMPLE,
    P_DLG_NORM,
    P_DLG_BORDER,
    P_DLG_TITLE,
    P_DLG_BTN,
    P_DLG_BTNDEF,
    P_DLG_BTNSEL,
    P_DLG_BTNHOT,
    P_DLG_BTNDIS,
    P_DLG_INPUT,
    P_DLG_INPUTSEL,
    P_DLG_LIST,
    P_DLG_LISTSEL,
    P_DLG_LISTFOC,
    P_DLG_DIS,
    P_DLG_HOT,
    P_DLG_CHECK,
    P_DLG_INFO,
    P_DLG_ERR,
    P_DLG_WARN,
    P_COUNT
};

typedef struct {
    const char *group, *item;
    uint8_t def;
} PalInfo;

extern const PalInfo palinfo[P_COUNT];
extern uint8_t pal[P_COUNT];
void pal_defaults(void);

/* ------------------------------------------------------------------ */
/* configuration                                                       */
/* ------------------------------------------------------------------ */
#define NWARN 64

typedef struct TransferItem {
    char title[64], path[128], cmd[160];
    int hotkey;
} TransferItem;

enum { NOPT = 12 }; /* number of switchable optimizations (FCO_* bits in the compiler) */

typedef struct Config {
    /* compiler / source */
    int nested_comments, unsigned_char, ident_len;
    int enum_int, merge_strings, align_struct, stack_check, ansi_kw; /* code generation / source */
    int jump_opt, red_loads;                                         /* optimizations */
    int stack_kb, elf_symtab, wx_segments;                           /* linker */
    char undefs[120];
    int max_errors, max_warnings, warn_display; /* 0 none, 1 selected, 2 all */
    int opt[NOPT];                                                   /* FCO_* passes, by bit number */
    int warn[NWARN];                            /* only the entries used by ide_fill_options() */
    int fp;                                     /* 0 no floating point, 1 floating point */
    char defines[200];
    /* make / link */
    int check_autodep;
    int map_file, lnk_default_libs;
    int lib_stdrt, lib_math, lib_wild;
    int dbg_source, dbg_swap, dbg_radix;
    /* directories */
    char incdir[256], outdir[256];
    char run_args[256];
    /* environment */
    int screen_lines, src_tracking, cmdset;
    int as_editor, as_env, as_desktop, as_project, save_old_msgs;
    int backup, insert, autoindent, use_tab, syntax, bs_unindent;
    int group_undo, persist_blocks, overwrite_blocks, tabsize;
    char defext[8];
    int mouse_right;
    int sv_watch, sv_bp, sv_open;
    int pause_after_run;
    TransferItem xfer[12];
    int nxfer;
    uint8_t colors[P_COUNT];
    char last_project[256];
} Config;

extern Config cfg;
void cfg_defaults(void);
int cfg_load(void);
int cfg_save(void);
const char *cfg_path(void);

/* ------------------------------------------------------------------ */
/* text buffers                                                        */
/* ------------------------------------------------------------------ */
/* One line of text (no trailing newline). */
typedef struct Line {
    char *text;
    int len, cap;
} Line;

/* One undoable edit.  type says whether text was inserted or deleted; (l, c)-(l2, c2) is the
   affected range and text the removed/inserted characters.  Edits with the same group number
   are undone together.  cursor_line/cursor_col restore the cursor after an undo. */
typedef struct Undo {
    int type, l, c, l2, c2, group;
    char *text;
    int cursor_line, cursor_col;
} Undo;

/* A text buffer: the lines of one file plus its undo history and syntax-colouring cache. */
typedef struct Buf {
    Line *lines;
    int n, cap;          /* number of lines used / allocated */
    char name[512];      /* file name (absolute once saved or opened) */
    int named, modified; /* named: has a real file name; modified: unsaved changes */
    Undo *undo;          /* undo history; entries [0, undo_pos) are applied */
    int undo_count, undo_cap, undo_pos, group, last_kind;
    unsigned char *comment_state; /* per line: 1 when the line starts inside a block comment */
    int comment_state_count, comment_state_cap, dirty;
    int readonly, version; /* version changes on every edit */
} Buf;

Buf *buf_new(void);
void buf_free(Buf *b);
int buf_load(Buf *b, const char *path); /* 0 ok */
int buf_save(Buf *b, const char *path, int backup);
char *buf_get(Buf *b, int l1, int c1, int l2, int c2); /* malloc'd text of a range */
void buf_insert(Buf *b, int *l, int *c, const char *text,
                int n); /* cursor moved to end of insert */
void buf_delete(Buf *b, int l1, int c1, int l2, int c2);
void buf_clear(Buf *b);
void buf_group(Buf *b); /* start a new undo group */
int buf_undo(Buf *b, int *l, int *c);
int buf_redo(Buf *b, int *l, int *c);
char *buf_all(Buf *b, int *len);
void buf_set_data(Buf *b, const char *data,
                  long len);             /* replace the text; clears undo, not modified */
int syntax_line_state(Buf *b, int line); /* 1 if line starts inside a block comment */
void syntax_line_attrs(Buf *b, int line, uint8_t *attrs, int n);

/* regex-ish search */
int text_find(const char *line, int len, int from, const char *pat, int cs, int whole, int rx,
              int *mlen);
int text_find_rev(const char *line, int len, int before, const char *pat, int cs, int whole, int rx,
                  int *mlen);

/* ------------------------------------------------------------------ */
/* windows                                                             */
/* ------------------------------------------------------------------ */
enum { W_EDIT, W_MSG, W_OUT, W_WATCH, W_PROJ, W_HELP, W_REG, W_CLIP, W_NOTES, W_CALLS, W_LIST };

/* A window on the desktop.  kind selects the behaviour (W_EDIT, W_MSG, ...).  Fields that only
   apply to editor windows are marked; list windows use sel/top for their selection. */
typedef struct Win {
    int kind, x, y, w, h, num, zoomed;
    int saved_x, saved_y, saved_w, saved_h; /* frame before zooming */
    char title[96];
    Buf *buf;                          /* editor windows */
    int cur_col, cur_line;             /* cursor position in the text */
    int top, left;                     /* first visible line / column */
    int want_col;                      /* column to return to when moving vertically */
    int insert;                        /* insert (1) or overwrite (0) mode */
    int sel_active, sel_line, sel_col; /* shift-selection anchor */
    int block_active, block_line1, block_col1, block_line2, block_col2; /* Ctrl-K B / K markers */
    int sel;                                                            /* list selection */
    int key_prefix;    /* pending Ctrl-K / Ctrl-Q */
    int error_line;    /* highlighted line, -1 none */
    int error_version; /* buffer version when error_line was set; an edit hides the bar */
    int hist_pos;
    void *data; /* kind-specific state (help topic, ...) */
} Win;

#define MAXWIN 40
extern Win *wins[MAXWIN];
extern int nwins;

typedef struct Msg {
    int type;
    char file[256];
    int line;
    char text[256];
    int header;
} Msg;

extern Msg *msgs;
extern int nmsgs, msgsel;
void msg_clear(void);
void msg_add(int type, const char *file, int line, const char *text, int header);

extern Buf *clipbuf;
extern char project_name[256];

/* settings of one project item (Project|Local options); proj_opts[i] belongs to proj_files[i] */
typedef struct ProjItem {
    char cmd[200]; /* extra compiler switches, applied to the whole build */
    char out[260]; /* where the executable is written: a file name or an existing directory */
    int nodebug;   /* leave this file out of the debug information */
    int nolink;    /* do not compile this file into the program */
} ProjItem;

extern char **proj_files;
extern ProjItem *proj_opts;
extern int nproj, proj_sel;
extern char **watches;
extern int nwatch;
extern char *notes_text;

Win *win_open_file(const char *path); /* open (or activate) a file */
Win *win_new_edit(const char *name);
Win *win_find_kind(int kind);
Win *win_active(void);
void win_activate(Win *w);
void win_close(Win *w);
void win_close_all(void);
void win_tile(void);
void win_cascade(void);
void win_zoom(Win *w);
void win_set_title(Win *w);
Win *win_by_num(int n);
Win *win_get_special(int kind);

/* ------------------------------------------------------------------ */
/* dialogs                                                             */
/* ------------------------------------------------------------------ */
enum {
    cmOK = 1,
    cmCancel,
    cmYes,
    cmNo,
    cmHelp,
    cmOpen,
    cmReplace,
    cmChange,
    cmChangeAll,
    cmEdit,
    cmDelete,
    cmNew,
    cmModify,
    cmAdd,
    cmDone,
    cmView,
    cmChdir,
    cmRevert,
    cmEvaluate,
    cmStop,
    cmIgnore,
    cmRetry,
    cmPrev,
    cmContents,
    cmIndex,
    cmSave,
    cmAt,
    cmDefault_
};

enum { D_STATIC, D_INPUT, D_CHECK, D_RADIO, D_BUTTON, D_LIST, D_FRAME, D_CUSTOM, D_LABEL };
struct Dialog;

typedef struct DItem {
    int type, x, y, w, h;
    const char *text;
    char *buf;
    int cap; /* input */
    int *val;
    int bit; /* check (bit mask, 0 = whole value), radio (index) */
    int cmd; /* button */
    int disabled, defbtn, link;
    const char *hint;
    char **items;
    const char **helps;   /* check list: hint of every item */
    int nitems, sel, top; /* list; with val set it is a check list over the ints val[0..nitems) */
    void (*draw)(struct Dialog *, struct DItem *, int focused);
    int (*onkey)(struct Dialog *, struct DItem *, int key);
    int cpos, scroll, selall; /* input state */
    char nbuf[24];            /* numeric input storage */
} DItem;

typedef struct Dialog {
    const char *title;
    int w, h, x, y;
    DItem it[48];
    int n, focus;
    int result;
    int done;
    void *user;
    void (*on_change)(struct Dialog *);
    int (*on_cmd)(struct Dialog *, int cmd); /* return nonzero to keep the dialog open */
    int cur_x, cur_y;                        /* builder cursor */
    int bw;                                  /* group width */
    int group_start, in_group, gx;
} Dialog;

int dlg_run(Dialog *d);
void dlg_init(Dialog *d, const char *title);
DItem *dlg_add(Dialog *d, DItem it);
DItem *dlg_static(Dialog *d, int x, int y, const char *text);
DItem *dlg_input(Dialog *d, int x, int y, int w, const char *label, char *buf, int cap,
                 const char *hint);
DItem *dlg_check(Dialog *d, int x, int y, const char *text, int *val, int bit, const char *hint);
DItem *dlg_radio(Dialog *d, int x, int y, const char *text, int *val, int idx, const char *hint);
DItem *dlg_button(Dialog *d, int x, int y, int w, const char *text, int cmd, int def,
                  const char *hint);
DItem *dlg_frame(Dialog *d, int x, int y, int w, int h, const char *title);
DItem *dlg_list(Dialog *d, int x, int y, int w, int h, char **items, int n, const char *hint);
/* flowing builder used by the options dialogs */
void db_start(Dialog *d, const char *title, int width);
void db_at(Dialog *d, int gx, int y, int bw);            /* start a column */
void db_group(Dialog *d, const char *title, int nlines); /* framed group; call db_end_group */
void db_end_group(Dialog *d);
DItem *db_check(Dialog *d, const char *text, int *val, int bit, const char *hint);
DItem *db_radio(Dialog *d, const char *text, int *val, int idx, const char *hint);
DItem *db_input(Dialog *d, const char *label, char *buf, int cap, int w, const char *hint);
DItem *db_num(Dialog *d, const char *label, int *val, int w, const char *hint);
void db_gap(Dialog *d);
void db_finish(Dialog *d, int help);                          /* adds OK / Cancel / Help */
int msgbox(const char *title, const char *text, int buttons); /* returns cm* */
void infobox(const char *title, const char *fmt, ...);
int inputbox(const char *title, const char *label, char *buf, int cap, const char *hint);
#define MB_OK 1
#define MB_YESNO 2
#define MB_YESNOCANCEL 4
#define MB_ERROR 16
#define MB_WARN 32
int file_dialog(const char *title, const char *mask, char *out, int outcap, int save);
void status_hint(const char *s);

/* ------------------------------------------------------------------ */
/* menus & commands                                                    */
/* ------------------------------------------------------------------ */
enum {
    cmNone = 100,
    cmAbout,
    cmClearDesktop,
    cmRepaint,
    cmTransfer0,
    cmNewFile = 130,
    cmOpenFile,
    cmSaveFile,
    cmSaveAs,
    cmSaveAll,
    cmChangeDir,
    cmPrint,
    cmShell,
    cmQuit,
    cmUndo,
    cmRedo,
    cmCut,
    cmCopy,
    cmPaste,
    cmClear,
    cmCopyExample,
    cmShowClip,
    cmFind,
    cmReplaceText,
    cmSearchAgain,
    cmGotoLine,
    cmPrevError,
    cmNextError,
    cmLocateFn,
    cmRun,
    cmProgReset,
    cmGotoCursor,
    cmTraceInto,
    cmStepOver,
    cmArguments,
    cmCompile,
    cmMake,
    cmLink,
    cmBuildAll,
    cmInformation,
    cmRemoveMsgs,
    cmInspect,
    cmEvalMod,
    cmCallStack,
    cmAddWatch,
    cmDelWatch,
    cmEditWatch,
    cmRemoveWatches,
    cmToggleBP,
    cmBreakpoints,
    cmOpenProject,
    cmCloseProject,
    cmAddItem,
    cmDelItem,
    cmLocalOpts,
    cmIncludeFiles,
    cmOptCodeGen,
    cmOptOptimize,
    cmOptSource,
    cmOptMsgDisplay,
    cmOptMsgPortability,
    cmOptMsgAnsi,
    cmOptMsgFrequent,
    cmOptMsgLess,
    cmOptTransfer,
    cmOptMake,
    cmOptLinkSettings,
    cmOptLinkLibs,
    cmOptDebugger,
    cmOptDirs,
    cmOptPrefs,
    cmOptEditor,
    cmOptMouse,
    cmOptDesktop,
    cmOptColors,
    cmOptSave,
    cmSizeMove,
    cmZoom,
    cmTile,
    cmCascade,
    cmNextWin,
    cmCloseWin,
    cmCloseAll,
    cmWinMessage,
    cmWinOutput,
    cmWinWatch,
    cmUserScreen,
    cmWinRegister,
    cmWinProject,
    cmWinNotes,
    cmWinList,
    cmHelpContents,
    cmHelpIndex,
    cmHelpTopic,
    cmHelpPrev,
    cmHelpOnHelp,
    cmHelp_,
    cmMenu,
    cmWinNum1 = 400
};

/* Modes of ide_build(). */
enum { BUILD_COMPILE, BUILD_MAKE, BUILD_LINK, BUILD_ALL, BUILD_FOR_RUN };

int ide_build(int mode);

void ide_run(int argc, char **argv);
void ide_draw(void);
void ide_command(int cmd);
void ide_refresh(void);
void ide_message(const char *fmt, ...);

/* editor */
void ed_draw(Win *w, int active);
int ed_key(Win *w, int key);
void ed_mouse(Win *w);
void ed_goto(Win *w, int line, int col);
void ed_ensure_visible(Win *w);
int ed_has_block(Win *w);
void ed_block_range(Win *w, int *l1, int *c1, int *l2, int *c2);
void ed_copy(Win *w, int cut);
void ed_paste(Win *w);
void ed_clear_block(Win *w);
int ed_find_next(Win *w, int dir_override);
extern char find_text[256], repl_text[256];
extern int find_opts[8]; /* case, whole, regex, prompt, direction, scope, origin */

/* help */
void help_show(const char *topic);
void help_contents(void);
void help_index(void);
void help_topic_search(const char *word);
void help_prev(void);
void help_draw(Win *w, int active);
void help_free(Win *w);
void help_scroll(Win *w, int delta);
void help_bar_click(Win *w, int y);
char *help_example_text(void); /* program text of the current help topic (malloc'd) or NULL */
int help_key(Win *w, int key);
void help_click(Win *w, int x, int y);
void help_mouse_press(
    Win *w, int x,
    int y); /* text selection with the mouse; a click without a drag follows a link */
void help_mouse_drag(Win *w, int x, int y);
void help_mouse_release(Win *w);
int help_has_selection(Win *w);
int help_copy(Win *w); /* copy the selected text to the Clipboard; 0 when nothing is selected */

/* compile support */
int ide_build(int mode); /* returns 0 on success */
const char *ide_exe_name(char *buf, int n);
void ide_fill_options(FcOptions *o);

/* misc */
int file_exists(const char *p);
void base_name(const char *path, char *out, int n);
void set_status_text(const char *s);
void clip_set_system(const char *text, int n);
void clip_set_text(
    const char *text); /* put text into the Clipboard window and the terminal clipboard */
#endif

/* ------------------------------------------------------------------ */
/* menus                                                               */
/* ------------------------------------------------------------------ */
struct MMenu;

typedef struct MItem {
    const char *text, *key;
    int cmd;
    const char *hint;
    const struct MMenu *sub;
} MItem;

typedef struct MMenu {
    const char *title;
    const MItem *items;
    int n;
} MMenu;

#define NBAR 11
extern int bar_x[NBAR], bar_w[NBAR];
extern int menu_active; /* bar index being displayed as active, -1 none */
void menu_layout(void);
int menu_run(int start);  /* returns command or 0 */
int menu_hotkey(int key); /* Alt+letter -> bar index or -1 */
void menubar_draw(void);
int cmd_enabled(int cmd);
const char *menu_bar_title(int i);
extern int sys_menu_extra; /* number of transfer items in the system menu */
void statusline_draw(void);
int statusline_click(int x); /* returns key to inject or 0 */
Win *win_at(int x, int y);
void wm_mouse(void);
void wm_resize(void);
void wm_relayout_msg(int open);
extern char **outlines;
extern int noutlines;
void out_clear(void);
void out_add(const char *s);
void out_feed(
    const char *data,
    int len); /* program output in arbitrary pieces, split into lines for the Output window */
int win_edit_count(void);
void list_win_key(Win *w, int key);
extern int drag_mode;

/* dialogs.c */
void opt_command(int cmd);
int dlg_find_text(int replace); /* returns cmOK / cmChange / cmChangeAll / cmCancel */
int dlg_goto_line(int *line);
int dlg_locate(char *name, int cap);
void dlg_about(void);
void dlg_compiler_info(int lines, int errors, int warnings);
int dlg_window_list(int *action); /* returns window index or -1; action: 0 select, 1 close */
void dlg_eval(void);

/* debugger.c */
void dbgui_init(void);
int dbgui_active(void); /* a program is loaded in the debugger and stopped */
void dbgui_reset(void); /* program reset */
int dbgui_wants_run(void);
void dbgui_go(void);
void dbgui_step(int into);
void dbgui_goto_cursor(void);
void dbgui_toggle_bp(void);
void dbgui_breakpoints(void);
void dbgui_call_stack(void);
void dbgui_inspect(void);
int dbgui_line_pal(const char *file, int line); /* palette index for the line, -1 none */
int dbgui_eval(const char *expr, char *out, int n);
int dbgui_assign(const char *expr, const char *val, char *out, int n);
void dbgui_save_bps(FILE *f);
void dbgui_load_bp(const char *s);
void dlg_transfer(void);
void dlg_colors(void);
int dlg_local_options(const char *file,
                      ProjItem *item); /* returns 1 if the settings were changed */
void dlg_include_files(const char *file);
