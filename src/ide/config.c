/*
 * File:    config.c
 * Purpose: Flash C IDE - palette and configuration persistence
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "ide.h"
#include <unistd.h>
#include <ctype.h>

const PalInfo palinfo[P_COUNT] = {
    [P_MENU_NORM] = {"Main Menu", "Normal Text", 0x70},
    [P_MENU_HOT] = {"Main Menu", "First Letter", 0x74},
    [P_MENU_BAR] = {"Main Menu", "Selection Bar", 0x20},
    [P_MENU_SELHOT] = {"Main Menu", "Selected Letter", 0x24},
    [P_MENU_DIS] = {"Main Menu", "Disabled Entry", 0x78},
    [P_MENU_SELDIS] = {"Main Menu", "Selected Disabled", 0x28},
    [P_PULL_NORM] = {"Pulldown Menus", "Normal Text", 0x70},
    [P_PULL_HOT] = {"Pulldown Menus", "First Letter", 0x74},
    [P_PULL_SEL] = {"Pulldown Menus", "Selection Bar", 0x20},
    [P_PULL_SELHOT] = {"Pulldown Menus", "Selected Letter", 0x24},
    [P_PULL_DIS] = {"Pulldown Menus", "Disabled Entry", 0x78},
    [P_PULL_SELDIS] = {"Pulldown Menus", "Selected Disabled", 0x28},
    [P_PULL_BORDER] = {"Pulldown Menus", "Border", 0x70},
    [P_STATUS] = {"Status Line", "Normal Text", 0x70},
    [P_STATUS_HOT] = {"Status Line", "Hot Keys", 0x74},
    [P_DESKTOP] = {"Desktop Color", "Normal Text", 0x17},
    [P_ED_NORM] = {"Edit Window", "Normal Text", 0x1E},
    [P_ED_SEL] = {"Edit Window", "Marked Text", 0x71},
    [P_ED_BORDER] = {"Edit Window", "Inactive Border", 0x17},
    [P_ED_ACTBORDER] = {"Edit Window", "Active Border", 0x1F},
    [P_ED_TITLE] = {"Edit Window", "Inactive Title", 0x17},
    [P_ED_ACTTITLE] = {"Edit Window", "Active Title", 0x1F},
    [P_ED_ICON] = {"Edit Window", "Icons", 0x1A},
    [P_ED_SCROLL] = {"Edit Window", "Scroll Bar", 0x31},
    [P_ED_ARROWS] = {"Edit Window", "Arrows", 0x31},
    [P_ED_THUMB] = {"Edit Window", "Thumb Tab", 0x31},
    [P_ED_ERRLINE] = {"Edit Window", "Error Line", 0x4F},
    [P_ED_BPLINE] = {"Edit Window", "Break Point Line", 0x4F},
    [P_ED_TRACK] = {"Edit Window", "Tracking Bar", 0x20},
    [P_ED_COMMENT] = {"Syntax", "Comment", 0x17},
    [P_ED_RESERVED] = {"Syntax", "Reserved", 0x1F},
    [P_ED_IDENT] = {"Syntax", "Identifier", 0x1E},
    [P_ED_SYMBOL] = {"Syntax", "Symbol", 0x1E},
    [P_ED_STRING] = {"Syntax", "String", 0x1B},
    [P_ED_INTEGER] = {"Syntax", "Integer", 0x1B},
    [P_ED_PRE] = {"Syntax", "Preprocessor", 0x1A},
    [P_ED_ILLEGAL] = {"Syntax", "Illegal Char", 0x4E},
    [P_MSG_NORM] = {"Message Window", "Normal Message", 0x30},
    [P_MSG_SEL] = {"Message Window", "Selected Message", 0x2F},
    [P_MSG_BORDER] = {"Message Window", "Border", 0x38},
    [P_MSG_ACTBORDER] = {"Message Window", "Active Border", 0x3F},
    [P_MSG_ERR] = {"Message Window", "Error", 0x34},
    [P_WATCH_NORM] = {"Watch Window", "Normal Watch", 0x30},
    [P_WATCH_SEL] = {"Watch Window", "Selected Watch", 0x2F},
    [P_OUT_NORM] = {"Output Window", "Normal Text", 0x07},
    [P_HELP_NORM] = {"Modal Help", "Normal Text", 0x30},
    [P_HELP_LINK] = {"Modal Help", "Keyword", 0x3F},
    [P_HELP_LINKSEL] = {"Modal Help", "Selected Keyword", 0x2F},
    [P_HELP_TITLE] = {"Modal Help", "Title", 0x3E},
    [P_HELP_BORDER] = {"Modal Help", "Border", 0x3F},
    [P_HELP_EXAMPLE] = {"Modal Help", "Example Program", 0x70},
    [P_DLG_NORM] = {"Dialog Window", "Normal Text", 0x70},
    [P_DLG_BORDER] = {"Dialog Window", "Border", 0x7F},
    [P_DLG_TITLE] = {"Dialog Window", "Title", 0x7F},
    [P_DLG_BTN] = {"Dialog Buttons", "Normal Text", 0x20},
    [P_DLG_BTNDEF] = {"Dialog Buttons", "Default", 0x2B},
    [P_DLG_BTNSEL] = {"Dialog Buttons", "Selected", 0x2F},
    [P_DLG_BTNHOT] = {"Dialog Buttons", "First Letter", 0x2E},
    [P_DLG_BTNDIS] = {"Dialog Buttons", "Disabled Entry", 0x78},
    [P_DLG_INPUT] = {"Dialog Text Entry", "Normal Text", 0x1F},
    [P_DLG_INPUTSEL] = {"Dialog Text Entry", "Selected Text", 0x71},
    [P_DLG_LIST] = {"Dialog Lists", "Normal Text", 0x1E},
    [P_DLG_LISTSEL] = {"Dialog Lists", "Selection Bar", 0x30},
    [P_DLG_LISTFOC] = {"Dialog Lists", "Active", 0x20},
    [P_DLG_DIS] = {"Dialog Controls", "Disabled Entry", 0x78},
    [P_DLG_HOT] = {"Dialog Controls", "First Letter", 0x74},
    [P_DLG_CHECK] = {"Dialog Controls", "Active Letter", 0x7F},
    [P_DLG_INFO] = {"Dialog Messages", "Information", 0x70},
    [P_DLG_ERR] = {"Dialog Messages", "Error", 0x74},
    [P_DLG_WARN] = {"Dialog Messages", "Warning", 0x7E},
};
uint8_t pal[P_COUNT];

/* Resets the colour palette to the built-in colours. */
void pal_defaults(void)
{
    for (int i = 0; i < P_COUNT; i++) {
        pal[i] = palinfo[i].def;
    }
}

Config cfg;

/* Resets all settings to their defaults. */
void cfg_defaults(void)
{
    memset(&cfg, 0, sizeof cfg);
    cfg.ident_len = 32;
    cfg.enum_int = 1;
    cfg.align_struct = 1;
    cfg.max_errors = 25;
    cfg.max_warnings = 100;
    cfg.warn_display = 1;
    static const int on[] = {0,  1,  2,  6,  8,  15, 32,
                             34, 37, 38, 40, 43, 44, 48}; /* the warnings the compiler can switch */
    for (unsigned i = 0; i < sizeof on / sizeof on[0]; i++) {
        cfg.warn[on[i]] = 1;
    }
    cfg.fp = 1;
    cfg.check_autodep = 1;
    cfg.lnk_default_libs = 1;
    cfg.lib_stdrt = 1;
    cfg.lib_math = 1;
    cfg.dbg_source = 0;
    cfg.dbg_swap = 1;
    cfg.screen_lines = 0;
    cfg.src_tracking = 1;
    cfg.cmdset = 1;
    cfg.as_editor = 1;
    cfg.backup = 1;
    cfg.insert = 1;
    cfg.autoindent = 1;
    cfg.use_tab = 1;
    cfg.syntax = 1;
    cfg.bs_unindent = 1;
    cfg.group_undo = 1;
    cfg.persist_blocks = 0;
    cfg.overwrite_blocks = 1;
    cfg.tabsize = 8;
    strcpy(cfg.defext, ".c");
    cfg.sv_watch = 1;
    cfg.sv_bp = 1;
    cfg.sv_open = 1;
    cfg.pause_after_run = 1;
    cfg.nxfer = 1;
    snprintf(cfg.xfer[0].title, 64, "~Grep");
    snprintf(cfg.xfer[0].path, 128, "grep");
    snprintf(cfg.xfer[0].cmd, 160, "-n $PROMPT $EDNAME");
    cfg.xfer[0].hotkey = 0;
    for (int i = 0; i < P_COUNT; i++) {
        cfg.colors[i] = palinfo[i].def;
    }
}

typedef struct {
    const char *name;
    void *p;
    int kind, n;
} CfgVar; /* kind: 0 int, 1 string, 2 int array */

#define I(f) {#f, &cfg.f, 0, 0}
#define S(f) {#f, cfg.f, 1, (int)sizeof cfg.f}
static CfgVar vars[] = {
    I(nested_comments),
    I(unsigned_char),
    I(ident_len),
    I(enum_int),
    I(merge_strings),
    I(align_struct),
    I(stack_check),
    I(ansi_kw),
    I(jump_opt),
    I(red_loads),
    I(stack_kb),
    I(elf_symtab),
    I(wx_segments),
    S(undefs),
    I(max_errors),
    I(max_warnings),
    I(warn_display),
    {"warn", cfg.warn, 2, NWARN},
    I(fp),
    S(defines),
    I(check_autodep),
    I(map_file),
    I(lnk_default_libs),
    I(lib_stdrt),
    I(lib_math),
    I(lib_wild),
    I(dbg_source),
    I(dbg_swap),
    I(dbg_radix),
    S(incdir),
    S(outdir),
    S(run_args),
    I(screen_lines),
    I(src_tracking),
    I(cmdset),
    I(as_editor),
    I(as_env),
    I(as_desktop),
    I(as_project),
    I(save_old_msgs),
    I(backup),
    I(insert),
    I(autoindent),
    I(use_tab),
    I(syntax),
    I(bs_unindent),
    I(group_undo),
    I(persist_blocks),
    I(overwrite_blocks),
    I(tabsize),
    S(defext),
    I(mouse_right),
    I(sv_watch),
    I(sv_bp),
    I(sv_open),
    I(pause_after_run),
    S(last_project),
};

static char cfgpath[600];

/*
 * Returns the path of the configuration file: the one that was loaded, else fcconfig.fc in the
 * current directory.
 */
const char *cfg_path(void)
{
    return cfgpath[0] ? cfgpath : "fcconfig.fc";
}

/*
 * Reads fcconfig.fc from the current directory or ~/.fcconfig.fc.  Unknown keys are ignored.
 * Returns 0 on success.
 */
int cfg_load(void)
{
    const char *home = getenv("HOME");
    char alt[600] = "";
    if (home) {
        snprintf(alt, sizeof alt, "%s/.fcconfig.fc", home);
    }
    const char *tries[2] = {"fcconfig.fc", alt};
    FILE *f = NULL;
    for (int i = 0; i < 2 && !f; i++) {
        if (tries[i][0] && (f = fopen(tries[i], "r"))) {
            snprintf(cfgpath, sizeof cfgpath, "%s", tries[i]);
        }
    }
    if (!f) {
        return 1;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = 0;
        }
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = 0;
        char *val = eq + 1;
        if (!strcmp(line, "color")) {
            int idx, v;
            if (sscanf(val, "%d %x", &idx, &v) == 2 && idx >= 0 && idx < P_COUNT) {
                cfg.colors[idx] = v;
            }
            continue;
        }
        if (!strcmp(line, "xfer")) {
            if (cfg.nxfer < 12) {
                TransferItem *t = &cfg.xfer[cfg.nxfer];
                char *a = strtok(val, "|"), *b = strtok(NULL, "|"), *c = strtok(NULL, "|"),
                     *d = strtok(NULL, "|");
                if (a && b) {
                    snprintf(t->title, 64, "%s", a);
                    snprintf(t->path, 128, "%s", b);
                    snprintf(t->cmd, 160, "%s", c ? c : "");
                    t->hotkey = d ? atoi(d) : 0;
                    cfg.nxfer++;
                }
            }
            continue;
        }
        if (!strcmp(line, "xfer_reset")) {
            cfg.nxfer = 0;
            continue;
        }
        for (unsigned i = 0; i < sizeof vars / sizeof vars[0]; i++) {
            if (strcmp(vars[i].name, line)) {
                continue;
            }
            if (vars[i].kind == 0) {
                *(int *)vars[i].p = atoi(val);
            } else if (vars[i].kind == 1) {
                snprintf((char *)vars[i].p, vars[i].n, "%s", val);
            } else {
                int *a = vars[i].p;
                for (int k = 0; k < vars[i].n && *val; k++) {
                    a[k] = (int)strtol(val, &val, 10);
                }
            }
            break;
        }
    }
    fclose(f);
    for (int i = 0; i < P_COUNT; i++) {
        pal[i] = cfg.colors[i];
    }
    return 0;
}

/* Writes the configuration file.  Returns 0 on success, 1 if it cannot be written. */
int cfg_save(void)
{
    FILE *f = fopen(cfg_path(), "w");
    if (!f) {
        return 1;
    }
    fprintf(f, "# Flash C configuration\n");
    for (unsigned i = 0; i < sizeof vars / sizeof vars[0]; i++) {
        if (vars[i].kind == 0) {
            fprintf(f, "%s=%d\n", vars[i].name, *(int *)vars[i].p);
        } else if (vars[i].kind == 1) {
            fprintf(f, "%s=%s\n", vars[i].name, (char *)vars[i].p);
        } else {
            fprintf(f, "%s=", vars[i].name);
            for (int k = 0; k < vars[i].n; k++) {
                fprintf(f, "%d ", ((int *)vars[i].p)[k]);
            }
            fputc('\n', f);
        }
    }
    for (int i = 0; i < P_COUNT; i++) {
        fprintf(f, "color=%d %02x\n", i, pal[i]);
    }
    fprintf(f, "xfer_reset=1\n");
    for (int i = 0; i < cfg.nxfer; i++) {
        fprintf(f, "xfer=%s|%s|%s|%d\n", cfg.xfer[i].title, cfg.xfer[i].path, cfg.xfer[i].cmd,
                cfg.xfer[i].hotkey);
    }
    fclose(f);
    return 0;
}
