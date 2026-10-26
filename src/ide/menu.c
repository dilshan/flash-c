/* Flash C IDE - menu bar and pulldown menus (structure recovered from the reference IDE) */
#include "ide.h"
#include <ctype.h>

#define SEP { NULL, NULL, 0, NULL, NULL }
#define IT(t, k, c, h) { t, k, c, h, NULL }
#define SUB(t, m, h) { t, NULL, 0, h, &m }

static const MItem file_items[] = {
    IT("~N~ew", NULL, cmNewFile, "Create a new file in a new Edit window"),
    IT("~O~pen...", "F3", cmOpenFile, "Locate and open a file"),
    IT("~S~ave", "F2", cmSaveFile, "Save the file in the active Edit window"),
    IT("S~a~ve as...", NULL, cmSaveAs, "Save the file in the active window under a new name"),
    IT("Save a~l~l", NULL, cmSaveAll, "Save all modified files"),
    SEP,
    IT("~C~hange dir...", NULL, cmChangeDir, "Change the current working directory"),
    IT("~P~rint", NULL, cmPrint, "Print the contents of the active window"),
    IT("~D~OS shell", NULL, cmDosShell, "Temporarily exit to the shell"),
    SEP,
    IT("~Q~uit", "Alt+X", cmQuit, "Exit Flash C"),
};
static const MItem edit_items[] = {
    IT("~U~ndo", "Alt+BkSp", cmUndo, "Undo the previous editor action"),
    IT("~R~edo", "Shift+Alt+BkSp", cmRedo, "Redo the previously undone editor action"),
    SEP,
    IT("Cu~t~", "Shift+Del", cmCut, "Remove the selected text and put it in the Clipboard"),
    IT("~C~opy", "Ctrl+Ins", cmCopy, "Copy the selected text and put it in the Clipboard"),
    IT("~P~aste", "Shift+Ins", cmPaste, "Insert selected text from the Clipboard at the cursor position"),
    IT("C~l~ear", "Ctrl+Del", cmClear, "Delete the selected text"),
    SEP,
    IT("Copy e~x~ample", NULL, cmCopyExample, "Copy program example from the Help window into the Clipboard"),
    IT("Show c~l~ipboard", NULL, cmShowClip, "Open the Clipboard window"),
};
static const MItem search_items[] = {
    IT("~F~ind...", NULL, cmFind, "Search for text"),
    IT("~R~eplace...", NULL, cmReplaceText, "Search for text and replace it with new text"),
    IT("~S~earch again", "Ctrl+L", cmSearchAgain, "Repeat the last Find or Replace operation"),
    IT("~G~o to line number...", NULL, cmGotoLine, "Move the cursor to a specific line number"),
    IT("~P~revious error", "Alt+F7", cmPrevError, "Move the cursor to the position of the previous message"),
    IT("~N~ext error", "Alt+F8", cmNextError, "Move the cursor to the position of the next message"),
    IT("~L~ocate function...", NULL, cmLocateFn, "Search for a function definition"),
};
static const MItem run_items[] = {
    IT("~R~un", "Ctrl+F9", cmRun, "Make and run the current program"),
    IT("~P~rogram reset", "Ctrl+F2", cmProgReset, "Restart the program from beginning"),
    IT("~G~o to cursor", "F4", cmGotoCursor, "Run the program from the run bar to the cursor position"),
    IT("~T~race into", "F7", cmTraceInto, "Execute next statement; stop within the current function"),
    IT("~S~tep over", "F8", cmStepOver, "Execute next statement; skip over the current function"),
    IT("~A~rguments...", NULL, cmArguments, "Set command-line arguments to be passed to program"),
};
static const MItem compile_items[] = {
    IT("~C~ompile", "Alt+F9", cmCompile, "Compile the file in the active Edit window"),
    IT("~M~ake", "F9", cmMake, "Bring target up-to-date by compiling and linking, as necessary"),
    IT("~L~ink", NULL, cmLink, "Link target without recompiling"),
    IT("~B~uild all", NULL, cmBuildAll, "Rebuild all files"),
    SEP,
    IT("~I~nformation...", NULL, cmInformation, "Display compiler information"),
    IT("~R~emove messages", NULL, cmRemoveMsgs, "Clear all messages from the Message window"),
};
static const MItem watch_items[] = {
    IT("~A~dd watch...", "Ctrl+F7", cmAddWatch, "Insert a watch expression into the Watch window"),
    IT("~D~elete watch", NULL, cmDelWatch, "Remove the current watch expression from the Watch window"),
    IT("~E~dit watch...", NULL, cmEditWatch, "Edit the current watch expression in the Watch window"),
    IT("~R~emove all watches", NULL, cmRemoveWatches, "Delete all watch expressions from the Watch window"),
};
static const MMenu watch_menu = { "Watches", watch_items, 4 };
static const MItem debug_items[] = {
    IT("~I~nspect...", "Alt+F4", cmInspect, "Open an Inspector window to examine values in a data element"),
    IT("~E~valuate/modify...", "Ctrl+F4", cmEvalMod, "Evaluate a variable or expression and display the value"),
    IT("~C~all stack...", "Ctrl+F3", cmCallStack, "Show the functions the program called to reach this point"),
    SEP,
    SUB("~W~atches", watch_menu, "Add, delete, and edit watches"),
    IT("~T~oggle breakpoint", "Ctrl+F8", cmToggleBP, "Set or clear an unconditional breakpoint at the cursor position"),
    IT("~B~reakpoints...", NULL, cmBreakpoints, "Set conditional breakpoints; view and edit breakpoints"),
};
static const MItem project_items[] = {
    IT("~O~pen project...", NULL, cmOpenProject, "Load a project file and its desktop"),
    IT("~C~lose project", NULL, cmCloseProject, "Close the current project"),
    SEP,
    IT("~A~dd item...", NULL, cmAddItem, "Add an item to the project"),
    IT("~D~elete item", NULL, cmDelItem, "Delete an item from the project"),
    IT("~L~ocal options...", NULL, cmLocalOpts, "Set or view options for a project item"),
    IT("~I~nclude files...", NULL, cmIncludeFiles, "View the include files for a project item"),
};
static const MItem msg_items[] = {
    IT("~D~isplay...", NULL, cmOptMsgDisplay, "Configure error and warning display"),
    IT("~P~ortability...", NULL, cmOptMsgPortability, "Choose portability messages to display"),
    IT("~A~NSI violations...", NULL, cmOptMsgAnsi, "Choose ANSI-violation messages to display"),
    IT("~F~requent errors...", NULL, cmOptMsgFrequent, "Choose frequently encountered messages to display"),
    IT("~L~ess frequent errors...", NULL, cmOptMsgLess, "Choose less common messages to display"),
};
static const MMenu msg_menu = { "Messages", msg_items, 5 };
static const MItem comp_opt_items[] = {
    IT("~C~ode generation...", NULL, cmOptCodeGen, "Specify how the compiler prepares object code (memory models, etc.)"),
    IT("~A~dvanced code generation...", NULL, cmOptAdvCodeGen, "View further code generation options"),
    IT("~E~ntry/Exit Code...", NULL, cmOptEntryExit, "Change the entry/exit code generated for functions"),
    IT("~O~ptimizations...", NULL, cmOptOptimize, "Specify compiler optimizations"),
    IT("~S~ource...", NULL, cmOptSource, "Specify source code related options"),
    SUB("~M~essages", msg_menu, "Specify how the IDE handles errors and warnings"),
    IT("~N~ames...", NULL, cmOptNames, "Change the default segment, group, and class names"),
};
static const MMenu comp_opt_menu = { "Compiler", comp_opt_items, 7 };
static const MItem linker_items[] = {
    IT("~S~ettings...", NULL, cmOptLinkSettings, "Set linker options (map files, overlays, etc.)"),
    IT("~L~ibraries...", NULL, cmOptLinkLibs, "Set libraries to be included in link"),
};
static const MMenu linker_menu = { "Linker", linker_items, 2 };
static const MItem env_items[] = {
    IT("~P~references...", NULL, cmOptPrefs, "Specify environment settings"),
    IT("~E~ditor...", NULL, cmOptEditor, "Specify editor settings"),
    IT("~M~ouse...", NULL, cmOptMouse, "Specify mouse settings"),
    IT("~D~esktop...", NULL, cmOptDesktop, "Specify desktop settings"),
    IT("~S~tartup...", NULL, cmOptStartup, "Specify environment startup settings"),
    IT("~C~olors...", NULL, cmOptColors, "Set environment colors"),
};
static const MMenu env_menu = { "Environment", env_items, 6 };
static const MItem options_items[] = {
    IT("~A~pplication...", NULL, cmOptApplication, "Set application options"),
    SUB("~C~ompiler", comp_opt_menu, "Set compiler defaults for code generation, error messages, and names"),
    IT("~T~ransfer...", NULL, cmOptTransfer, "Create or change Transfer programs"),
    IT("~M~ake...", NULL, cmOptMake, "Set make options"),
    SUB("~L~inker", linker_menu, "Set linker settings and libraries to be linked"),
    IT("Li~b~rarian...", NULL, cmOptLibrarian, "Set librarian options"),
    IT("~D~ebugger...", NULL, cmOptDebugger, "Set debugging options (display swapping, inspector options, etc.)"),
    IT("Directo~r~ies...", NULL, cmOptDirs, "Set paths for include files, libraries, and binary output files"),
    SUB("~E~nvironment", env_menu, "Set environment, editor, mouse, and desktop preferences"),
    IT("~S~ave...", NULL, cmOptSave, "Save all the options"),
};
static const MItem window_items[] = {
    IT("~S~ize/Move", "Ctrl+F5", cmSizeMove, "Change the size or position of the active window"),
    IT("~Z~oom", "F5", cmZoom, "Enlarge or restore the size of the active window"),
    IT("~T~ile", NULL, cmTile, "Tile windows on the desktop"),
    IT("C~a~scade", NULL, cmCascade, "Cascade windows on the desktop"),
    IT("~N~ext", "F6", cmNextWin, "Make the next window active"),
    IT("~C~lose", "Alt+F3", cmCloseWin, "Close the active window"),
    IT("Close a~l~l", NULL, cmCloseAll, "Close all windows on the desktop"),
    SEP,
    IT("~M~essage", NULL, cmWinMessage, "Open the Message window"),
    IT("~O~utput", NULL, cmWinOutput, "Open the Output window"),
    IT("~W~atch", NULL, cmWinWatch, "Open the Watch window"),
    IT("~U~ser screen", "Alt+F5", cmUserScreen, "Switch to the full-screen user output"),
    IT("~R~egister", NULL, cmWinRegister, "Open the Register window"),
    IT("~P~roject", NULL, cmWinProject, "Open the Project Manager window"),
    IT("Project n~o~tes", NULL, cmWinNotes, "Open the Project Manager Notebook window"),
    SEP,
    IT("~L~ist all...", "Alt+0", cmWinList, "Show a complete list of windows"),
};
static const MItem help_items[] = {
    IT("~C~ontents", NULL, cmHelpContents, "Show table of contents for online Help"),
    IT("~I~ndex", "Shift+F1", cmHelpIndex, "Show index for online Help"),
    IT("~T~opic search", "Ctrl+F1", cmHelpTopic, "Display Help for the word at the cursor, or display the index"),
    IT("~P~revious topic", "Alt+F1", cmHelpPrev, "Redisplay the last-viewed online Help screen"),
    IT("~H~elp on help", NULL, cmHelpOnHelp, "How to use online Help"),
};

static MItem sys_items[16] = {
    IT("~A~bout...", NULL, cmAbout, "Show version and copyright information"),
    SEP,
    IT("~C~lear desktop", NULL, cmClearDesktop, "Close all windows on the desktop; clear history lists"),
    IT("~R~epaint desktop", NULL, cmRepaint, "Redraw the screen"),
};
static MMenu sys_menu = { "\xe2\x89\xa1", sys_items, 4 };
int sys_menu_extra;

#define M(t, a) { t, a, (int)(sizeof a / sizeof a[0]) }
static const MMenu bar[NBAR] = {
    { "\xe2\x89\xa1", NULL, 0 },
    M("File", file_items), M("Edit", edit_items), M("Search", search_items), M("Run", run_items),
    M("Compile", compile_items), M("Debug", debug_items), M("Project", project_items),
    M("Options", options_items), M("Window", window_items), M("Help", help_items),
};

int bar_x[NBAR], bar_w[NBAR];
int menu_active = -1;

const char *menu_bar_title(int i) { return bar[i].title; }

void menu_layout(void)
{
    int x = 1;
    for (int i = 0; i < NBAR; i++) {
        int tl = i == 0 ? 1 : (int)strlen(bar[i].title);
        bar_x[i] = x; bar_w[i] = tl + 2;
        x += bar_w[i];
    }
}

void menubar_draw(void)
{
    scr_fill(0, 0, SW, 1, ' ', pal[P_MENU_NORM]);
    for (int i = 0; i < NBAR; i++) {
        uint8_t a = pal[P_MENU_NORM];
        int active = i == menu_active;
        if (active) scr_fill(bar_x[i], 0, bar_w[i], 1, ' ', pal[P_MENU_BAR]);
        if (i == 0) scr_str(bar_x[i] + 1, 0, "\xe2\x89\xa1", active ? pal[P_MENU_BAR] : a);
        else {
            const char *t = bar[i].title;
            scr_put(bar_x[i] + 1, 0, t[0], active ? pal[P_MENU_SELHOT] : pal[P_MENU_HOT]);
            scr_str(bar_x[i] + 2, 0, t + 1, active ? pal[P_MENU_BAR] : a);
        }
    }
}

int menu_hotkey(int key)
{
    if (!(key & K_ALT)) return -1;
    int c = tolower(key & KMASK);
    if ((key & KMASK) == ' ') return 0;
    for (int i = 1; i < NBAR; i++) if (tolower(bar[i].title[0]) == c) return i;
    return -1;
}

/* ---------- pulldown ---------- */
typedef struct { const MMenu *m; const MItem *items; int n, x, y, w, h, sel; } Level;

static const MMenu *bar_menu(int i)
{
    if (i == 0) {
        /* rebuild the system menu with the configured transfer items */
        int n = 4;
        sys_items[n].text = NULL; 
        if (cfg.nxfer) {
            sys_items[n++] = (MItem){ NULL, NULL, 0, NULL, NULL };
            for (int k = 0; k < cfg.nxfer && n < 16; k++)
                sys_items[n++] = (MItem){ cfg.xfer[k].title, NULL, cmTransfer0 + k, "Run this Transfer program", NULL };
        }
        sys_menu.n = n; sys_menu.items = sys_items;
        return &sys_menu;
    }
    return &bar[i];
}

static void level_init(Level *L, const MMenu *m, int x, int y)
{
    L->m = m; L->items = m->items; L->n = m->n; L->sel = -1;
    int w = 0;
    for (int i = 0; i < L->n; i++) {
        const MItem *it = &L->items[i];
        if (!it->text) continue;
        int tl = hotlen(it->text) + (it->key ? (int)strlen(it->key) + 3 : 0) + (it->sub ? 2 : 0);
        if (tl > w) w = tl;
    }
    L->w = w + 4; L->h = L->n + 2;
    if (x + L->w + 2 > SW) x = SW - L->w - 2;
    if (x < 0) x = 0;
    L->x = x; L->y = y;
}

static void level_draw(Level *L, int active_level)
{
    scr_fill(L->x, L->y, L->w, L->h, ' ', pal[P_PULL_NORM]);
    scr_box(L->x, L->y, L->w, L->h, BOX_SINGLE, pal[P_PULL_BORDER]);
    scr_shadow(L->x + L->w, L->y + 1, 2, L->h);
    scr_shadow(L->x + 2, L->y + L->h, L->w, 1);
    for (int i = 0; i < L->n; i++) {
        const MItem *it = &L->items[i];
        int y = L->y + 1 + i;
        if (!it->text) {
            scr_put(L->x, y, 0x251C, pal[P_PULL_BORDER]);
            for (int k = 1; k < L->w - 1; k++) scr_put(L->x + k, y, 0x2500, pal[P_PULL_BORDER]);
            scr_put(L->x + L->w - 1, y, 0x2524, pal[P_PULL_BORDER]);
            continue;
        }
        int en = it->sub ? 1 : cmd_enabled(it->cmd);
        int sel = i == L->sel;
        uint8_t a = !en ? (sel ? pal[P_PULL_SELDIS] : pal[P_PULL_DIS]) : (sel ? pal[P_PULL_SEL] : pal[P_PULL_NORM]);
        uint8_t hot = !en ? a : (sel ? pal[P_PULL_SELHOT] : pal[P_PULL_HOT]);
        scr_fill(L->x + 1, y, L->w - 2, 1, ' ', a);
        scr_hotstr(L->x + 2, y, it->text, a, hot);
        if (it->key) scr_str(L->x + L->w - 2 - (int)strlen(it->key), y, it->key, a);
        if (it->sub) scr_put(L->x + L->w - 3, y, 0x25BA, a);
    }
}

static int next_sel(Level *L, int from, int dir)
{
    for (int k = 0; k < L->n; k++) {
        from = (from + dir + L->n) % L->n;
        if (L->items[from].text) return from;
    }
    return from;
}

int menu_run(int start)
{
    Level lv[4]; int depth = 0;
    int top = start;
    int result = 0;
    level_init(&lv[0], bar_menu(top), bar_x[top] - (top == 0 ? 0 : 0), 1);
    lv[0].sel = next_sel(&lv[0], -1, 1);
    menu_active = top;
    for (;;) {
        {
            Level *L = &lv[depth];
            const MItem *it = L->sel >= 0 ? &L->items[L->sel] : NULL;
            status_hint(it && it->hint ? it->hint : "");
        }
        ide_draw();
        menubar_draw();
        for (int i = 0; i <= depth; i++) level_draw(&lv[i], i == depth);
        scr_cursor(0, 0, 0);
        scr_flush();
        int key = key_get(1000);
        if (key == KEY_IDLE || key == KEY_NONE) continue;
        Level *L = &lv[depth];
        int base = key & KMASK;
        if (key == KEY_ESC || key == KEY_F10) { if (depth > 0 && key == KEY_ESC) { depth--; continue; } break; }
        if (key == KEY_MOUSE) {
            if (ms.ev != MS_PRESS || ms.btn != 0) continue;
            int hit = -1;
            for (int d = depth; d >= 0 && hit < 0; d--) {
                Level *M = &lv[d];
                if (ms.x > M->x && ms.x < M->x + M->w - 1 && ms.y > M->y && ms.y < M->y + M->h - 1) {
                    int idx = ms.y - M->y - 1;
                    if (M->items[idx].text) { depth = d; M->sel = idx; hit = idx; }
                    else hit = -2;
                }
            }
            if (hit == -2) continue;
            if (hit >= 0) {
                Level *M = &lv[depth];
                const MItem *it = &M->items[M->sel];
                if (it->sub) { level_init(&lv[depth + 1], it->sub, M->x + M->w - 1, M->y + 1 + M->sel); lv[depth + 1].sel = next_sel(&lv[depth + 1], -1, 1); depth++; continue; }
                if (cmd_enabled(it->cmd)) { result = it->cmd; break; }
                continue;
            }
            if (ms.y == 0) {
                int nt = -1;
                for (int i = 0; i < NBAR; i++) if (ms.x >= bar_x[i] && ms.x < bar_x[i] + bar_w[i]) nt = i;
                if (nt >= 0) { top = nt; depth = 0; menu_active = top; level_init(&lv[0], bar_menu(top), bar_x[top], 1); lv[0].sel = next_sel(&lv[0], -1, 1); continue; }
            }
            break;
        }
        if (base == KEY_LEFT || base == KEY_RIGHT) {
            if (depth > 0 && base == KEY_LEFT) { depth--; continue; }
            if (depth > 0 && base == KEY_RIGHT) { /* fall through to menu switch */ depth = 0; }
            top = (top + (base == KEY_RIGHT ? 1 : NBAR - 1)) % NBAR;
            menu_active = top; depth = 0;
            level_init(&lv[0], bar_menu(top), bar_x[top], 1);
            lv[0].sel = next_sel(&lv[0], -1, 1);
            continue;
        }
        if (base == KEY_UP) { L->sel = next_sel(L, L->sel < 0 ? 0 : L->sel, -1); continue; }
        if (base == KEY_DOWN) { L->sel = next_sel(L, L->sel < 0 ? L->n - 1 : L->sel, 1); continue; }
        if (base == KEY_HOME) { L->sel = next_sel(L, -1, 1); continue; }
        if (base == KEY_END) { L->sel = next_sel(L, 0, -1); continue; }
        if (key & K_ALT) {
            int nb = menu_hotkey(key);
            if (nb >= 0) { top = nb; menu_active = top; depth = 0; level_init(&lv[0], bar_menu(top), bar_x[top], 1); lv[0].sel = next_sel(&lv[0], -1, 1); }
            continue;
        }
        int pick = -1;
        if (key == KEY_ENTER && L->sel >= 0) pick = L->sel;
        else if (base >= 32 && base < 127 && !(key & (K_CTRL | K_ALT))) {
            int c = tolower(base);
            for (int i = 0; i < L->n; i++) {
                const char *t = L->items[i].text;
                if (!t) continue;
                for (const char *p = t; *p; p++) if (*p == '~' && p[1]) { if (tolower((unsigned char)p[1]) == c) pick = i; break; }
                if (pick >= 0) break;
            }
        }
        if (pick >= 0) {
            L->sel = pick;
            const MItem *it = &L->items[pick];
            if (it->sub) { level_init(&lv[depth + 1], it->sub, L->x + L->w - 1, L->y + 1 + pick); lv[depth + 1].sel = next_sel(&lv[depth + 1], -1, 1); depth++; continue; }
            if (cmd_enabled(it->cmd)) { result = it->cmd; break; }
        }
    }
    menu_active = -1;
    status_hint(NULL);
    return result;
}
