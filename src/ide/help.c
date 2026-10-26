/* Flash C IDE - online help */
#include "ide.h"
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>

typedef struct { const char *title, *keys, *text; } Topic;

/* The basic documentation is built in. Extended topics (the library reference, ...) are read from
 * external *.fch files when they are installed; see help_load_external(). */
static const Topic basic[] = {
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
" Options|Compiler|Code generation  unsigned char, floating point on/off,\n"
"                                   predefined macros (Defines)\n"
" Options|Compiler|Source           nested comments, identifier length\n"
" Options|Compiler|Messages         which warnings are shown\n"
" Options|Linker                    map file, default libraries, which\n"
"                                   run-time libraries are linked\n"
" Options|Make                      check auto-dependencies\n"
" Options|Debugger                  source debugging, display swapping,\n"
"                                   number format\n"
" Options|Directories               include search path and output directory\n"
" Options|Save                      writes the settings to fcconfig.fc\n\n"
" Every option in these dialogs is used by the compiler or the IDE. The\n"
" classic memory models, overlays, segment names, librarian and C++\n"
" options do not exist because they do not apply to this compiler.\n\n"
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
" Also:    float and double; structures can be passed and returned by\n"
"           value; bit fields (int / unsigned int, C89). Not yet: compound literals.\n\n"
" Statements: if else while do for switch case default break continue\n"
"           goto return, declarations anywhere in a block.\n"
" Operators:  the complete C set including ?: , && || and assignment\n"
"           operators; pointer arithmetic is scaled as usual.\n"
" Initializers: scalars, arrays, structs and strings, with nesting.\n"
" Preprocessor: #include #define (also with arguments, # and ##)\n"
"           #undef #if #ifdef #ifndef #elif #else #endif #error\n"
"           #pragma startup / exit (other pragmas are ignored)\n"
" Keywords near, far, huge, cdecl and pascal are accepted and ignored.\n" },

{ "Library: stdio", "stdio clearerr fclose fcloseall fdopen feof ferror fflush fgetc fgetchar fgetpos fgets fileno flushall fopen fprintf fputc fputchar fputs fread freopen fscanf fseek fsetpos _fsopen ftell fwrite getc getchar gets getw perror printf putc putchar puts putw remove rename rewind rmtmp scanf setbuf setvbuf snprintf sprintf sscanf tempnam tmpfile tmpnam ungetc unlink vfprintf vfscanf vprintf vscanf vsnprintf vsprintf vsscanf",
" <stdio.h>\n"
"\n"
" Formatted and character I/O on streams. stdin, stdout and stderr are predefined.\n"
" Output to a terminal is flushed at each new-line; everything is flushed when the program ends.\n"
"\n"
" clearerr fclose fcloseall fdopen feof ferror fflush fgetc fgetchar\n"
" fgetpos fgets fileno flushall fopen fprintf fputc fputchar fputs\n"
" fread freopen fscanf fseek fsetpos _fsopen ftell fwrite getc getchar\n"
" gets getw perror printf putc putchar puts putw remove rename rewind\n"
" rmtmp scanf setbuf setvbuf snprintf sprintf sscanf tempnam tmpfile\n"
" tmpnam ungetc unlink vfprintf vfscanf vprintf vscanf vsnprintf\n"
" vsprintf vsscanf\n" },
{ "Library: string", "string memccpy memchr memcmp memcpy memicmp memmove memset movmem setmem stpcpy strcat strchr strcmp strcmpi strcoll strcpy strcspn strdup strerror _strerror stricmp strlen strlwr strncat strncmp strncpy strnicmp strnset strpbrk strrchr strrev strset strspn strstr strtok strupr strxfrm",
" <string.h> <mem.h>\n"
"\n"
" String and memory block functions (stricmp, strupr, movmem, setmem ... are included).\n"
"\n"
" memccpy memchr memcmp memcpy memicmp memmove memset movmem setmem\n"
" stpcpy strcat strchr strcmp strcmpi strcoll strcpy strcspn strdup\n"
" strerror _strerror stricmp strlen strlwr strncat strncmp strncpy\n"
" strnicmp strnset strpbrk strrchr strrev strset strspn strstr strtok\n"
" strupr strxfrm\n" },
{ "Library: stdlib", "stdlib abort abs atexit atof atoi atol bsearch calloc div ecvt exit _exit _cexit _c_exit fcvt free gcvt getenv itoa labs ldiv lfind lsearch ltoa malloc mblen mbstowcs mbtowc putenv qsort rand realloc srand strtod strtol strtoul swab system ultoa wcstombs wctomb _rotl _rotr _lrotl _lrotr _fullpath _makepath _splitpath _searchenv brk sbrk coreleft heapcheck heapcheckfree heapchecknode heapfillfree heapwalk",
" <stdlib.h> <alloc.h>\n"
"\n"
" Memory allocation, conversions, sorting, environment and program termination.\n"
" rand() uses the classic generator: srand(1) gives 346, 130, 10982 ...\n"
" RAND_MAX is 32767. exit runs atexit handlers and flushes the files.\n"
"\n"
" abort abs atexit atof atoi atol bsearch calloc div ecvt exit _exit\n"
" _cexit _c_exit fcvt free gcvt getenv itoa labs ldiv lfind lsearch\n"
" ltoa malloc mblen mbstowcs mbtowc putenv qsort rand realloc srand\n"
" strtod strtol strtoul swab system ultoa wcstombs wctomb _rotl _rotr\n"
" _lrotl _lrotr _fullpath _makepath _splitpath _searchenv brk sbrk\n"
" coreleft heapcheck heapcheckfree heapchecknode heapfillfree heapwalk\n" },
{ "Library: ctype", "ctype isalnum isalpha isascii iscntrl isdigit isgraph islower isprint ispunct isspace isupper isxdigit toascii tolower toupper _tolower _toupper",
" <ctype.h>\n"
"\n"
" Character classification and conversion.\n"
"\n"
" isalnum isalpha isascii iscntrl isdigit isgraph islower isprint\n"
" ispunct isspace isupper isxdigit toascii tolower toupper _tolower\n"
" _toupper\n" },
{ "Library: math", "math matherr abs labs acos asin atan atan2 atof ceil cos cosh exp fabs floor fmod frexp hypot ldexp log log10 modf poly pow pow10 sin sinh sqrt tan tanh acosl asinl atanl atan2l ceill cosl coshl expl fabsl floorl fmodl frexpl hypotl ldexpl logl log10l modfl polyl powl pow10l sinl sinhl sqrtl tanl tanhl trunc round log2 cbrt fmax fmin isnan isinf _clear87 _control87 _fpreset _status87",
" <math.h> <float.h>\n"
"\n"
" Mathematics. Domain and range errors call matherr() and print a message such as\n"
" 'sqrt: DOMAIN error'. Linking floating point is automatic when the program uses\n"
" float or double; Options|Libraries|Math library can switch it off.\n"
"\n"
" matherr abs labs acos asin atan atan2 atof ceil cos cosh exp fabs\n"
" floor fmod frexp hypot ldexp log log10 modf poly pow pow10 sin sinh\n"
" sqrt tan tanh acosl asinl atanl atan2l ceill cosl coshl expl fabsl\n"
" floorl fmodl frexpl hypotl ldexpl logl log10l modfl polyl powl pow10l\n"
" sinl sinhl sqrtl tanl tanhl trunc round log2 cbrt fmax fmin isnan\n"
" isinf _clear87 _control87 _fpreset _status87\n" },
{ "Library: time", "time asctime clock ctime difftime gmtime localtime mktime strftime time stime tzset _strdate _strtime ftime utime",
" <time.h> <dos.h> <sys/timeb.h>\n"
"\n"
" Calendar time. The time zone comes from the TZ variable (for example EST5EDT);\n"
" without it local time is UTC. Also: the DOS-style date and time calls of dos.h.\n"
"\n"
" asctime clock ctime difftime gmtime localtime mktime strftime time\n"
" stime tzset _strdate _strtime ftime utime\n" },
{ "Library: io and dir", "io dir access chmod _chmod chsize close _close creat _creat creatnew creattemp dup dup2 eof filelength getftime ioctl isatty lock locking lseek mktemp open _open read _read remove rename setftime setmode sopen tell umask unlink unlock write _write stat fstat chdir findfirst findnext fnmerge fnsplit getcurdir getcwd getdisk mkdir rmdir searchpath setdisk opendir readdir closedir rewinddir _chdrive _getdcwd _getdrive",
" <io.h> <fcntl.h> <sys/stat.h> <dir.h> <dirent.h>\n"
"\n"
" Low-level file handles, file status, directories and wildcard searches\n"
" (findfirst/findnext), path splitting and merging.\n"
"\n"
" access chmod _chmod chsize close _close creat _creat creatnew\n"
" creattemp dup dup2 eof filelength getftime ioctl isatty lock locking\n"
" lseek mktemp open _open read _read remove rename setftime setmode\n"
" sopen tell umask unlink unlock write _write stat fstat chdir\n"
" findfirst findnext fnmerge fnsplit getcurdir getcwd getdisk mkdir\n"
" rmdir searchpath setdisk opendir readdir closedir rewinddir _chdrive\n"
" _getdcwd _getdrive\n" },
{ "Library: process", "process abort _cexit _c_exit execl execle execlp execlpe execv execve execvp execvpe exit _exit getpid spawnl spawnle spawnlp spawnlpe spawnv spawnve spawnvp spawnvpe system signal raise",
" <process.h> <signal.h> <setjmp.h>\n"
"\n"
" Running other programs (system, spawn..., exec...), signals and non-local jumps.\n"
"\n"
" abort _cexit _c_exit execl execle execlp execlpe execv execve execvp\n"
" execvpe exit _exit getpid spawnl spawnle spawnlp spawnlpe spawnv\n"
" spawnve spawnvp spawnvpe system signal raise\n" },
{ "Library: conio and dos", "conio dos cgets clreol clrscr cprintf cputs cscanf delline getch getche gettextinfo getpass gotoxy highvideo insline kbhit lowvideo normvideo putch _setcursortype textattr textbackground textcolor textmode ungetch wherex wherey window delay sleep sound nosound getdate gettime setdate settime dostounix unixtodos getdfree getpid unlink biostime bioskey biosequip biosmemory setlocale localeconv",
" <conio.h> <dos.h> <bios.h>\n"
"\n"
" Console I/O through ANSI terminal sequences: windows, colours, cursor control,\n"
" unbuffered keyboard input (extended keys return 0 followed by a scan code).\n"
" DOS interrupts, ports, far memory, gettext/puttext/movetext and graphics are not available.\n"
"\n"
" cgets clreol clrscr cprintf cputs cscanf delline getch getche\n"
" gettextinfo getpass gotoxy highvideo insline kbhit lowvideo normvideo\n"
" putch _setcursortype textattr textbackground textcolor textmode\n"
" ungetch wherex wherey window delay sleep sound nosound getdate\n"
" gettime setdate settime dostounix unixtodos getdfree getpid unlink\n"
" biostime bioskey biosequip biosmemory setlocale localeconv\n" },
{ "Libraries and linking", "library libraries link linker map wildargs pragma startup exit nolibs floating",
" Libraries and linking\n\n"
" Programs are linked with the Flash C run-time library. Only the functions\n"
" a program really uses are placed in the executable.\n\n"
" Start-up code      always linked (argc/argv/environ, exit, abort)\n"
" Standard library   Options|Libraries|Standard run-time library\n"
" Math library       Options|Libraries|Math library (math.h, %f %e %g,\n"
"                    atof/strtod); linked when floating point is used\n"
" Wildcard arguments Options|Libraries|Wildcard arguments expands * and ?\n"
"                    in command-line arguments (command line: wildargs.obj)\n\n"
" Options|Linker\n"
"   Map file          Off / Segments / Publics / Detailed. Writes <program>.map\n"
"                     listing every linked function and its library module.\n"
"   Default libraries unchecked = link only the start-up code (-ln)\n\n"
" Options|Compiler|Code generation|Floating point\n"
"   None              floating-point formats are not linked; printf(\"%f\")\n"
"                     stops with 'floating point formats not linked'\n"
"   Emulation / 8087  both use the built-in floating-point code\n\n"
" #pragma startup fn [priority]   run fn before main\n"
" #pragma exit fn [priority]      run fn after main returns or exit() is called\n"
" Priorities are 0-255 (default 100). Start-up functions run from the lowest\n"
" number up, exit functions from the highest down.\n\n"
" See also: {Library: stdio}, {Library: math}\n" },

{ "Source-level debugger", "debug debugger breakpoint trace step watch inspect evaluate call stack go to cursor register",
" Source-level debugger\n\n"
" Ctrl+F9  Run to the next breakpoint        Ctrl+F2  Program reset\n"
" F4  Go to cursor                           F7  Trace into\n"
" F8  Step over                              Ctrl+F8  Toggle breakpoint\n"
" Alt+F4  Inspect                            Ctrl+F4  Evaluate/modify\n"
" Ctrl+F3  Call stack                        Ctrl+F7  Add watch\n\n"
" The program is compiled with debug information when Options|Debugger|\n"
" Source debugging is On. The first Trace into / Step over starts the\n"
" program and stops at the first statement of main; the green bar marks the\n"
" statement that runs next. Breakpoints (red lines) can have a condition\n"
" (a C expression) and a pass count: Debug|Breakpoints.\n\n"
" Expressions may use local and global variables, ., ->, [], *, &, casts,\n"
" sizeof and assignment. A format suffix selects the display: total,x\n"
" (hex), ,d (decimal), ,c (character).\n\n"
" Display swapping (Options|Debugger) selects when the program screen is\n"
" shown while stepping; Alt+F5 shows the user screen at any time.\n\n"
" See also: {Differences from classic compilers}\n" },

{ "Differences from classic compilers", "dos difference int 64 float memory model far near",
" Differences from classic DOS compilers\n\n"
" * The target is 64-bit Linux, so int is 32 bits, long and pointers are\n"
"   64 bits. Memory models, near / far pointers, overlays and the\n"
"   floating-point emulator do not apply (the settings are kept for\n"
"   compatibility). Graphics, BIOS and interrupt calls are not provided.\n"
" * Only C (ANSI C89) is supported. C++ is not supported, so there are no\n"
"   C++ options and files with C++ extensions are not compiled as C++.\n"
" * There is no separate object-file / link step: the compiler writes the\n"
"   executable directly. Compile and Link therefore produce the same file.\n"
" * The source-level debugger runs the program under ptrace (see\n"
"   {Source-level debugger}); it needs Options|Debugger|Source debugging.\n"
" * The standard library is built in (see {Libraries and linking}). Programs\n"
"   get their header files from the compiler itself, and your own include\n"
"   directories (Options|Directories) are searched first.\n" },

{ "About Flash C", "about version",
" Flash C " FC_VERSION "\n\n"
" A compact C compiler and text-mode IDE for Linux.\n"
" The compiler translates C to native x86-64 executables.\n\n"
" See {Contents} for a list of topics.\n" },
{ NULL, NULL, NULL }
};


/* ------------------------------------------------------------------ topic database */
typedef struct { char *title, *keys, *alias, *text; } Tp;
static Tp *tps; static int ntps, captps;
static int ext_loaded, ext_files, ext_tried;

static int tp_find(const char *name)
{
    for (int i = 0; i < ntps; i++) if (!strcasecmp(tps[i].title, name)) return i;
    for (int i = 0; i < ntps; i++) {                         /* aliases are separated by ';' */
        const char *a = tps[i].alias;
        while (a && *a) {
            while (*a == ' ' || *a == ';') a++;
            const char *e = a; while (*e && *e != ';') e++;
            const char *t = e; while (t > a && t[-1] == ' ') t--;
            if ((size_t)(t - a) == strlen(name) && !strncasecmp(a, name, t - a)) return i;
            a = e;
        }
    }
    return -1;
}

static int tp_new(const char *title)
{
    int i = tp_find(title);
    if (i >= 0 && !strcasecmp(tps[i].title, title)) {           /* replaces a topic of the same title */
        free(tps[i].keys); free(tps[i].alias); free(tps[i].text);
        tps[i].keys = strdup(""); tps[i].alias = strdup(""); tps[i].text = strdup("");
        return i;
    }
    if (ntps == captps) { captps = captps ? captps * 2 : 256; tps = realloc(tps, captps * sizeof *tps); }
    tps[ntps].title = strdup(title); tps[ntps].keys = strdup(""); tps[ntps].alias = strdup(""); tps[ntps].text = strdup("");
    return ntps++;
}

static void tp_append(int i, const char *s)
{
    size_t a = strlen(tps[i].text), b = strlen(s);
    tps[i].text = realloc(tps[i].text, a + b + 1);
    memcpy(tps[i].text + a, s, b + 1);
}

static void init_db(void)
{
    if (ntps) return;
    for (int i = 0; basic[i].title; i++) {
        int k = tp_new(basic[i].title);
        free(tps[k].keys); tps[k].keys = strdup(basic[i].keys);
        free(tps[k].text); tps[k].text = strdup(basic[i].text);
    }
}

/* ---- external help files (*.fch) ----
 *   ;; comment
 *   @topic Title          starts a topic (an existing topic of that title is replaced)
 *   @append Title         adds the following text to an existing topic
 *   @keys w1 w2 ...       words found by Topic search (Ctrl+F1)
 *   @alias n1; n2         more names for the Index and for links
 *   @example ... @end     program text; shown in the example colour and copied by Edit|Copy example
 *   {Title} {label|Title} a link; a line that starts with @@ stands for a text line that starts with @ */
static void load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[600]; int cur = -1, in_ex = 0;
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        nl = strchr(line, '\r'); if (nl) *nl = 0;
        if (!strncmp(line, ";;", 2)) continue;
        if (line[0] == '@' && line[1] != '@') {
            char *arg = strchr(line, ' '); if (arg) { *arg++ = 0; while (*arg == ' ') arg++; } else arg = line + strlen(line);
            if (!strcmp(line, "@topic") && *arg) { cur = tp_new(arg); in_ex = 0; }
            else if (!strcmp(line, "@append") && *arg) { cur = tp_find(arg); in_ex = 0; }
            else if (cur >= 0 && !strcmp(line, "@keys")) { free(tps[cur].keys); tps[cur].keys = strdup(arg); }
            else if (cur >= 0 && !strcmp(line, "@alias")) { free(tps[cur].alias); tps[cur].alias = strdup(arg); }
            else if (!strcmp(line, "@example")) in_ex = 1;
            else if (!strcmp(line, "@end")) in_ex = 0;
            continue;
        }
        if (cur < 0) continue;
        char *body = line[0] == '@' ? line + 1 : line;
        if (in_ex) tp_append(cur, "\x01");
        tp_append(cur, body); tp_append(cur, "\n");
    }
    fclose(f);
    ext_files++;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int load_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    char *names[256]; int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 256) {
        size_t l = strlen(de->d_name);
        if (l > 4 && !strcmp(de->d_name + l - 4, ".fch")) names[n++] = strdup(de->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    for (int i = 0; i < n; i++) { char p[1100]; snprintf(p, sizeof p, "%s/%s", dir, names[i]); load_file(p); free(names[i]); }
    return n;
}

/* where the help files may be installed */
static void help_dirs(char dirs[][700], int *n)
{
    *n = 0;
    const char *env = getenv("FC_HELP");
    if (env && *env) snprintf(dirs[(*n)++], 700, "%s", env);
    char exe[600]; ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (l > 0) {
        exe[l] = 0; char *sl = strrchr(exe, '/'); if (sl) *sl = 0;
        snprintf(dirs[(*n)++], 700, "%s/help", exe);
        snprintf(dirs[(*n)++], 700, "%s/../share/flashc/help", exe);
        snprintf(dirs[(*n)++], 700, "%s/../help", exe);
    }
    snprintf(dirs[(*n)++], 700, "%s", "/usr/local/share/flashc/help");
    snprintf(dirs[(*n)++], 700, "%s", "/usr/share/flashc/help");
    snprintf(dirs[(*n)++], 700, "%s", "help");
}

static void help_load_external(void)
{
    if (ext_tried) return;
    ext_tried = 1;
    init_db();
    char dirs[10][700]; int nd;
    help_dirs(dirs, &nd);
    for (int i = 0; i < nd && !ext_loaded; i++) if (load_dir(dirs[i]) > 0) ext_loaded = 1;
    if (!ext_loaded) {
        int c = tp_find("Contents");
        if (c >= 0) tp_append(c, "\n The library reference and the language reference are not installed\n (no *.fch files were found, see the help directory); only the basic\n documentation is available.\n");
    }
}

/* ------------------------------------------------------------------ parsed topic */
typedef struct { char *s; int example; } PLine;
typedef struct { int row, col, len; char target[100]; } PLink;
typedef struct HS {
    int topic, top, link, hist[32], nhist;
    PLine *lines; int nlines; PLink *links; int nlinks;
} HS;

static void parse_free(HS *h)
{
    for (int i = 0; i < h->nlines; i++) free(h->lines[i].s);
    free(h->lines); free(h->links);
    h->lines = NULL; h->links = NULL; h->nlines = h->nlinks = 0;
}

static void parse_topic(HS *h)
{
    parse_free(h);
    const char *text = tps[h->topic].text;
    int caplines = 0, caplinks = 0;
    for (const char *p = text; *p;) {
        const char *e = strchr(p, '\n'); if (!e) e = p + strlen(p);
        if (h->nlines == caplines) { caplines = caplines ? caplines * 2 : 64; h->lines = realloc(h->lines, caplines * sizeof *h->lines); }
        PLine *L = &h->lines[h->nlines];
        L->example = *p == 1;
        const char *q = p + (L->example ? 1 : 0);
        char *out = malloc(e - q + 1); int o = 0;
        for (; q < e; q++) {
            if (!L->example && *q == '{') {
                const char *c = memchr(q, '}', e - q);
                if (c) {
                    const char *bar = memchr(q, '|', c - q);
                    const char *ls = q + 1, *le = bar ? bar : c;
                    const char *ts = bar ? bar + 1 : q + 1;
                    if (h->nlinks == caplinks) { caplinks = caplinks ? caplinks * 2 : 32; h->links = realloc(h->links, caplinks * sizeof *h->links); }
                    PLink *k = &h->links[h->nlinks++];
                    k->row = h->nlines; k->col = o; k->len = (int)(le - ls);
                    int tl = (int)(c - ts); if (tl > 99) tl = 99;
                    memcpy(k->target, ts, tl); k->target[tl] = 0;
                    memcpy(out + o, ls, le - ls); o += (int)(le - ls);
                    q = c; continue;
                }
            }
            out[o++] = *q;
        }
        out[o] = 0; L->s = out;
        h->nlines++;
        p = *e ? e + 1 : e;
    }
}

static void help_goto(Win *w, int topic, int push)
{
    HS *h = w->data;
    if (push && h->topic >= 0 && h->nhist < 32) h->hist[h->nhist++] = h->topic;
    h->topic = topic; h->top = 0; h->link = 0;
    snprintf(w->title, sizeof w->title, "Help: %s", tps[topic].title);
    parse_topic(h);
}

static Win *help_window(void)
{
    help_load_external();
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

void help_free(Win *w) { HS *h = w->data; if (h) parse_free(h); }

void help_show(const char *topic)
{
    Win *w = help_window(); if (!w) return;
    int t = tp_find(topic);
    help_goto(w, t < 0 ? 0 : t, ((HS *)w->data)->topic >= 0);
}
void help_contents(void) { help_show("Contents"); }

/* ---- Index: every topic title and alias, with type-ahead */
typedef struct { char *name; int topic; } IEnt;
static IEnt *ient; static int nient;
static int cmp_ient(const void *a, const void *b) { return strcasecmp(((const IEnt *)a)->name, ((const IEnt *)b)->name); }

static void build_index(void)
{
    for (int i = 0; i < nient; i++) free(ient[i].name);
    free(ient); ient = NULL; nient = 0;
    int cap = 0;
    for (int i = 0; i < ntps; i++) {
        const char *a = tps[i].alias;
        for (int pass = 0; pass < 2; pass++) {
            const char *s = pass ? a : tps[i].title;
            while (s && *s) {
                const char *e = pass ? strchr(s, ';') : NULL; if (!e) e = s + strlen(s);
                const char *b = s; while (b < e && *b == ' ') b++;
                const char *t = e; while (t > b && t[-1] == ' ') t--;
                if (t > b) {
                    if (nient == cap) { cap = cap ? cap * 2 : 256; ient = realloc(ient, cap * sizeof *ient); }
                    ient[nient].name = strndup(b, t - b); ient[nient].topic = i; nient++;
                }
                if (!pass || !*e) break;
                s = e + 1;
            }
        }
    }
    qsort(ient, nient, sizeof *ient, cmp_ient);
}

static char ix_text[100], ix_last[100];
static void ix_change(Dialog *d)
{
    if (!strcmp(ix_text, ix_last)) return;
    snprintf(ix_last, sizeof ix_last, "%s", ix_text);
    DItem *l = &d->it[1];
    size_t n = strlen(ix_text);
    for (int i = 0; i < nient; i++) if (!strncasecmp(ient[i].name, ix_text, n)) {
        l->sel = i;
        if (l->sel < l->top || l->sel >= l->top + l->h) l->top = i;
        break;
    }
}

static int index_dialog(const char *initial)
{
    help_load_external();
    build_index();
    if (!nient) return -1;
    char **items = malloc(nient * sizeof *items);
    for (int i = 0; i < nient; i++) items[i] = ient[i].name;
    snprintf(ix_text, sizeof ix_text, "%s", initial ? initial : "");
    ix_last[0] = 1; ix_last[1] = 0;
    Dialog d; dlg_init(&d, " Index ");
    d.w = 44; d.h = SH > 22 ? 20 : 16; d.on_change = ix_change;
    dlg_input(&d, 3, 1, 36, "~S~earch for", ix_text, sizeof ix_text, "Type the first letters of a topic name");
    int lh = d.h - 7;
    dlg_list(&d, 3, 4, 36, lh, items, nient, "Use cursor keys to examine the list; Enter shows the topic");
    d.it[1].link = 0;
    dlg_button(&d, 3, d.h - 3, 10, "~S~how", cmOK, 1, NULL);
    dlg_button(&d, 15, d.h - 3, 10, "Cancel", cmCancel, 0, NULL);
    ix_change(&d);
    int r = dlg_run(&d);
    int t = (r == cmOK && d.it[1].sel < nient) ? ient[d.it[1].sel].topic : -1;
    free(items);
    return t;
}

void help_index(void)
{
    int t = index_dialog(NULL);
    if (t < 0) return;
    Win *w = help_window(); if (!w) return;
    help_goto(w, t, ((HS *)w->data)->topic >= 0);
}

/* Ctrl+F1: the topic for the word at the cursor, or the index */
void help_topic_search(const char *word)
{
    help_load_external();
    if (word && *word) {
        int cand[64], n = 0;
        size_t wl = strlen(word);
        for (int i = 0; i < ntps && n < 64; i++) {
            int hit = !strcasecmp(tps[i].title, word) || tp_find(word) == i;
            const char *k = tps[i].keys;
            for (const char *p = k; *p && !hit;) {
                while (*p == ' ') p++;
                const char *e = p; while (*e && *e != ' ') e++;
                if ((size_t)(e - p) == wl && !strncasecmp(p, word, wl)) hit = 1;
                p = e;
            }
            if (hit) cand[n++] = i;
        }
        if (n == 1) { Win *w = help_window(); if (w) help_goto(w, cand[0], ((HS *)w->data)->topic >= 0); return; }
        if (n > 1) {
            char **items = malloc(n * sizeof *items);
            for (int i = 0; i < n; i++) items[i] = tps[cand[i]].title;
            Dialog d; dlg_init(&d, " Topic search ");
            d.w = 44; d.h = n + 8 > 18 ? 18 : n + 8;
            dlg_list(&d, 3, 1, 36, d.h - 5, items, n, "Choose the topic to display");
            dlg_button(&d, 3, d.h - 3, 10, "~S~how", cmOK, 1, NULL);
            dlg_button(&d, 15, d.h - 3, 10, "Cancel", cmCancel, 0, NULL);
            int r = dlg_run(&d), sel = d.it[0].sel;
            free(items);
            if (r == cmOK) { Win *w = help_window(); if (w) help_goto(w, cand[sel], ((HS *)w->data)->topic >= 0); }
            return;
        }
        int t = index_dialog(word);
        if (t >= 0) { Win *w = help_window(); if (w) help_goto(w, t, ((HS *)w->data)->topic >= 0); }
        return;
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

/* the program text of the current topic, for Edit|Copy example (malloc'd, NULL if there is none) */
char *help_example_text(void)
{
    Win *w = win_find_kind(W_HELP);
    if (!w || win_active() != w) return NULL;
    HS *h = w->data;
    if (!h || h->topic < 0) return NULL;
    size_t n = 0;
    for (int i = 0; i < h->nlines; i++) if (h->lines[i].example) n += strlen(h->lines[i].s) + 1;
    if (!n) return NULL;
    char *r = malloc(n + 1); size_t o = 0;
    for (int i = 0; i < h->nlines; i++) if (h->lines[i].example) o += sprintf(r + o, "%s\n", h->lines[i].s);
    return r;
}

/* scroll the text by delta lines (mouse wheel, scroll bar); the selected link follows the view */
void help_scroll(Win *w, int delta)
{
    HS *h = w->data;
    int hh = w->h - 2, max = h->nlines - hh;
    if (max < 0) max = 0;
    h->top += delta;
    if (h->top > max) h->top = max;
    if (h->top < 0) h->top = 0;
    if (!h->nlinks) return;
    int k = h->link;
    while (k < h->nlinks - 1 && h->links[k].row < h->top) k++;
    while (k > 0 && h->links[k].row >= h->top + hh) k--;
    h->link = k;
}

/* click on the vertical scroll bar */
void help_bar_click(Win *w, int y)
{
    int hh = w->h - 2;
    if (y <= w->y + 1) help_scroll(w, -1);
    else if (y >= w->y + w->h - 2) help_scroll(w, 1);
    else if (y < w->y + w->h / 2) help_scroll(w, -(hh - 1));
    else help_scroll(w, hh - 1);
}

void help_draw(Win *w, int active)
{
    HS *h = w->data;
    uint8_t norm = pal[P_HELP_NORM];
    scr_fill(w->x, w->y, w->w, w->h, ' ', norm);
    scr_box(w->x, w->y, w->w, w->h, active ? BOX_DOUBLE : BOX_SINGLE, pal[P_HELP_BORDER]);
    char t[140]; snprintf(t, sizeof t, " %s ", w->title);
    if ((int)strlen(t) > w->w - 12) t[w->w - 12] = 0;
    scr_str(w->x + (w->w - (int)strlen(t)) / 2, w->y, t, pal[P_HELP_BORDER]);
    scr_put(w->x + 1, w->y, '[', pal[P_HELP_BORDER]); scr_put(w->x + 2, w->y, 0x25A0, pal[P_ED_ICON]); scr_put(w->x + 3, w->y, ']', pal[P_HELP_BORDER]);
    int hh = w->h - 2, wd = w->w - 2;
    if (h->link >= h->nlinks) h->link = h->nlinks ? h->nlinks - 1 : 0;
    if (h->nlinks) { int r = h->links[h->link].row; if (r < h->top) h->top = r; if (r >= h->top + hh) h->top = r - hh + 1; }
    if (h->top > h->nlines - hh) h->top = h->nlines - hh;
    if (h->top < 0) h->top = 0;
    for (int r = 0; r < hh && h->top + r < h->nlines; r++) {
        PLine *L = &h->lines[h->top + r];
        int y = w->y + 1 + r;
        if (L->example) scr_fill(w->x + 1, y, wd, 1, ' ', pal[P_HELP_EXAMPLE]);
        scr_strn(w->x + 1, y, L->s, wd, L->example ? pal[P_HELP_EXAMPLE] : norm);
    }
    if (w->h > 4) {                                              /* scroll bar */
        int sx = w->x + w->w - 1;
        scr_put(sx, w->y + 1, 0x25B2, pal[P_ED_ARROWS]); scr_put(sx, w->y + w->h - 2, 0x25BC, pal[P_ED_ARROWS]);
        for (int yy = w->y + 2; yy < w->y + w->h - 2; yy++) scr_put(sx, yy, 0x2592, pal[P_ED_SCROLL]);
        int span = w->h - 5, max = h->nlines - hh;
        if (span > 0) scr_put(sx, w->y + 2 + (max > 0 ? (int)((long)h->top * span / max) : 0), 0x25A0, pal[P_ED_THUMB]);
    }
    for (int i = 0; i < h->nlinks; i++) {
        PLink *k = &h->links[i];
        int r = k->row - h->top;
        if (r < 0 || r >= hh) continue;
        uint8_t a = (i == h->link && active) ? pal[P_HELP_LINKSEL] : pal[P_HELP_LINK];
        const char *s = h->lines[k->row].s + k->col;
        for (int c = 0; c < k->len && k->col + c < wd; c++) scr_put(w->x + 1 + k->col + c, w->y + 1 + r, (unsigned char)s[c], a);
    }
}

int help_key(Win *w, int key)
{
    HS *h = w->data;
    int base = key & KMASK, nl = h->nlinks;
    if (key == KEY_ESC) { win_close(w); return 1; }
    switch (base) {
    case KEY_TAB: if (nl) h->link = (h->link + ((key & K_SHIFT) ? nl - 1 : 1)) % nl; return 1;
    case KEY_DOWN: case KEY_RIGHT: if (h->link < nl - 1 && (base == KEY_RIGHT || h->links[h->link + 1].row <= h->top + w->h - 3)) h->link++; else h->top++; return 1;
    case KEY_UP: case KEY_LEFT: if (h->link > 0 && (base == KEY_LEFT || h->links[h->link - 1].row >= h->top)) h->link--; else if (h->top > 0) h->top--; return 1;
    case KEY_PGDN: h->top += w->h - 3; if (nl) { int k = h->link; while (k < nl - 1 && h->links[k].row < h->top) k++; h->link = k; } return 1;
    case KEY_PGUP: h->top -= w->h - 3; if (h->top < 0) h->top = 0; if (nl) { int k = h->link; while (k > 0 && h->links[k].row >= h->top + w->h - 2) k--; h->link = k; } return 1;
    case KEY_HOME: h->top = 0; h->link = 0; return 1;
    case KEY_END: h->top = h->nlines; h->link = nl ? nl - 1 : 0; return 1;
    case KEY_ENTER: {
        if (!nl) return 1;
        int t = tp_find(h->links[h->link].target);
        if (t >= 0) help_goto(w, t, 1);
        return 1;
    }
    case KEY_BKSP: help_prev(); return 1;
    }
    return 0;
}

void help_click(Win *w, int x, int y)
{
    HS *h = w->data;
    int row = y - w->y - 1 + h->top, col = x - w->x - 1;
    for (int i = 0; i < h->nlinks; i++) {
        PLink *k = &h->links[i];
        if (k->row == row && col >= k->col && col < k->col + k->len) { h->link = i; help_key(w, KEY_ENTER); return; }
    }
}
