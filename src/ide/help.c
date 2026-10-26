/* Flash C IDE - online help */
#include "ide.h"
#include <ctype.h>

typedef struct { const char *title, *keys, *text; } Topic;

static const Topic topics[] = {
{ "Contents", "",
"                     Flash C Help\n\n"
" {Using the IDE}\n"
" {Keyboard shortcuts}\n"
" {The editor}\n"
" {Compiling and running}\n"
" {Compiler options}\n"
" {Messages and errors}\n"
" {Projects}\n"
" {Language reference}\n"
" {Library: stdio}\n"
" {Library: string}\n"
" {Library: stdlib}\n"
" {Library: ctype}\n"
" {Library: conio and dos}\n"
" {Differences from classic compilers}\n"
" {About Flash C}\n\n"
" Press Tab to move between topics, Enter to follow one,\n"
" Alt-F1 to go back and Esc to close this window.\n" },

{ "Using the IDE", "ide menu window desktop",
" Using the IDE\n\n"
" The top line is the menu bar. Press F10 to activate it, or press\n"
" Alt plus the first letter of a menu (Alt-F opens File, Alt-C opens\n"
" Compile, and so on). Move with the arrow keys, Enter chooses an item,\n"
" Esc closes the menu. The mouse works too.\n\n"
" The bottom line shows the keys that are useful right now. While a menu\n"
" or a dialog box is open it shows a description of the highlighted item.\n\n"
" The desktop holds windows. Edit windows hold source files; the Message\n"
" window lists compiler messages. F6 selects the next window, F5 zooms the\n"
" active window, Alt-F3 closes it and Alt-0 lists all of them.\n"
" Alt-1 .. Alt-9 select an edit window by its number.\n\n"
" See also: {Keyboard shortcuts}, {The editor}\n" },

{ "Keyboard shortcuts", "keys shortcut hotkey",
" Keyboard shortcuts\n\n"
" F1 Help          F2 Save           F3 Open           F4 Go to cursor\n"
" F5 Zoom          F6 Next window    F9 Make           F10 Menu\n"
" Alt-F9 Compile   Ctrl-F9 Run       Alt-F3 Close      Alt-F5 User screen\n"
" Alt-F7 Previous message            Alt-F8 Next message\n"
" Alt-X  Quit      Alt-0  Window list\n"
" Ctrl-F7 Add watch                  Ctrl-F4 Evaluate\n"
" Shift-F1 Help index                Ctrl-F1 Help on the word at the cursor\n\n"
" Some terminals reserve a few of these (for instance F11, Alt-F4 or\n"
" Ctrl-F5). Every command is also reachable from the menus.\n\n"
" See also: {The editor}\n" },

{ "The editor", "edit editor cursor block undo find replace",
" The editor\n\n"
" Cursor:  arrows, Home, End, PgUp, PgDn; Ctrl-Left / Ctrl-Right move by\n"
"          word; Ctrl-PgUp / Ctrl-PgDn go to the start / end of the file.\n"
" Select:  hold Shift with any movement key, or drag with the mouse.\n"
" Edit:    Ctrl-Ins copy, Shift-Del cut, Shift-Ins paste, Ctrl-Del clear.\n"
"          Alt-Backspace undo, Shift-Alt-Backspace redo.\n"
"          Ctrl-Y deletes the line, Ctrl-T the word to the right,\n"
"          Ctrl-N inserts a line, Ins toggles insert / overwrite.\n\n"
" Block commands (press Ctrl-K, then the letter):\n"
"   B  mark the block start        K  mark the block end\n"
"   C  copy the block              V  move the block\n"
"   Y  delete the block            H  hide the block\n"
"   R  read a file into the text   W  write the block to a file\n"
"   T  mark the word               L  mark the line\n\n"
" Quick commands (press Ctrl-Q, then the letter):\n"
"   S  line start     D  line end     R  file start     C  file end\n"
"   E  window top     X  window bottom  F  find   A  replace\n"
"   Y  delete to end of line   [ or ]  jump to the matching bracket\n\n"
" The editor highlights C syntax; turn this off in\n"
" Options|Environment|Editor.\n" },

{ "Compiling and running", "compile make build link run flash fcc",
" Compiling and running\n\n"
" Compile (Alt-F9)  compiles the file in the active Edit window.\n"
" Make (F9)         compiles what changed and produces the executable.\n"
" Link              produces the executable from the current sources.\n"
" Build all         recompiles everything, whether it changed or not.\n"
" Run (Ctrl-F9)     makes the program and runs it. Output appears on the\n"
"                   user screen; press Alt-F5 to look at it again.\n"
" Run|Arguments     sets the command line passed to your program.\n\n"
" If a project is open ({Projects}), all of its source files are compiled\n"
" together into one program. Otherwise the file in the active window is\n"
" compiled. The executable is placed in the Output directory\n"
" (Options|Directories) or next to the source file.\n\n"
" Flash C generates native 64-bit Linux executables directly; no external\n"
" assembler, linker or C library is needed. A small run-time library is\n"
" built into the compiler ({Library: stdio} and the topics that follow).\n\n"
" The command-line version of the compiler is called fcc:\n"
"     fcc [options] file.c ...\n" },

{ "Compiler options", "options warnings directories names optimization",
" Compiler options\n\n"
" Options|Compiler|Source        nested comments, identifier length\n"
" Options|Compiler|Messages      which warnings are shown\n"
" Options|Compiler|Optimizations constant folding and jump optimisation\n"
" Options|Directories            include search path and output directory\n"
" Options|Save                   writes the settings to fcconfig.fc\n\n"
" Several dialog boxes (memory model, floating point, overlays, segment\n"
" names ...) keep the layout of the classic DOS tools, so existing\n"
" configuration habits still apply. The settings that make sense for a\n"
" 64-bit Linux target take effect; the others are stored but ignored.\n\n"
" See also: {Differences from classic compilers}\n" },

{ "Messages and errors", "message error warning fatal linker",
" Messages and errors\n\n"
" Compiler messages appear in the Message window. Use Enter to edit the\n"
" source line of the selected message, Space to view it, Alt-F7 and\n"
" Alt-F8 to step through the messages.\n\n"
" Error    the program cannot be compiled until this is fixed.\n"
" Warning  the program compiled, but something looks suspicious.\n"
" Fatal    the compiler cannot continue.\n"
" Linker   a symbol could not be resolved (for instance no main).\n\n"
" Common messages:\n"
"   Undefined symbol 'x'        the name was not declared.\n"
"   Statement missing ;         a semicolon is missing.\n"
"   Call to function 'f' with no prototype\n"
"                               declare the function before using it\n"
"                               (usually by including the right header).\n"
"   Function should return a value\n"
"   'x' is declared but never used\n" },

{ "Projects", "project prj fcp",
" Projects\n\n"
" A project lists several source files that make up one program.\n"
" Project|Open project... loads (or creates) a .FCP file; Add item adds\n"
" a source file; Delete item removes the selected file. The Project\n"
" window (Window|Project) shows the list.\n\n"
" All files of the project are compiled together; a function or global\n"
" variable can be used from any file of the project provided it is\n"
" declared before use. Declare shared items in a header file.\n" },

{ "Language reference", "c language type int char long short unsigned struct union enum typedef pointer array sizeof",
" Language reference\n\n"
" Flash C compiles the C language as used by DOS-era compilers.\n\n"
" Types:    void char short int long, signed / unsigned variants,\n"
"           pointers, arrays, struct, union, enum, typedef,\n"
"           function pointers.\n"
" Sizes:    char 1, short 2, int 4, long 8, pointer 8 bytes.\n"
" Not yet:  float and double, bit fields, passing or returning whole\n"
"           structures by value (pass a pointer instead).\n\n"
" Statements: if else while do for switch case default break continue\n"
"           goto return, declarations anywhere in a block.\n"
" Operators:  the complete C set including ?: , && || and assignment\n"
"           operators; pointer arithmetic is scaled as usual.\n"
" Initializers: scalars, arrays, structs and strings, with nesting.\n"
" Preprocessor: #include #define (also with arguments, # and ##)\n"
"           #undef #if #ifdef #ifndef #elif #else #endif #error\n"
"           #pragma (ignored)\n"
" Keywords near, far, huge, cdecl and pascal are accepted and ignored.\n" },

{ "Library: stdio", "stdio printf scanf puts putchar getchar gets fgets fopen fclose fprintf sprintf sscanf fread fwrite fseek",
" <stdio.h>\n\n"
" printf fprintf sprintf snprintf vprintf vfprintf vsprintf vsnprintf\n"
"    Formats: %d %i %u %x %X %o %c %s %p %% with flags - + space # 0,\n"
"    width, precision and the l modifier. (No floating point.)\n"
" scanf fscanf sscanf    reads %d %i %u %x %o %s %c\n"
" puts putchar fputs fputc putc    gets fgets getchar fgetc getc ungetc\n"
" fopen fclose fread fwrite fseek ftell rewind feof ferror fflush\n"
" remove rename perror\n"
" stdin stdout stderr\n\n"
" Output to stdout is flushed at every new-line and when the program ends.\n" },

{ "Library: string", "string strlen strcpy strcat strcmp strchr strstr memcpy memset strtok strdup",
" <string.h>\n\n"
" strlen strcpy strncpy strcat strncat strcmp strncmp stricmp strnicmp\n"
" strchr strrchr strstr strspn strcspn strpbrk strtok strdup\n"
" strupr strlwr strrev\n"
" memcpy memmove memset memcmp memchr\n" },

{ "Library: stdlib", "stdlib malloc free calloc realloc exit abs atoi atol strtol rand srand qsort bsearch itoa",
" <stdlib.h>\n\n"
" malloc calloc realloc free      exit abort\n"
" abs labs atoi atol strtol strtoul itoa ltoa\n"
" rand srand (RAND_MAX is 32767)  qsort bsearch\n" },

{ "Library: ctype", "ctype isalpha isdigit isspace toupper tolower",
" <ctype.h>\n\n"
" isalpha isdigit isalnum isupper islower isspace isxdigit isprint\n"
" isgraph iscntrl ispunct isascii toupper tolower\n" },

{ "Library: conio and dos", "conio clrscr gotoxy getch getche putch cputs delay sleep",
" <conio.h> <dos.h>\n\n"
" clrscr clreol gotoxy(x, y)     use ANSI escape sequences\n"
" getch getche putch cputs       unbuffered console input and output\n"
" delay(milliseconds) sleep(seconds)\n" },

{ "Differences from classic compilers", "dos difference int 64 float memory model far near",
" Differences from classic DOS compilers\n\n"
" * The target is 64-bit Linux, so int is 32 bits, long and pointers are\n"
"   64 bits. Memory models, near / far pointers, overlays and the\n"
"   floating-point emulator do not apply.\n"
" * There is no separate object-file / link step: the compiler writes the\n"
"   executable directly. Compile and Link therefore produce the same file.\n"
" * The source-level debugger is not available yet; the Debug menu offers\n"
"   Evaluate and Watches for constant expressions.\n"
" * The standard library is a compact built-in one. Programs get their\n"
"   header files from the compiler itself, and your own include\n"
"   directories (Options|Directories) are searched first.\n" },

{ "About Flash C", "about version",
" Flash C " FC_VERSION "\n\n"
" A compact C compiler and text-mode IDE for Linux.\n"
" The compiler translates C to native x86-64 executables.\n\n"
" See {Contents} for a list of topics.\n" },
{ NULL, NULL, NULL }
};

#define NTOPICS (int)(sizeof topics / sizeof topics[0] - 1)

typedef struct HS { int topic, top, link, hist[32], nhist; } HS;

static int find_topic(const char *name)
{
    for (int i = 0; i < NTOPICS; i++) if (!strcasecmp(topics[i].title, name)) return i;
    return -1;
}

static int count_links(const char *t)
{
    int n = 0;
    for (; *t; t++) if (*t == '{') n++;
    return n;
}

static void help_goto(Win *w, int topic, int push)
{
    HS *h = w->data;
    if (push && h->topic >= 0 && h->nhist < 32) h->hist[h->nhist++] = h->topic;
    h->topic = topic; h->top = 0; h->link = 0;
    snprintf(w->title, sizeof w->title, "Help: %s", topics[topic].title);
}

static Win *help_window(void)
{
    Win *w = win_find_kind(W_HELP);
    if (w) { win_activate(w); return w; }
    int ww = SW > 80 ? 76 : 72, hh = SH - 6;
    if (nwins >= MAXWIN) return NULL;
    w = calloc(1, sizeof *w);
    w->kind = W_HELP; w->x = (SW - ww) / 2; w->y = 2; w->w = ww; w->h = hh; w->errline = -1;
    HS *h = calloc(1, sizeof *h); h->topic = -1;
    w->data = h;
    wins[nwins++] = w;
    return w;
}

void help_show(const char *topic)
{
    Win *w = help_window(); if (!w) return;
    int t = find_topic(topic);
    help_goto(w, t < 0 ? 0 : t, ((HS *)w->data)->topic >= 0);
}
void help_contents(void) { help_show("Contents"); }

void help_index(void)
{
    Win *w = help_window(); if (!w) return;
    help_goto(w, 0, ((HS *)w->data)->topic >= 0);
}

void help_topic_search(const char *word)
{
    if (word && *word) {
        for (int i = 0; i < NTOPICS; i++) {
            const char *k = topics[i].keys;
            size_t wl = strlen(word);
            for (const char *p = k; *p;) {
                while (*p == ' ') p++;
                const char *e = p; while (*e && *e != ' ') e++;
                if ((size_t)(e - p) == wl && !strncasecmp(p, word, wl)) { Win *w = help_window(); if (w) help_goto(w, i, ((HS *)w->data)->topic >= 0); return; }
                p = e;
            }
        }
    }
    help_contents();
}

void help_prev(void)
{
    Win *w = win_find_kind(W_HELP);
    if (!w) { help_contents(); return; }
    HS *h = w->data;
    if (h->nhist) { int t = h->hist[--h->nhist]; help_goto(w, t, 0); }
    win_activate(w);
}

static int link_target(const char *text, int idx, char *name, int n)
{
    int k = 0;
    for (const char *p = text; *p; p++) if (*p == '{') {
        if (k++ == idx) {
            const char *e = strchr(p, '}'); if (!e) return -1;
            int l = (int)(e - p - 1); if (l >= n) l = n - 1;
            memcpy(name, p + 1, l); name[l] = 0; return find_topic(name);
        }
    }
    return -1;
}

void help_draw(Win *w, int active)
{
    HS *h = w->data;
    const char *text = topics[h->topic].text;
    uint8_t norm = pal[P_HELP_NORM];
    scr_fill(w->x, w->y, w->w, w->h, ' ', norm);
    scr_box(w->x, w->y, w->w, w->h, active ? BOX_DOUBLE : BOX_SINGLE, pal[P_HELP_BORDER]);
    char t[100]; snprintf(t, sizeof t, " %s ", w->title);
    scr_str(w->x + (w->w - (int)strlen(t)) / 2, w->y, t, pal[P_HELP_BORDER]);
    scr_put(w->x + 1, w->y, '[', pal[P_HELP_BORDER]); scr_put(w->x + 2, w->y, 0x25A0, pal[P_ED_ICON]); scr_put(w->x + 3, w->y, ']', pal[P_HELP_BORDER]);
    int row = 0, link = 0, hh = w->h - 2, nlines = 0, sel_row = -1;
    const char *p = text;
    /* first pass: find the row of the selected link */
    {
        int r = 0, l = 0;
        for (const char *q = text; *q; q++) { if (*q == '\n') r++; else if (*q == '{') { if (l == h->link) sel_row = r; l++; } }
        for (const char *q = text; *q; q++) if (*q == '\n') nlines++;
    }
    if (sel_row >= 0) { if (sel_row < h->top) h->top = sel_row; if (sel_row >= h->top + hh) h->top = sel_row - hh + 1; }
    if (h->top > nlines - hh) h->top = nlines - hh; if (h->top < 0) h->top = 0;
    while (*p) {
        const char *e = strchr(p, '\n'); if (!e) e = p + strlen(p);
        int sy = row - h->top;
        int x = 0;
        for (const char *q = p; q < e; q++) {
            if (*q == '{') {
                const char *c = memchr(q, '}', e - q);
                if (c) {
                    uint8_t a = (link == h->link && active) ? pal[P_HELP_LINKSEL] : pal[P_HELP_LINK];
                    if (sy >= 0 && sy < hh) for (const char *r = q + 1; r < c; r++) scr_put(w->x + 1 + x + (int)(r - q - 1), w->y + 1 + sy, (unsigned char)*r, a);
                    x += (int)(c - q - 1); q = c; link++; continue;
                }
            }
            if (sy >= 0 && sy < hh && x < w->w - 2) scr_put(w->x + 1 + x, w->y + 1 + sy, (unsigned char)*q, norm);
            x++;
        }
        row++;
        p = *e ? e + 1 : e;
    }
}

int help_key(Win *w, int key)
{
    HS *h = w->data;
    int base = key & KMASK, nl = count_links(topics[h->topic].text);
    char nm[100];
    if (key == KEY_ESC) { win_close(w); return 1; }
    switch (base) {
    case KEY_TAB: if (nl) h->link = (h->link + ((key & K_SHIFT) ? nl - 1 : 1)) % nl; return 1;
    case KEY_DOWN: case KEY_RIGHT: if (h->link < nl - 1) h->link++; else h->top++; return 1;
    case KEY_UP: case KEY_LEFT: if (h->link > 0) h->link--; else if (h->top > 0) h->top--; return 1;
    case KEY_PGDN: h->top += w->h - 3; return 1;
    case KEY_PGUP: h->top -= w->h - 3; if (h->top < 0) h->top = 0; return 1;
    case KEY_HOME: h->top = 0; h->link = 0; return 1;
    case KEY_ENTER: { int t = link_target(topics[h->topic].text, h->link, nm, sizeof nm); if (t >= 0) help_goto(w, t, 1); return 1; }
    case KEY_BKSP: help_prev(); return 1;
    }
    return 0;
}

void help_click(Win *w, int x, int y)
{
    HS *h = w->data;
    int row = y - w->y - 1 + h->top, link = 0, r = 0;
    for (const char *q = topics[h->topic].text; *q; q++) {
        if (*q == '\n') r++;
        else if (*q == '{') {
            const char *c = strchr(q, '}');
            if (r == row && c) {
                /* column of this link */
                const char *ls = q; while (ls > topics[h->topic].text && ls[-1] != '\n') ls--;
                int col = 0;
                for (const char *z = ls; z < q; z++) { if (*z == '{') { const char *cz = strchr(z, '}'); col += (int)(cz - z - 1); z = cz; } else col++; }
                int lx = w->x + 1 + col;
                if (x >= lx && x < lx + (int)(c - q - 1)) { h->link = link; help_key(w, KEY_ENTER); return; }
            }
            link++; if (c) q = c;
        }
    }
}
