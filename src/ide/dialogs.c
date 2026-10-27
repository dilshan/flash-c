/*
 * File:    dialogs.c
 * Purpose: Flash C IDE - Options dialogs, search dialogs and other dialog boxes
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "ide.h"
#include <ctype.h>
#include <unistd.h>

#define H_OK "Accept the settings in this dialog box"

/* run a settings dialog transactionally: Cancel restores the configuration */
static int opt_run(Dialog *d, const char *helptopic)
{
    static Config bak;
    bak = cfg;
    uint8_t palbak[P_COUNT];
    memcpy(palbak, pal, sizeof pal);
    int r;
    do {
        r = dlg_run(d);
        if (r == cmHelp) {
            help_show(helptopic ? helptopic : "Compiler options");
        }
    } while (r == cmHelp);
    if (r != cmOK) {
        cfg = bak;
        memcpy(pal, palbak, sizeof pal);
    }
    return r == cmOK;
}

/* Adds a column of radio buttons for the n names, all bound to *val. */
static void radios(Dialog *d, int *val, const char **names, int n, const char **hints)
{
    for (int i = 0; i < n; i++) {
        db_radio(d, names[i], val, i, hints ? hints[i] : NULL);
    }
}

/* ---------- warning check lists ---------- */
static void warn_dialog(const char *title, const char **names, const int *idx, int n, int width)
{
    Dialog d;
    db_start(&d, title, width);
    for (int i = 0; i < n; i++) {
        db_check(&d, names[i], &cfg.warn[idx[i]], 0,
                 "Select this message to be displayed as a warning");
    }
    d.cur_y += 0;
    db_finish(&d, 1);
    if (opt_run(&d, "Messages and errors")) {
    }
}

/* Shows the Options|Compiler|Messages|Display dialog. */
static void msg_dialog_display(void)
{
    Dialog d;
    db_start(&d, " Compiler Messages ", 58);
    db_num(&d, "~E~rrors:   Stop After", &cfg.max_errors, 8,
           "Stop compiling after detecting a specified number of errors");
    db_num(&d, "~W~arnings: Stop After", &cfg.max_warnings, 8,
           "Stop compiling after detecting a specified number of warnings");
    db_group(&d, "Display warnings", 3);
    db_radio(&d, "~S~elected", &cfg.warn_display, 1, "Display only selected warning messages");
    db_radio(&d, "~A~ll", &cfg.warn_display, 2, "Display all warning messages");
    db_radio(&d, "~N~one", &cfg.warn_display, 0, "Suppress all warning messages");
    db_end_group(&d);
    db_finish(&d, 1);
    opt_run(&d, "Messages and errors");
}

/* Opens the Options dialog that belongs to the menu command cmd. */
void opt_command(int cmd)
{
    Dialog d;
    switch (cmd) {
    case cmOptCodeGen: {
        db_start(&d, " Code Generation ", 74);
        db_check(&d, "T~r~eat enums as ints", &cfg.enum_int, 0,
                 "Treat enums as int; off = use the smallest integer type that holds all values");
        db_check(&d, "Duplicate strings ~m~erged", &cfg.merge_strings, 0,
                 "Store identical string literals only once in the executable");
        db_check(
            &d, "~A~lign structure members", &cfg.align_struct, 0,
            "Pad structure members to their natural alignment; off = pack them without padding");
        db_check(&d, "~U~nsigned characters", &cfg.unsigned_char, 0,
                 "Treat all char declarations as if they were unsigned char type");
        db_check(&d, "Test stack o~v~erflow", &cfg.stack_check, 0,
                 "Check for a stack overflow at function entry and stop with 'Stack overflow!'");
        db_gap(&d);
        db_group(&d, "Floating Point", 2);
        db_radio(&d, "No~n~e", &cfg.fp, 0,
                 "Don't use floating-point numbers (the math library is not linked)");
        db_radio(&d, "~F~loating point", &cfg.fp, 1, "Generate code for floating-point numbers");
        db_end_group(&d);
        db_input(&d, "~D~efines", cfg.defines, sizeof cfg.defines, 60,
                 "Pass macro definitions to the preprocessor (NAME or NAME=VALUE, separated by "
                 "semicolons)");
        db_input(&d, "Undefine ~s~ymbols", cfg.undefs, sizeof cfg.undefs, 60,
                 "Remove predefined or defined macros (names separated by semicolons)");
        db_finish(&d, 1);
        opt_run(&d, "Code generation options");
        break;
    }
    case cmOptOptimize: {
        static char *labels[NOPT + 2] = {
            "Constant folding",
            "Algebraic simplification",
            "Constant comparison evaluation",
            "Constant propagation",
            "Copy propagation",
            "Dead assignment elimination",
            "Unreachable code elimination",
            "Redundant cast elimination",
            "Expression canonicalization",
            "Common subexpression elimination",
            "Strength reduction",
            "Boolean simplification",
            "Jump optimization",
            "Suppress redundant loads",
        };
        static const char *helps[NOPT + 2] = {
            "Calculate operations on constants while compiling",
            "Simplify x+0, x*1, x-x, (x+1)+2 and similar expressions",
            "Decide comparisons whose result is known, such as unsigned >= 0",
            "Use the known constant values of local variables",
            "Use the original of a local variable that was copied",
            "Remove assignments whose value is never used",
            "Remove statements that can never run (after return, if (0), while (0))",
            "Remove conversions that do not change the value",
            "Put the operands of expressions in a fixed order, constants last",
            "Compute an expression that occurs several times only once",
            "Replace multiplication, division and modulo by shifts and masks",
            "Simplify conditions: !!x, x == 0, c ? 1 : 0, constant && and ||",
            "Remove jumps to the next instruction and use short jumps where the target is near",
            "Keep constant operands in a register instead of saving and restoring the left operand",
        };
        int on[NOPT + 2];
        for (int i = 0; i < NOPT; i++) {
            on[i] = cfg.opt[i];
        }
        on[NOPT] = cfg.jump_opt;
        on[NOPT + 1] = cfg.red_loads;
        db_start(&d, " Optimization Options ", 60);
        dlg_static(&d, 2, 2, "Select the optimizations to apply:");
        DItem *list = dlg_list(&d, 2, 3, 42, 11, labels, NOPT + 2, NULL);
        list->val = on; /* check list: Space or a click on the box switches an entry */
        list->helps = helps;
        db_finish(&d, 1);
        d.h = 16;
        if (opt_run(&d, "Optimization options")) {
            for (int i = 0; i < NOPT; i++) {
                cfg.opt[i] = on[i];
            }
            cfg.jump_opt = on[NOPT];
            cfg.red_loads = on[NOPT + 1];
        }
        break;
    }
    case cmOptSource: {
        db_start(&d, " Source Options ", 60);
        db_check(&d, "~N~ested comments", &cfg.nested_comments, 0,
                 "Allow nested comments in source files");
        db_gap(&d);
        db_group(&d, "Keywords", 2);
        db_radio(&d, "~F~lash C", &cfg.ansi_kw, 0,
                 "Allow // comments and recognise near, far, huge, cdecl, pascal and interrupt");
        db_radio(&d, "~A~NSI", &cfg.ansi_kw, 1,
                 "Recognise only ANSI C: no // comments, the extended keywords are ordinary "
                 "identifiers");
        db_end_group(&d);
        db_gap(&d);
        db_num(&d, "~I~dentifier Length", &cfg.ident_len, 6,
               "Enter significant length of identifiers");
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptMsgDisplay:
        msg_dialog_display();
        break;
    case cmOptMsgPortability: {
        const char *n[] = {"~N~on-portable pointer conversion", "Non-portable pointer ~c~omparison",
                           "~M~ixing pointers to signed and unsigned char"};
        int idx[] = {0, 1, 2};
        warn_dialog(" Portability Warnings ", n, idx, 3, 66);
        break;
    }
    case cmOptMsgAnsi: {
        const char *n[] = {"~V~oid functions may not return a value",
                           "~S~uspicious pointer conversion", "Division by ~z~ero"};
        int idx[] = {6, 8, 15};
        warn_dialog(" ANSI Violations ", n, idx, 3, 66);
        break;
    }
    case cmOptMsgFrequent: {
        const char *n[] = {"~F~unction should return a value", "~U~nreachable code",
                           "~C~ode has no effect", "Parameter 'ident' is ~n~ever used",
                           "Possibly incorrect ~a~ssignment"};
        int idx[] = {32, 40, 34, 37, 38};
        warn_dialog(" Frequent Errors ", n, idx, 5, 66);
        break;
    }
    case cmOptMsgLess: {
        const char *n[] = {"~A~mbiguous operators need parentheses", "Structure passed by ~v~alue",
                           "Call to function with no ~p~rototype",
                           "C~o~ndition is always true/false", "'~i~dent' declared but never used"};
        int idx[] = {41, 42, 43, 44, 48};
        warn_dialog(" Less Frequent Errors ", n, idx, 5, 66);
        break;
    }
    case cmOptMake: {
        db_start(&d, " Make ", 62);
        db_check(&d, "~C~heck auto-dependencies", &cfg.check_autodep, 0,
                 "Rebuild when included files change");
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptLinkSettings: {
        db_start(&d, " Linker ", 56);
        db_group(&d, "Map File", 4);
        const char *mn[] = {"O~f~f", "~S~egments", "~P~ublics", "~D~etailed"};
        radios(&d, &cfg.map_file, mn, 4, NULL);
        db_end_group(&d);
        db_check(&d, "Default ~l~ibraries", &cfg.lnk_default_libs, 0,
                 "Link the run-time libraries selected under Linker|Libraries");
        db_gap(&d);
        db_group(&d, "Options", 2);
        db_check(&d, "~E~LF symbol table", &cfg.elf_symtab, 0,
                 "Add .symtab/.strtab so nm, gdb, objdump and perf can show function and variable "
                 "names");
        db_check(&d, "~P~rotect code (W^X)", &cfg.wx_segments, 0,
                 "Put code in a read+execute segment and data in a separate read+write segment");
        db_end_group(&d);
        db_num(&d, "S~t~ack size (KB, 0 = system)", &cfg.stack_kb, 8,
               "Limit for the program stack in kilobytes; 0 keeps the system default (ulimit -s)");
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptLinkLibs: {
        db_start(&d, " Libraries ", 56);
        db_group(&d, "Libraries", 4);
        db_check(&d, "~S~tandard run-time library", &cfg.lib_stdrt, 0,
                 "Link the C library (stdio, string, stdlib, time, io, dir, conio, ...)");
        db_check(&d, "~M~ath library", &cfg.lib_math, 0,
                 "Link the math library and floating-point formatting (math.h, %f, atof)");
        db_check(&d, "~W~ildcard arguments", &cfg.lib_wild, 0,
                 "Expand * and ? in command-line arguments (wildargs)");
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptDebugger: {
        db_start(&d, " Debugger ", 72);
        d.bw = 24;
        db_at(&d, 2, 2, 24);
        db_group(&d, "Source Debugging", 3);
        const char *sn[] = {"~O~n", "~S~tandalone", "~N~one"};
        radios(&d, &cfg.dbg_source, sn, 3, NULL);
        db_end_group(&d);
        db_group(&d, "Display Swapping", 3);
        const char *wn[] = {"No~n~e", "Sma~r~t", "~A~lways"};
        radios(&d, &cfg.dbg_swap, wn, 3, NULL);
        db_end_group(&d);
        db_at(&d, 28, 2, 22);
        db_group(&d, "Number Format", 3);
        db_radio(&d, "Show ~d~ecimal", &cfg.dbg_radix, 0, NULL);
        db_radio(&d, "Show ~h~ex", &cfg.dbg_radix, 1, NULL);
        db_radio(&d, "Show ~b~oth", &cfg.dbg_radix, 2, NULL);
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptDirs: {
        db_start(&d, " Directories ", 72);
        d.bw = 54;
        db_input(&d, "~I~nclude Directories", cfg.incdir, sizeof cfg.incdir, 52,
                 "The directories to search for your include (.h) files");
        db_input(&d, "~O~utput Directory", cfg.outdir, sizeof cfg.outdir, 52,
                 "The output directory for your executable files");
        db_finish(&d, 1);
        for (int i = d.n - 3; i < d.n; i++) {
            d.it[i].x = 30 + (i - (d.n - 3)) * 0;
        }
        d.it[d.n - 3].x = 58;
        d.it[d.n - 3].y = 2 + 0;
        d.it[d.n - 2].x = 58;
        d.it[d.n - 1].x = 58;
        d.w = 72;
        d.it[d.n - 3].y = 3;
        d.it[d.n - 2].y = 5;
        d.it[d.n - 1].y = 7;
        opt_run(&d, NULL);
        break;
    }
    case cmOptPrefs: {
        db_start(&d, " Preferences ", 74);
        d.bw = 26;
        db_at(&d, 2, 2, 26);
        db_group(&d, "Screen Size", 2);
        db_radio(&d, "~2~5 lines", &cfg.screen_lines, 0, "Use the whole terminal");
        db_radio(&d, "~4~3/50 lines", &cfg.screen_lines, 1,
                 "Use a larger display if the terminal has one");
        db_end_group(&d);
        db_group(&d, "Source Tracking", 2);
        db_radio(&d, "~N~ew window", &cfg.src_tracking, 0, "Open a new window to track messages");
        db_radio(&d, "~C~urrent window", &cfg.src_tracking, 1,
                 "Replace the contents of the current window when tracking messages");
        db_end_group(&d);
        if (cfg.cmdset > 1) {
            cfg.cmdset = 1;
        }
        db_group(&d, "Command Set", 2);
        db_radio(&d, "C~U~A", &cfg.cmdset, 0,
                 "Use CUA menus and hot-key selections (Ctrl-X, Ctrl-C, Ctrl-V, Ctrl-Z)");
        db_radio(&d, "~C~lassic", &cfg.cmdset, 1, "Use the classic menus and hot-key selections");
        db_end_group(&d);
        db_at(&d, 30, 2, 26);
        db_group(&d, "Auto-Save", 4);
        db_check(&d, "~E~ditor files", &cfg.as_editor, 0,
                 "Save modified editor files when you run or compile");
        db_check(&d, "En~v~ironment", &cfg.as_env, 0, "Save config file when you quit or run");
        db_check(&d, "~D~esktop", &cfg.as_desktop, 0, "Save desktop when you quit or run");
        db_check(&d, "~P~roject", &cfg.as_project, 0, "Save project when you quit or run");
        db_end_group(&d);
        db_check(&d, "~S~ave old messages", &cfg.save_old_msgs, 0,
                 "Preserve messages before a compile or make");
        db_check(&d, "Pa~u~se after run", &cfg.pause_after_run, 0,
                 "Wait for a key after the program ends");
        db_finish(&d, 1);
        int old = cfg.screen_lines;
        if (opt_run(&d, NULL) && old != cfg.screen_lines) {
            set_screen_lines(cfg.screen_lines ? 0 : 25);
            wm_resize();
        }
        break;
    }
    case cmOptEditor: {
        db_start(&d, " Editor Options ", 66);
        d.bw = 46;
        db_group(&d, "Editor Options", 9);
        db_check(&d, "Create backup ~f~iles", &cfg.backup, 0,
                 "Keep the previous version as file~ whenever you save");
        db_check(&d, "~I~nsert mode", &cfg.insert, 0,
                 "Characters typed are inserted into edit text");
        db_check(&d, "~A~utoindent mode", &cfg.autoindent, 0,
                 "Auto-indent to previous indentation level on each new line");
        db_check(&d, "~U~se tab character", &cfg.use_tab, 0,
                 "Insert actual tab characters into file");
        db_check(&d, "~S~yntax Highlighting", &cfg.syntax, 0,
                 "Syntax elements are highlighted in editor windows");
        db_check(&d, "~B~ackspace unindents", &cfg.bs_unindent, 0,
                 "The Backspace key will unindent, based on previous starting columns");
        db_check(&d, "~G~roup undo", &cfg.group_undo, 0,
                 "Group similar editor actions into one Undo action");
        db_check(&d, "~P~ersistent blocks", &cfg.persist_blocks, 0,
                 "Selected text remains highlighted when cursor moves");
        db_check(&d, "Overwrite b~l~ocks", &cfg.overwrite_blocks, 0,
                 "Inserting new text deletes (overwrites) selected text");
        db_end_group(&d);
        db_num(&d, "~T~ab Size", &cfg.tabsize, 6,
               "Change the number of columns to use for tab width");
        db_input(&d, "~D~efault Extension", cfg.defext, sizeof cfg.defext, 8,
                 "Use this file extension if you don't provide one");
        db_finish(&d, 1);
        opt_run(&d, "The editor");
        break;
    }
    case cmOptMouse: {
        db_start(&d, " Mouse Options ", 62);
        d.bw = 30;
        db_group(&d, "Right Mouse Button", 10);
        const char *n[] = {"~N~othing",  "~T~opic search", "~S~earch",     "Search ~a~gain",
                           "~R~eplace",  "~G~o to cursor", "~B~reakpoint", "~I~nspect",
                           "~E~valuate", "Add ~w~atch"};
        radios(&d, &cfg.mouse_right, n, 10, NULL);
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptDesktop: {
        db_start(&d, " Desktop Preferences ", 56);
        db_group(&d, "Save Options", 3);
        db_check(&d, "~W~atch expressions", &cfg.sv_watch, 0, "Save all watch expressions");
        db_check(&d, "~B~reakpoints", &cfg.sv_bp, 0, "Save all breakpoints");
        db_check(&d, "~O~pen windows", &cfg.sv_open, 0,
                 "Save state information for all open windows");
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptSave: {
        if (cfg_save()) {
            msgbox(" Error ", "Cannot write the configuration file", MB_OK | MB_ERROR);
        } else {
            infobox(" Save Options ", "Options saved to\n%s", cfg_path());
        }
        break;
    }
    case cmOptColors:
        dlg_colors();
        break;
    case cmOptTransfer:
        dlg_transfer();
        break;
    }
}

/* ---------- search dialogs ---------- */
int dlg_find_text(int replace)
{
    Dialog d;
    dlg_init(&d, replace ? " Replace Text " : " Find Text ");
    d.w = replace ? 66 : 62;
    d.h = replace ? 20 : 17;
    int r = 0;
    dlg_input(&d, 3, 1, 44, "~T~ext to Find", find_text, sizeof find_text,
              "Enter literal text or regular expression to search for");
    int y = 3;
    if (replace) {
        dlg_input(&d, 3, 4, 44, "~N~ew Text", repl_text, sizeof repl_text,
                  "Enter new text that will replace matching text");
        y = 6;
    }
    dlg_frame(&d, 3, y + 1, 26, replace ? 6 : 5, "Options");
    dlg_check(&d, 5, y + 2, "~C~ase sensitive", &find_opts[0], 0,
              "Case of letters is significant during search");
    dlg_check(&d, 5, y + 3, "~W~hole words only", &find_opts[1], 0,
              "Consider only whole words during search");
    dlg_check(&d, 5, y + 4, "~R~egular expression", &find_opts[2], 0,
              "Interpret search text as a regular expression");
    if (replace) {
        dlg_check(&d, 5, y + 5, "~P~rompt on replace", &find_opts[3], 0,
                  "Prompt before replacing text after every match");
    }
    dlg_frame(&d, 31, y + 1, 18, 4, "Direction");
    dlg_radio(&d, 33, y + 2, "Forwar~d~", &find_opts[4], 0, "Search forward from origin");
    dlg_radio(&d, 33, y + 3, "~B~ackward", &find_opts[4], 1, "Search backward from origin");
    int y2 = y + (replace ? 7 : 6);
    dlg_frame(&d, 3, y2, 26, 4, "Scope");
    dlg_radio(&d, 5, y2 + 1, "~G~lobal", &find_opts[5], 0,
              "Search from origin to the end of the edit buffer");
    dlg_radio(&d, 5, y2 + 2, "~S~elected text", &find_opts[5], 1,
              "Search current selected block of edit buffer");
    dlg_frame(&d, 31, y2, 18, 4, "Origin");
    dlg_radio(&d, 33, y2 + 1, "~F~rom cursor", &find_opts[6], 0,
              "Start search from current cursor position");
    dlg_radio(&d, 33, y2 + 2, "~E~ntire scope", &find_opts[6], 1,
              "Start search at beginning of scope");
    int bx = d.w - 16;
    dlg_button(&d, bx, 2, 14, "OK", cmOK, 1, "Accept the settings in this dialog box");
    if (replace) {
        dlg_button(&d, bx, 5, 14, "Change ~A~ll", cmChangeAll, 0,
                   "Find all occurrences of the search text and replace them");
    }
    dlg_button(&d, bx, replace ? 8 : 5, 14, "Cancel", cmCancel, 0,
               "Close the dialog box without making any changes");
    dlg_button(&d, bx, replace ? 11 : 8, 14, "Help", cmHelp, 0,
               "View a Help screen about this dialog box");
    d.it[0].selall = 1;
    do {
        r = dlg_run(&d);
        if (r == cmHelp) {
            help_show("The editor");
        }
    } while (r == cmHelp);
    return r;
}

/* Asks for a line number; returns true if the user accepted. */
int dlg_goto_line(int *line)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%d", *line);
    Dialog d;
    dlg_init(&d, " Go to Line Number ");
    d.w = 40;
    d.h = 9;
    dlg_input(&d, 3, 1, 20, "~E~nter New Line Number", buf, 12,
              "Enter Edit window line number to position cursor on");
    dlg_button(&d, 4, 5, 10, "OK", cmOK, 1, NULL);
    dlg_button(&d, 16, 5, 10, "Cancel", cmCancel, 0, NULL);
    if (dlg_run(&d) != cmOK) {
        return 0;
    }
    *line = atoi(buf);
    return 1;
}

/* Asks for a function name for Search|Locate function; returns true if accepted. */
int dlg_locate(char *name, int cap)
{
    return inputbox(" Locate Function ", "~F~unction Name", name, cap,
                    "Enter name of function to locate");
}

/* Shows the About box. */
void dlg_about(void)
{
    static const char *lines[] = {"Flash C",
                                  "Version " FC_VERSION,
                                  "",
                                  "A C compiler and text-mode IDE for Linux",
                                  "native x86-64 code, no external tools",
                                  "",
                                  "Author: Dilshan R Jayakody",
                                  "E-mail: jayakody2000lk@gmail.com",
                                  "",
                                  "Press Alt-F1 for the help index"};
    Dialog d;
    dlg_init(&d, " About ");
    d.w = 54;
    d.h = 16;
    for (int i = 0; i < (int)(sizeof lines / sizeof *lines); i++) {
        if (*lines[i]) {
            dlg_static(&d, (d.w - (int)strlen(lines[i])) / 2, 2 + i, lines[i]);
        }
    }
    dlg_button(&d, (d.w - 12) / 2, 13, 12, "  OK  ", cmOK, 1, NULL);
    dlg_run(&d);
}

/* Shows the result of the last compilation (Compile|Information). */
void dlg_compiler_info(int lines, int errors, int warnings)
{
    char cwd[512] = ".";
    if (!getcwd(cwd, sizeof cwd)) {
        cwd[0] = 0;
    }
    Win *w = win_active();
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
    char buf[900];
    snprintf(buf, sizeof buf,
             "Current directory : %.60s\n"
             "Current file      : %.60s\n"
             "Available memory  : %ld KB\n\n"
             "Lines compiled    : %d\n"
             "Total warnings    : %d\n"
             "Total errors      : %d",
             cwd, w && w->kind == W_EDIT ? w->buf->name : "", avail, lines, warnings, errors);
    msgbox(" Information ", buf, MB_OK);
}

/* ---------- window list ---------- */
int dlg_window_list(int *action)
{
    static char *items[MAXWIN];
    static char store[MAXWIN][120];
    Dialog d;
    dlg_init(&d, " Window List ");
    d.w = 52;
    d.h = 17;
    int n = 0, sel = 0;
    for (int i = nwins - 1; i >= 0; i--) {
        Win *w = wins[i];
        snprintf(store[n], sizeof store[n], "%c %s", w->num ? '0' + w->num : ' ',
                 w->title[0] ? w->title : "(untitled)");
        items[n] = store[n];
        n++;
    }
    DItem *l = dlg_list(&d, 3, 2, 44, 10, items, n,
                        "Use cursor keys to examine windows in windows list box");
    l->sel = sel;
    dlg_button(&d, 4, 13, 10, "~O~k", cmOK, 1, NULL);
    dlg_button(&d, 16, 13, 12, "~C~lose", cmDelete, 0,
               "Remove the selected entry from the windows list box");
    dlg_button(&d, 30, 13, 10, "Cancel", cmCancel, 0, NULL);
    int r = dlg_run(&d);
    *action = r == cmDelete;
    if (r != cmOK && r != cmDelete) {
        return -1;
    }
    return nwins - 1 - d.it[0].sel;
}

/* ---------- evaluate / modify ---------- */
static char ev_expr[256], ev_result[256], ev_new[256];

/* Draws the result line of the Evaluate dialog. */
static void ev_draw(Dialog *d, DItem *it, int foc)
{
    scr_fill(d->x + it->x, d->y + it->y, it->w, 1, ' ', pal[P_DLG_INPUT]);
    scr_strn(d->x + it->x, d->y + it->y, ev_result, it->w, pal[P_DLG_INPUT]);
}

/* Handles the buttons of the Evaluate/Modify dialog. */
static int ev_cmd(Dialog *d, int cmd)
{
    if (cmd == cmEvaluate || cmd == cmOK) {
        long long v;
        char err[160];
        if (!ev_expr[0]) {
            return 1;
        }
        {
            int k = dbgui_eval(ev_expr, ev_result, sizeof ev_result);
            if (k != -2) {
                return 1;
            }
        }
        if (!fc_eval_expr(ev_expr, &v, err, sizeof err)) {
            snprintf(ev_result, sizeof ev_result, "%lld", v);
            if (v >= 0 && v < 0x100000000LL) {
                char hx[40];
                snprintf(hx, sizeof hx, "  (0x%llX)", v);
                strcat(ev_result, hx);
            }
            if (v >= 32 && v < 127) {
                char c[16];
                snprintf(c, sizeof c, "  '%c'", (char)v);
                strcat(ev_result, c);
            }
        } else {
            snprintf(ev_result, sizeof ev_result, "%s", err);
        }
        return 1;
    }
    if (cmd == cmModify) {
        if (!dbgui_active()) {
            msgbox(" Evaluate and Modify ",
                   "Variables can only be modified while a\nprogram is being debugged.", MB_OK);
            return 1;
        }
        if (!ev_expr[0] || !ev_new[0]) {
            return 1;
        }
        dbgui_assign(ev_expr, ev_new, ev_result, sizeof ev_result);
        return 1;
    }
    return 0;
}

/* Shows the Debug|Evaluate/Modify dialog. */
void dlg_eval(void)
{
    Dialog d;
    dlg_init(&d, " Evaluate and Modify ");
    d.w = 66;
    d.h = 16;
    d.on_cmd = ev_cmd;
    dlg_input(&d, 3, 1, 56, "E~x~pression", ev_expr, sizeof ev_expr,
              "Enter expression to evaluate");
    DItem lab = {.type = D_LABEL, .x = 3, .y = 4, .text = "~R~esult"};
    dlg_add(&d, lab);
    DItem res = {.type = D_CUSTOM, .x = 3, .y = 5, .w = 56, .h = 1, .draw = ev_draw};
    dlg_add(&d, res);
    dlg_input(&d, 3, 7, 56, "~N~ew Value", ev_new, sizeof ev_new, "Enter new value for expression");
    dlg_button(&d, 3, 11, 12, "~E~valuate", cmEvaluate, 1, "Evaluate expression");
    dlg_button(&d, 17, 11, 12, "~M~odify", cmModify, 0, "Modify expression");
    dlg_button(&d, 31, 11, 12, "Cancel", cmCancel, 0, NULL);
    dlg_button(&d, 45, 11, 12, "Help", cmHelp, 0, NULL);
    ev_result[0] = 0;
    dlg_run(&d);
}

/* ---------- transfer items ---------- */
static void xfer_edit(TransferItem *t)
{
    Dialog d;
    dlg_init(&d, " Modify/New Transfer Item ");
    d.w = 64;
    d.h = 15;
    dlg_input(&d, 3, 1, 56, "Program ~T~itle", t->title, sizeof t->title,
              "Enter title you want assigned to the Transfer item (use ~x~ for the hot key)");
    dlg_input(&d, 3, 4, 56, "Program ~P~ath", t->path, sizeof t->path,
              "Enter the path and program name associated with this item");
    dlg_input(&d, 3, 7, 56, "~C~ommand Line", t->cmd, sizeof t->cmd,
              "Enter the parameterized command line ($NAME, $EDNAME, $PROMPT)");
    dlg_button(&d, 16, 11, 10, "OK", cmOK, 1, NULL);
    dlg_button(&d, 30, 11, 10, "Cancel", cmCancel, 0, NULL);
    dlg_run(&d);
}

/* Shows the Options|Transfer dialog that lists the user-defined menu programs. */
void dlg_transfer(void)
{
    static char *items[12];
    static char store[12][80];
    for (;;) {
        Dialog d;
        dlg_init(&d, " Transfer ");
        d.w = 60;
        d.h = 17;
        for (int i = 0; i < cfg.nxfer; i++) {
            int k = 0;
            for (const char *p = cfg.xfer[i].title; *p && k < 78; p++) {
                if (*p != '~') {
                    store[i][k++] = *p;
                }
            }
            store[i][k] = 0;
            items[i] = store[i];
        }
        DItem *l = dlg_list(&d, 3, 3, 38, 8, items, cfg.nxfer,
                            "Use cursor keys to examine the list of Transfer items");
        DItem lab = {.type = D_LABEL, .x = 3, .y = 2, .text = "Program ~T~itles"};
        dlg_add(&d, lab);
        l->link = d.n - 1;
        dlg_button(&d, 42, 3, 14, "~E~dit", cmEdit, 0,
                   "View and modify the description of the selected Transfer item");
        dlg_button(&d, 42, 5, 14, "~D~elete", cmDelete, 0, "Delete the selected Transfer item");
        dlg_button(&d, 42, 7, 14, "~N~ew", cmNew, 0, "Create a new Transfer item");
        dlg_button(&d, 42, 9, 14, "OK", cmOK, 1, NULL);
        dlg_button(&d, 42, 11, 14, "Cancel", cmCancel, 0, NULL);
        int r = dlg_run(&d), sel = l->sel;
        if (r == cmEdit && cfg.nxfer) {
            xfer_edit(&cfg.xfer[sel]);
        } else if (r == cmDelete && cfg.nxfer) {
            memmove(&cfg.xfer[sel], &cfg.xfer[sel + 1],
                    (cfg.nxfer - sel - 1) * sizeof(TransferItem));
            cfg.nxfer--;
        } else if (r == cmNew && cfg.nxfer < 12) {
            TransferItem *t = &cfg.xfer[cfg.nxfer];
            memset(t, 0, sizeof *t);
            xfer_edit(t);
            if (t->title[0]) {
                cfg.nxfer++;
            }
        } else {
            break;
        }
    }
}

/* ---------- colors ---------- */
static int col_group, col_item, col_fg, col_bg;
static char *grp_names[P_COUNT];
static int ngrp;
static char *item_names[P_COUNT];
static int item_idx[P_COUNT];
static int nitem;

/* Builds the group and item lists of the Colors dialog. */
static void col_fill_items(void)
{
    nitem = 0;
    for (int i = 0; i < P_COUNT; i++) {
        if (!strcmp(palinfo[i].group, grp_names[col_group])) {
            item_names[nitem] = (char *)palinfo[i].item;
            item_idx[nitem++] = i;
        }
    }
}

/* Selects the foreground/background entries that match the current item. */
static void col_sync(Dialog *d)
{
    DItem *gl = &d->it[0], *il = &d->it[1];
    if (gl->sel != col_group) {
        col_group = gl->sel;
        col_fill_items();
        il->items = item_names;
        il->nitems = nitem;
        il->sel = il->top = 0;
    }
    col_item = il->sel;
    int idx = item_idx[col_item];
    col_fg = pal[idx] & 15;
    col_bg = (pal[idx] >> 4) & 7;
}

/* Called when another colour item is chosen. */
static void col_change(Dialog *d)
{
    col_sync(d);
}

/* Draws the foreground colour chooser. */
static void col_draw_fg(Dialog *d, DItem *it, int foc)
{
    for (int i = 0; i < 16; i++) {
        uint8_t a = (i << 0) | (i == 0 ? 0x70 : 0);
        scr_put(d->x + it->x + i * 2, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        scr_put(d->x + it->x + i * 2 + 1, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        if (i == col_fg) {
            scr_put(d->x + it->x + i * 2, d->y + it->y + 1, 0x25B2, pal[P_DLG_NORM]);
        }
        (void)a;
    }
    if (foc) {
        scr_cursor(d->x + it->x + col_fg * 2, d->y + it->y + 1, 1);
    }
}

/* Draws the background colour chooser. */
static void col_draw_bg(Dialog *d, DItem *it, int foc)
{
    for (int i = 0; i < 8; i++) {
        scr_put(d->x + it->x + i * 2, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        scr_put(d->x + it->x + i * 2 + 1, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        if (i == col_bg) {
            scr_put(d->x + it->x + i * 2, d->y + it->y + 1, 0x25B2, pal[P_DLG_NORM]);
        }
    }
    if (foc) {
        scr_cursor(d->x + it->x + col_bg * 2, d->y + it->y + 1, 1);
    }
}

/* Applies the edited palette to the screen. */
static void col_apply(void)
{
    pal[item_idx[col_item]] = (col_bg << 4) | col_fg;
}

/* Keyboard handling of the foreground colour chooser. */
static int col_key_fg(Dialog *d, DItem *it, int key)
{
    int b = key & KMASK;
    if (b == KEY_LEFT && col_fg > 0) {
        col_fg--;
    } else if (b == KEY_RIGHT && col_fg < 15) {
        col_fg++;
    } else {
        return 0;
    }
    col_apply();
    return 1;
}

/* Keyboard handling of the background colour chooser. */
static int col_key_bg(Dialog *d, DItem *it, int key)
{
    int b = key & KMASK;
    if (b == KEY_LEFT && col_bg > 0) {
        col_bg--;
    } else if (b == KEY_RIGHT && col_bg < 7) {
        col_bg++;
    } else {
        return 0;
    }
    col_apply();
    return 1;
}

/* Draws a sample of the selected colour combination. */
static void col_draw_sample(Dialog *d, DItem *it, int foc)
{
    uint8_t a = pal[item_idx[col_item]];
    scr_fill(d->x + it->x, d->y + it->y, it->w, 3, ' ', a);
    scr_str(d->x + it->x + 2, d->y + it->y + 1, "Text", a);
}

/* Shows the Options|Environment|Colors dialog. */
void dlg_colors(void)
{
    ngrp = 0;
    for (int i = 0; i < P_COUNT; i++) {
        int seen = 0;
        for (int k = 0; k < ngrp; k++) {
            if (!strcmp(grp_names[k], palinfo[i].group)) {
                seen = 1;
            }
        }
        if (!seen) {
            grp_names[ngrp++] = (char *)palinfo[i].group;
        }
    }
    col_group = 0;
    col_fill_items();
    uint8_t bak[P_COUNT];
    memcpy(bak, pal, sizeof pal);
    Dialog d;
    dlg_init(&d, " Colors ");
    d.w = 66;
    d.h = 20;
    d.on_change = col_change;
    dlg_list(&d, 3, 3, 24, 10, grp_names, ngrp, "Chooses general color group to modify");
    dlg_list(&d, 30, 3, 24, 10, item_names, nitem,
             "Chooses specific item in color group to modify");
    DItem l1 = {.type = D_LABEL, .x = 3, .y = 2, .text = "~G~roup"};
    dlg_add(&d, l1);
    d.it[0].link = d.n - 1;
    DItem l2 = {.type = D_LABEL, .x = 30, .y = 2, .text = "~I~tem"};
    dlg_add(&d, l2);
    d.it[1].link = d.n - 1;
    dlg_static(&d, 3, 14, "~F~oreground");
    DItem fg = {.type = D_CUSTOM,
                .x = 3,
                .y = 15,
                .w = 32,
                .h = 2,
                .draw = col_draw_fg,
                .onkey = col_key_fg,
                .hint = "Sets the foreground color for selected item (left / right)"};
    dlg_add(&d, fg);
    dlg_static(&d, 38, 14, "~B~ackground");
    DItem bg = {.type = D_CUSTOM,
                .x = 38,
                .y = 15,
                .w = 16,
                .h = 2,
                .draw = col_draw_bg,
                .onkey = col_key_bg,
                .hint = "Sets the background color for selected item (left / right)"};
    dlg_add(&d, bg);
    DItem sm = {.type = D_CUSTOM, .x = 56, .y = 14, .w = 8, .h = 3, .draw = col_draw_sample};
    dlg_add(&d, sm);
    dlg_button(&d, 56, 3, 8, "OK", cmOK, 1, NULL);
    dlg_button(&d, 56, 6, 8, "Cancel", cmCancel, 0, NULL);
    col_sync(&d);
    int r = dlg_run(&d);
    if (r != cmOK) {
        memcpy(pal, bak, sizeof pal);
    } else {
        memcpy(cfg.colors, pal, sizeof pal);
    }
}

/* ---------- project item dialogs ---------- */
int dlg_local_options(const char *file, ProjItem *item)
{
    char cmd[sizeof item->cmd], out[sizeof item->out];
    int nodebug = item->nodebug, nolink = item->nolink;
    snprintf(cmd, sizeof cmd, "%s", item->cmd);
    snprintf(out, sizeof out, "%s", item->out);
    Dialog d;
    dlg_init(&d, " Local Options ");
    d.w = 64;
    d.h = 17;
    char t[300];
    snprintf(t, sizeof t, "Project Item: %s", file);
    dlg_static(&d, 3, 1, t);
    dlg_input(
        &d, 3, 3, 56, "~C~ommand Line Options", cmd, sizeof cmd,
        "Compiler switches for this item, for example -O -DDEBUG; they apply to the whole build");
    dlg_input(&d, 3, 6, 56, "Output ~P~ath", out, sizeof out,
              "The executable file or directory used when this item is built (empty: default)");
    dlg_check(&d, 3, 9, "~E~xclude debug information", &nodebug, 0,
              "Chooses whether debug information should be generated for this item");
    dlg_check(&d, 3, 10, "Exclude from ~l~ink", &nolink, 0,
              "Chooses whether this item should be compiled into the program");
    dlg_button(&d, 16, 13, 10, "OK", cmOK, 1, NULL);
    dlg_button(&d, 30, 13, 10, "Cancel", cmCancel, 0, NULL);
    if (dlg_run(&d) != cmOK) {
        return 0;
    }
    int changed = strcmp(cmd, item->cmd) || strcmp(out, item->out) || nodebug != item->nodebug ||
                  nolink != item->nolink;
    snprintf(item->cmd, sizeof item->cmd, "%s", cmd);
    snprintf(item->out, sizeof item->out, "%s", out);
    item->nodebug = nodebug;
    item->nolink = nolink;
    return changed;
}

/* Shows the files that the given source file includes (Project|Include files). */
void dlg_include_files(const char *file)
{
    static char *items[200];
    static char store[200][160];
    int n = 0;
    FILE *f = fopen(file, "r");
    if (f) {
        char line[512];
        while (n < 200 && fgets(line, sizeof line, f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p != '#') {
                continue;
            }
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (strncmp(p, "include", 7)) {
                continue;
            }
            p += 7;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            char close = *p == '<' ? '>' : '"';
            if (*p != '<' && *p != '"') {
                continue;
            }
            char *e = strchr(p + 1, close);
            if (!e) {
                continue;
            }
            snprintf(store[n], sizeof store[n], "%.*s", (int)(e - p - 1), p + 1);
            items[n] = store[n];
            n++;
        }
        fclose(f);
    }
    Dialog d;
    dlg_init(&d, " Include Files ");
    d.w = 56;
    d.h = 17;
    char t[300];
    snprintf(t, sizeof t, "Include files for %s", file);
    dlg_static(&d, 3, 1, t);
    if (!n) {
        items[0] = "(none)";
        n = 1;
    }
    dlg_list(&d, 3, 3, 36, 9, items, n,
             "Use cursor keys to examine list of dependent (include) files");
    dlg_button(&d, 42, 3, 10, "~V~iew", cmView, 1,
               "Load the selected include file into an Edit window for viewing");
    dlg_button(&d, 42, 5, 10, "Cancel", cmCancel, 0, NULL);
    int r = dlg_run(&d);
    if (r == cmView && strcmp(items[d.it[1].sel], "(none)")) {
        char path[600];
        const char *sl = strrchr(file, '/');
        if (sl) {
            snprintf(path, sizeof path, "%.*s/%s", (int)(sl - file), file, items[d.it[1].sel]);
        } else {
            snprintf(path, sizeof path, "%s", items[d.it[1].sel]);
        }
        if (file_exists(path)) {
            win_open_file(path);
        } else {
            infobox(" Include Files ", "%s is not in the source directory", items[d.it[1].sel]);
        }
    }
}
