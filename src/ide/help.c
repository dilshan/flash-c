/*
 * File:    help.c
 * Purpose: Flash C IDE - online help
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "ide.h"
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>

typedef struct {
    const char *title, *keys, *text;
} Topic;

/* The basic documentation is built in. Extended topics (the library reference, ...) are read from
 * external *.fch files when they are installed; see help_load_external(). */
static const Topic basic[] = {
    {"Contents", "",
     " Flash C Help\n"
     "\n"
     "## Getting started\n"
     "\n"
     " {Using the IDE}\n"
     " {Keyboard shortcuts}\n"
     " {The editor}\n"
     " {Compiling and running}\n"
     " {Projects}\n"
     "\n"
     "## Compiler\n"
     "\n"
     " {Compiler options}\n"
     " {Code generation options}\n"
     " {Messages and errors}\n"
     " {Libraries and linking}\n"
     " {Source-level debugger}\n"
     "\n"
     "## The C language\n"
     "\n"
     " {Language reference}\n"
     " {Differences from classic compilers}\n"
     "\n"
     "## Library\n"
     "\n"
     " {Library: stdio}\n"
     " {Library: string}\n"
     " {Library: stdlib}\n"
     " {Library: ctype}\n"
     " {Library: conio and dos}\n"
     "\n"
     "## About\n"
     "\n"
     " {About Flash C}\n"
     "\n"
     " Press Tab to move between topics, Enter to follow one,\n"
     " Alt-F1 to go back and Esc to close this window.\n"},

    {"Using the IDE", "ide menu window desktop",
     " Using the IDE\n"
     "\n"
     "## The menu bar\n"
     "\n"
     " The top line is the menu bar. Press F10 to activate it, or press Alt\n"
     " plus the first letter of a menu (Alt-F opens File, Alt-C opens Compile,\n"
     " and so on). Move with the arrow keys, Enter chooses an item, Esc closes\n"
     " the menu. The mouse works too.\n"
     "\n"
     "## The status line\n"
     "\n"
     " The bottom line shows the keys that are useful right now. While a menu\n"
     " or a dialog box is open it shows a description of the highlighted item.\n"
     "\n"
     "## Windows\n"
     "\n"
     " The desktop holds windows. Edit windows hold source files; the Message\n"
     " window lists compiler messages and the Output window keeps the text a\n"
     " program wrote.\n"
     "\n"
     " F6 selects the next window, F5 zooms the active window, Alt-F3 closes\n"
     " it and Alt-0 lists all of them. Alt-1 .. Alt-9 select an edit window by\n"
     " its number.\n"
     "\n"
     " See also: {Keyboard shortcuts}, {The editor}\n"},

    {"Keyboard shortcuts", "keys shortcut hotkey",
     " Keyboard shortcuts\n"
     "\n"
     "## Files and help\n"
     "\n"
     "   F2        Save                   F3        Open\n"
     "   Alt-X     Quit                   Alt-0     Window list\n"
     "   F1        Help                   Shift-F1  Help index\n"
     "   Ctrl-F1   Help on the word at the cursor\n"
     "\n"
     "## Compile and run\n"
     "\n"
     "   Alt-F9    Compile                F9        Make\n"
     "   Ctrl-F9   Run                    Alt-F5    User screen\n"
     "\n"
     "## Windows\n"
     "\n"
     "   F5        Zoom                   F6        Next window\n"
     "   Alt-F3    Close window           F10       Menu\n"
     "\n"
     "## Messages\n"
     "\n"
     "   Alt-F7    Previous message       Alt-F8    Next message\n"
     "\n"
     "## Debugger\n"
     "\n"
     "   F4        Go to cursor           F7        Trace into\n"
     "   F8        Step over              Ctrl-F2   Program reset\n"
     "   Ctrl-F4   Evaluate               Ctrl-F7   Add watch\n"
     "\n"
     " Some terminals reserve a few of these (for instance F11, Alt-F4 or\n"
     " Ctrl-F5). Every command is also reachable from the menus.\n"
     "\n"
     " See also: {The editor}, {Source-level debugger}\n"},

    {"The editor", "edit editor cursor block undo find replace",
     " The editor\n"
     "\n"
     "## Moving the cursor\n"
     "\n"
     " Arrows, Home, End, PgUp and PgDn move as usual.\n"
     "\n"
     " Ctrl-Left / Ctrl-Right    move by word\n"
     " Ctrl-PgUp / Ctrl-PgDn     go to the start / end of the file\n"
     "\n"
     "## Selecting\n"
     "\n"
     " Hold Shift with any movement key, or drag with the mouse.\n"
     "\n"
     "## Editing\n"
     "\n"
     " Ctrl-Ins      copy              Shift-Del     cut\n"
     " Shift-Ins     paste             Ctrl-Del      clear\n"
     " Alt-Backspace undo              Shift-Alt-Backspace  redo\n"
     "\n"
     " Ctrl-Y        delete the line   Ctrl-T        delete the word on the right\n"
     " Ctrl-N        insert a line     Ins           toggle insert / overwrite\n"
     "\n"
     "## Block commands\n"
     "\n"
     " Press Ctrl-K, then the letter:\n"
     "\n"
     "   B  mark the block start        K  mark the block end\n"
     "   C  copy the block              V  move the block\n"
     "   Y  delete the block            H  hide the block\n"
     "   R  read a file into the text   W  write the block to a file\n"
     "   T  mark the word               L  mark the line\n"
     "\n"
     "## Quick commands\n"
     "\n"
     " Press Ctrl-Q, then the letter:\n"
     "\n"
     "   S  line start                  D  line end\n"
     "   R  file start                  C  file end\n"
     "   E  window top                  X  window bottom\n"
     "   F  find                        A  replace\n"
     "   Y  delete to end of line       [ or ]  jump to the matching bracket\n"
     "\n"
     "## Syntax highlighting\n"
     "\n"
     " The editor highlights C syntax; turn this off in\n"
     " Options|Environment|Editor.\n"},

    {"Compiling and running", "compile make build link run flash fcc",
     " Compiling and running\n"
     "\n"
     "## Commands\n"
     "\n"
     " Compile (Alt-F9)    compiles the file in the active Edit window.\n"
     "\n"
     " Make (F9)           compiles what changed and produces the executable.\n"
     "\n"
     " Link                produces the executable from the current sources.\n"
     "\n"
     " Build all           recompiles everything, whether it changed or not.\n"
     "\n"
     " Run (Ctrl-F9)       makes the program and runs it. Output appears on the\n"
     "                     user screen (press Alt-F5 to look at it again) and a\n"
     "                     copy is kept in the Output window.\n"
     "\n"
     " Run|Arguments       sets the command line passed to your program.\n"
     "\n"
     "## What is compiled\n"
     "\n"
     " If a project is open ({Projects}), all of its source files are compiled\n"
     " together into one program. Otherwise the file in the active window is\n"
     " compiled.\n"
     "\n"
     " The executable is placed in the Output directory (Options|Directories)\n"
     " or next to the source file.\n"
     "\n"
     "## How it works\n"
     "\n"
     " Flash C generates native 64-bit Linux executables directly; no external\n"
     " assembler, linker or C library is needed. A small run-time library is\n"
     " built into the compiler ({Library: stdio} and the topics that follow).\n"
     "\n"
     "## From the command line\n"
     "\n"
     "     fcc [options] file.c ...\n"},

    {"Compiler options", "options warnings directories names optimization",
     " Compiler options\n"
     "\n"
     " All of these are in the Options menu. Settings are saved with\n"
     " Options|Save into fcconfig.fc.\n"
     "\n"
     "## Compiler\n"
     "\n"
     " Code generation     enums, duplicate strings, structure alignment,\n"
     "                     unsigned char, stack test, floating point,\n"
     "                     Defines / Undefines\n"
     "\n"
     " Optimizations       twelve tree optimizations, jumps, redundant loads\n"
     "\n"
     " Source              nested comments, keywords (Flash C or ANSI),\n"
     "                     identifier length\n"
     "\n"
     " Messages            which warnings are shown\n"
     "\n"
     "## Linking and libraries\n"
     "\n"
     " Linker              map file, default libraries, ELF symbol table,\n"
     "                     W^X segments, stack size\n"
     "\n"
     " Libraries           which run-time libraries are linked\n"
     "\n"
     "## Other\n"
     "\n"
     " Make                check auto-dependencies\n"
     "\n"
     " Debugger            source debugging, display swapping, number format\n"
     "\n"
     " Directories         include search path and output directory\n"
     "\n"
     " Save                writes the settings to fcconfig.fc\n"
     "\n"
     " Every option in these dialogs is used by the compiler or the IDE. The\n"
     " classic memory models, overlays, segment names, librarian and C++\n"
     " options do not exist because they do not apply to this compiler.\n"
     "\n"
     " See also: {Code generation options}, {Differences from classic compilers}\n"},

    {"Code generation options",
     "code generation enum string struct pack stack overflow optimization jump symbol table elf wx "
     "stack size keywords ansi undefine",
     " Code generation options\n"
     "\n"
     " The fcc switch is in brackets; -x- turns a switch off.\n"
     "\n"
     "## Options|Compiler|Code generation\n"
     "\n"
     " Treat enums as ints         [-b] default. Off [-b-]: an enum uses the\n"
     "                             smallest of char / short / int that holds\n"
     "                             its values.\n"
     "\n"
     " Duplicate strings merged    [-d] identical string literals are stored\n"
     "                             once.\n"
     "\n"
     " Align structure members     [-a] default. Off [-a-]: members are packed\n"
     "                             without padding (library headers keep their\n"
     "                             layout).\n"
     "\n"
     " Unsigned characters         [-K] plain char is unsigned.\n"
     "\n"
     " Test stack overflow         [-N] every function checks the stack on\n"
     "                             entry and stops with 'Stack overflow!'\n"
     "                             (exit code 1).\n"
     "\n"
     " Defines / Undefine symbols  [-Dname[=value]]  [-Uname]\n"
     "\n"
     "## Options|Compiler|Optimizations\n"
     "\n"
     " The tree optimizations below work on every function before its code\n"
     " is generated.  Each has its own check box; all start switched on in\n"
     " the IDE.  On the command line -O turns them all on, -O- turns them\n"
     " off, and -O:name,-name switches single ones (fold, algebra, compare,\n"
     " constprop, copyprop, deadstore, unreachable, casts, canon, cse,\n"
     " strength, bool, jumps, peephole, all, none).\n"
     "\n"
     " Constant folding            calculates operations on constants\n"
     " Algebraic simplification    x+0, x*1, x-x, (x+1)+2 and similar\n"
     " Constant comparison         decides tests with a known result, such\n"
     "                             as unsigned >= 0 or a char == 300\n"
     " Redundant cast removal      drops conversions that change nothing\n"
     " Expression canonicalization constants to the right, fixed order\n"
     " Strength reduction          x*8 becomes x<<3, unsigned x/4 becomes\n"
     "                             x>>2, unsigned x%16 becomes x&15\n"
     " Boolean simplification      !!x, x==0, c?1:0, constant && and ||\n"
     " Common subexpression elim.  a repeated expression is computed once\n"
     " Constant propagation        known values of local variables are used\n"
     " Copy propagation            the original of a copied variable is used\n"
     " Dead assignment removal     drops stores whose value is never used\n"
     " Unreachable code removal    drops code after return/break, if (0),\n"
     "                             while (0) and empty statements\n"
     "\n"
     " Variables whose address is taken, and functions that use goto,\n"
     " setjmp or volatile, are left alone by the data-flow passes.  With\n"
     " Source Debugging on, the passes that change statements or variable\n"
     " values (propagation, dead stores, CSE, unreachable code) are skipped\n"
     " so that stepping and watches match the source.\n"
     "\n"
     " Jump optimization           [-O] removes jumps to the next instruction\n"
     "                             and uses 2-byte jumps where the target is\n"
     "                             near.\n"
     "\n"
     " Suppress redundant loads    [-Z] constant operands go straight into a\n"
     "                             register instead of being saved and\n"
     "                             restored.\n"
     "\n"
     "## Options|Compiler|Source\n"
     "\n"
     " Keywords: ANSI              [-A] no // comments; near, far, huge, cdecl,\n"
     "                             pascal and interrupt are ordinary\n"
     "                             identifiers.\n"
     "\n"
     "## Options|Linker\n"
     "\n"
     " ELF symbol table            [-lt] writes .symtab / .strtab: nm, gdb,\n"
     "                             objdump and perf show function and variable\n"
     "                             names.\n"
     "\n"
     " Protect code (W^X)          [-lw] code is read+execute, data is\n"
     "                             read+write (two segments instead of one\n"
     "                             read+write+execute).\n"
     "\n"
     " Stack size (KB)             [-ls<KB>] sets the stack limit with setrlimit\n"
     "                             at start-up; 0 keeps the system default.\n"
     "                             With the stack test the same figure is the\n"
     "                             overflow limit.\n"
     "\n"
     "## Options|Compiler|Messages\n"
     "\n"
     " New warnings: Unreachable code [-rch], Ambiguous operators need\n"
     " parentheses [-amb], Structure passed by value [-stv], Condition is\n"
     " always true/false [-cln], Mixing pointers to signed and unsigned char\n"
     " [-ucp], Non-portable pointer comparison [-cpt].\n"
     "\n"
     " Use -wxxx to enable a warning and -w-xxx to disable it.\n"
     "\n"
     " See also: {Compiler options}, {Libraries and linking}\n"},

    {"Messages and errors", "message error warning fatal linker",
     " Messages and errors\n"
     "\n"
     " Compiler messages appear in the Message window. Use Enter to edit the\n"
     " source line of the selected message, Space to view it, Alt-F7 and\n"
     " Alt-F8 to step through the messages.\n"
     "\n"
     "## Kinds of message\n"
     "\n"
     " Error      the program cannot be compiled until this is fixed.\n"
     "\n"
     " Warning    the program compiled, but something looks suspicious.\n"
     "\n"
     " Fatal      the compiler cannot continue.\n"
     "\n"
     " Linker     a symbol could not be resolved (for instance no main).\n"
     "\n"
     "## Common messages\n"
     "\n"
     " Undefined symbol 'x'\n"
     "            the name was not declared.\n"
     "\n"
     " Statement missing ;\n"
     "            a semicolon is missing.\n"
     "\n"
     " Call to function 'f' with no prototype\n"
     "            declare the function before using it (usually by including\n"
     "            the right header).\n"
     "\n"
     " Function should return a value\n"
     "            a non-void function ends without return.\n"
     "\n"
     " 'x' is declared but never used\n"
     "            a variable can be removed.\n"},

    {"Projects", "project prj fcp",
     " Projects\n"
     "\n"
     " A project lists several source files that make up one program.\n"
     "\n"
     "## Working with a project\n"
     "\n"
     " Project|Open project...   loads (or creates) a .FCP file.\n"
     "\n"
     " Project|Add item         adds a source file.\n"
     "\n"
     " Project|Delete item      removes the selected file.\n"
     "\n"
     " Project|Local options   settings of the selected item (see below).\n"
     "\n"
     " Window|Project           shows the list.\n"
     "\n"
     "## Compiling\n"
     "\n"
     " All files of the project are compiled together; a function or global\n"
     " variable can be used from any file of the project provided it is\n"
     " declared before use. Declare shared items in a header file.\n"
     "\n"
     "## Local options\n"
     "\n"
     " Project|Local options sets the following for the selected item. The\n"
     " settings are stored in the .FCP file, indented below the file name.\n"
     "\n"
     " Command line options   compiler switches such as -O or -DDEBUG. A\n"
     "                        project is compiled in one run, so the switches\n"
     "                        of all items apply to the whole build.\n"
     "\n"
     " Output path            the executable file, or an existing directory to\n"
     "                        put it in. Missing directories are created. It\n"
     "                        takes the place of Options|Directories.\n"
     "\n"
     " Exclude debug info     functions of this file get no line table, so the\n"
     "                        debugger cannot step into them.\n"
     "\n"
     " Exclude from link      the file is not compiled into the program.\n"},

    {"Language reference", "c language",
     " Language reference\n"
     "\n"
     " Flash C compiles the C language (ANSI C89) as used by DOS-era\n"
     " compilers, with a few C99 conveniences.\n"
     "\n"
     "## Types\n"
     "\n"
     " void char short int long, signed and unsigned variants, float and\n"
     " double, pointers, arrays, struct, union, enum, typedef and function\n"
     " pointers.\n"
     "\n"
     " Sizes: char 1, short 2, int 4, long 8, pointer 8 bytes.\n"
     "\n"
     " Structures can be passed and returned by value. Bit fields (int and\n"
     " unsigned int, C89) are supported. Not yet: compound literals.\n"
     "\n"
     "## Statements\n"
     "\n"
     " if else while do for switch case default break continue goto return.\n"
     " Declarations may appear anywhere in a block.\n"
     "\n"
     "## Operators\n"
     "\n"
     " The complete C set including ?: , && || and the assignment operators.\n"
     " Pointer arithmetic is scaled as usual.\n"
     "\n"
     "## Initializers\n"
     "\n"
     " Scalars, arrays, structs and strings, with nesting.\n"
     "\n"
     "## Preprocessor\n"
     "\n"
     " #include #define (also with arguments, # and ##) #undef #if #ifdef\n"
     " #ifndef #elif #else #endif #error and #pragma startup / exit (other\n"
     " pragmas are ignored).\n"
     "\n"
     "## DOS keywords\n"
     "\n"
     " near, far, huge, cdecl and pascal are accepted and ignored.\n"},

    {"Library: stdio",
     "stdio clearerr fclose fcloseall fdopen feof ferror fflush fgetc fgetchar fgetpos fgets "
     "fileno flushall fopen fprintf fputc fputchar fputs fread freopen fscanf fseek fsetpos "
     "_fsopen ftell fwrite getc getchar gets getw perror printf putc putchar puts putw remove "
     "rename rewind rmtmp scanf setbuf setvbuf snprintf sprintf sscanf tempnam tmpfile tmpnam "
     "ungetc unlink vfprintf vfscanf vprintf vscanf vsnprintf vsprintf vsscanf",
     " Library: stdio\n"
     "\n"
     " Header files: <stdio.h>\n"
     "\n"
     " Formatted and character I/O on streams. stdin, stdout and stderr are\n"
     " predefined. Output to a terminal is flushed at each new-line;\n"
     " everything is flushed when the program ends.\n"
     "\n"
     "## Functions\n"
     "\n"
     " clearerr fclose fcloseall fdopen feof ferror fflush fgetc fgetchar\n"
     " fgetpos fgets fileno flushall fopen fprintf fputc fputchar fputs\n"
     " fread freopen fscanf fseek fsetpos _fsopen ftell fwrite getc getchar\n"
     " gets getw perror printf putc putchar puts putw remove rename rewind\n"
     " rmtmp scanf setbuf setvbuf snprintf sprintf sscanf tempnam tmpfile\n"
     " tmpnam ungetc unlink vfprintf vfscanf vprintf vscanf vsnprintf\n"
     " vsprintf vsscanf\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: string",
     "string memccpy memchr memcmp memcpy memicmp memmove memset movmem setmem stpcpy strcat "
     "strchr strcmp strcmpi strcoll strcpy strcspn strdup strerror _strerror stricmp strlen strlwr "
     "strncat strncmp strncpy strnicmp strnset strpbrk strrchr strrev strset strspn strstr strtok "
     "strupr strxfrm",
     " Library: string\n"
     "\n"
     " Header files: <string.h> <mem.h>\n"
     "\n"
     " String and memory block functions (stricmp, strupr, movmem, setmem\n"
     " ... are included).\n"
     "\n"
     "## Functions\n"
     "\n"
     " memccpy memchr memcmp memcpy memicmp memmove memset movmem setmem\n"
     " stpcpy strcat strchr strcmp strcmpi strcoll strcpy strcspn strdup\n"
     " strerror _strerror stricmp strlen strlwr strncat strncmp strncpy\n"
     " strnicmp strnset strpbrk strrchr strrev strset strspn strstr strtok\n"
     " strupr strxfrm\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: stdlib",
     "stdlib abort abs atexit atof atoi atol bsearch calloc div ecvt exit _exit _cexit _c_exit "
     "fcvt free gcvt getenv itoa labs ldiv lfind lsearch ltoa malloc mblen mbstowcs mbtowc putenv "
     "qsort rand realloc srand strtod strtol strtoul swab system ultoa wcstombs wctomb _rotl _rotr "
     "_lrotl _lrotr _fullpath _makepath _splitpath _searchenv brk sbrk coreleft heapcheck "
     "heapcheckfree heapchecknode heapfillfree heapwalk",
     " Library: stdlib\n"
     "\n"
     " Header files: <stdlib.h> <alloc.h>\n"
     "\n"
     " Memory allocation, conversions, sorting, environment and program\n"
     " termination. rand() uses the classic generator: srand(1) gives 346,\n"
     " 130, 10982 ... RAND_MAX is 32767. exit runs atexit handlers and\n"
     " flushes the files.\n"
     "\n"
     "## Functions\n"
     "\n"
     " abort abs atexit atof atoi atol bsearch calloc div ecvt exit _exit\n"
     " _cexit _c_exit fcvt free gcvt getenv itoa labs ldiv lfind lsearch\n"
     " ltoa malloc mblen mbstowcs mbtowc putenv qsort rand realloc srand\n"
     " strtod strtol strtoul swab system ultoa wcstombs wctomb _rotl _rotr\n"
     " _lrotl _lrotr _fullpath _makepath _splitpath _searchenv brk sbrk\n"
     " coreleft heapcheck heapcheckfree heapchecknode heapfillfree heapwalk\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: ctype",
     "ctype isalnum isalpha isascii iscntrl isdigit isgraph islower isprint ispunct isspace "
     "isupper isxdigit toascii tolower toupper _tolower _toupper",
     " Library: ctype\n"
     "\n"
     " Header files: <ctype.h>\n"
     "\n"
     " Character classification and conversion.\n"
     "\n"
     "## Functions\n"
     "\n"
     " isalnum isalpha isascii iscntrl isdigit isgraph islower isprint\n"
     " ispunct isspace isupper isxdigit toascii tolower toupper _tolower\n"
     " _toupper\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: math",
     "math matherr abs labs acos asin atan atan2 atof ceil cos cosh exp fabs floor fmod frexp "
     "hypot ldexp log log10 modf poly pow pow10 sin sinh sqrt tan tanh acosl asinl atanl atan2l "
     "ceill cosl coshl expl fabsl floorl fmodl frexpl hypotl ldexpl logl log10l modfl polyl powl "
     "pow10l sinl sinhl sqrtl tanl tanhl trunc round log2 cbrt fmax fmin isnan isinf _clear87 "
     "_control87 _fpreset _status87",
     " Library: math\n"
     "\n"
     " Header files: <math.h> <float.h>\n"
     "\n"
     " Mathematics. Domain and range errors call matherr() and print a\n"
     " message such as 'sqrt: DOMAIN error'. Linking floating point is\n"
     " automatic when the program uses float or double;\n"
     " Options|Libraries|Math library can switch it off.\n"
     "\n"
     "## Functions\n"
     "\n"
     " matherr abs labs acos asin atan atan2 atof ceil cos cosh exp fabs\n"
     " floor fmod frexp hypot ldexp log log10 modf poly pow pow10 sin sinh\n"
     " sqrt tan tanh acosl asinl atanl atan2l ceill cosl coshl expl fabsl\n"
     " floorl fmodl frexpl hypotl ldexpl logl log10l modfl polyl powl pow10l\n"
     " sinl sinhl sqrtl tanl tanhl trunc round log2 cbrt fmax fmin isnan\n"
     " isinf _clear87 _control87 _fpreset _status87\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: time",
     "time asctime clock ctime difftime gmtime localtime mktime strftime time stime tzset _strdate "
     "_strtime ftime utime",
     " Library: time\n"
     "\n"
     " Header files: <time.h> <dos.h> <sys/timeb.h>\n"
     "\n"
     " Calendar time. The time zone comes from the TZ variable (for example\n"
     " EST5EDT); without it local time is UTC. Also: the DOS-style date and\n"
     " time calls of dos.h.\n"
     "\n"
     "## Functions\n"
     "\n"
     " asctime clock ctime difftime gmtime localtime mktime strftime time\n"
     " stime tzset _strdate _strtime ftime utime\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: io and dir",
     "io dir access chmod _chmod chsize close _close creat _creat creatnew creattemp dup dup2 eof "
     "filelength getftime ioctl isatty lock locking lseek mktemp open _open read _read remove "
     "rename setftime setmode sopen tell umask unlink unlock write _write stat fstat chdir "
     "findfirst findnext fnmerge fnsplit getcurdir getcwd getdisk mkdir rmdir searchpath setdisk "
     "opendir readdir closedir rewinddir _chdrive _getdcwd _getdrive",
     " Library: io and dir\n"
     "\n"
     " Header files: <io.h> <fcntl.h> <sys/stat.h> <dir.h> <dirent.h>\n"
     "\n"
     " Low-level file handles, file status, directories and wildcard\n"
     " searches (findfirst/findnext), path splitting and merging.\n"
     "\n"
     "## Functions\n"
     "\n"
     " access chmod _chmod chsize close _close creat _creat creatnew\n"
     " creattemp dup dup2 eof filelength getftime ioctl isatty lock locking\n"
     " lseek mktemp open _open read _read remove rename setftime setmode\n"
     " sopen tell umask unlink unlock write _write stat fstat chdir\n"
     " findfirst findnext fnmerge fnsplit getcurdir getcwd getdisk mkdir\n"
     " rmdir searchpath setdisk opendir readdir closedir rewinddir _chdrive\n"
     " _getdcwd _getdrive\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: process",
     "process abort _cexit _c_exit execl execle execlp execlpe execv execve execvp execvpe exit "
     "_exit getpid spawnl spawnle spawnlp spawnlpe spawnv spawnve spawnvp spawnvpe system signal "
     "raise",
     " Library: process\n"
     "\n"
     " Header files: <process.h> <signal.h> <setjmp.h>\n"
     "\n"
     " Running other programs (system, spawn..., exec...), signals and non-\n"
     " local jumps.\n"
     "\n"
     "## Functions\n"
     "\n"
     " abort _cexit _c_exit execl execle execlp execlpe execv execve execvp\n"
     " execvpe exit _exit getpid spawnl spawnle spawnlp spawnlpe spawnv\n"
     " spawnve spawnvp spawnvpe system signal raise\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Library: conio and dos",
     "conio dos cgets clreol clrscr cprintf cputs cscanf delline getch getche gettextinfo getpass "
     "gotoxy highvideo insline kbhit lowvideo normvideo putch _setcursortype textattr "
     "textbackground textcolor textmode ungetch wherex wherey window delay sleep sound nosound "
     "getdate gettime setdate settime dostounix unixtodos getdfree getpid unlink biostime bioskey "
     "biosequip biosmemory setlocale localeconv",
     " Library: conio and dos\n"
     "\n"
     " Header files: <conio.h> <dos.h> <bios.h>\n"
     "\n"
     " Console I/O through ANSI terminal sequences: windows, colours, cursor\n"
     " control, unbuffered keyboard input (extended keys return 0 followed\n"
     " by a scan code). DOS interrupts, ports, far memory,\n"
     " gettext/puttext/movetext and graphics are not available.\n"
     "\n"
     "## Functions\n"
     "\n"
     " cgets clreol clrscr cprintf cputs cscanf delline getch getche\n"
     " gettextinfo getpass gotoxy highvideo insline kbhit lowvideo normvideo\n"
     " putch _setcursortype textattr textbackground textcolor textmode\n"
     " ungetch wherex wherey window delay sleep sound nosound getdate\n"
     " gettime setdate settime dostounix unixtodos getdfree getpid unlink\n"
     " biostime bioskey biosequip biosmemory setlocale localeconv\n"
     "\n"
     " See also: {Library functions}\n"},
    {"Libraries and linking",
     "library libraries link linker map wildargs pragma startup exit nolibs floating",
     " Libraries and linking\n"
     "\n"
     " Programs are linked with the Flash C run-time library. Only the\n"
     " functions a program really uses are placed in the executable.\n"
     "\n"
     "## Libraries\n"
     "\n"
     " Start-up code        always linked (argc/argv/environ, exit, abort).\n"
     "\n"
     " Standard library     Options|Libraries|Standard run-time library.\n"
     "\n"
     " Math library         Options|Libraries|Math library (math.h, %f %e %g,\n"
     "                      atof / strtod); linked when floating point is\n"
     "                      used.\n"
     "\n"
     " Wildcard arguments   Options|Libraries|Wildcard arguments expands * and\n"
     "                      ? in command-line arguments (command line:\n"
     "                      wildargs.obj).\n"
     "\n"
     "## Options|Linker\n"
     "\n"
     " Map file             Off / Segments / Publics / Detailed. Writes\n"
     "                      <program>.map listing every linked function and\n"
     "                      its library module.\n"
     "\n"
     " Default libraries    unchecked = link only the start-up code (-ln).\n"
     "\n"
     "## Options|Compiler|Code generation|Floating point\n"
     "\n"
     " None                 floating-point formats are not linked;\n"
     "                      printf(\"%f\") stops with 'floating point formats not\n"
     "                      linked'.\n"
     "\n"
     " Emulation / 8087     both use the built-in floating-point code.\n"
     "\n"
     "## Start-up and exit functions\n"
     "\n"
     " #pragma startup fn [priority]    run fn before main.\n"
     "\n"
     " #pragma exit fn [priority]       run fn after main returns or exit() is\n"
     "                                  called.\n"
     "\n"
     " Priorities are 0-255 (default 100). Start-up functions run from the\n"
     " lowest number up, exit functions from the highest down.\n"
     "\n"
     " See also: {Library: stdio}, {Library: math}\n"},

    {"Source-level debugger",
     "debug debugger breakpoint trace step watch inspect evaluate call stack go to cursor register",
     " Source-level debugger\n"
     "\n"
     "## Commands\n"
     "\n"
     "   Ctrl+F9   Run to the next breakpoint     Ctrl+F2   Program reset\n"
     "   F4        Go to cursor                   F7        Trace into\n"
     "   F8        Step over                      Ctrl+F8   Toggle breakpoint\n"
     "   Alt+F4    Inspect                        Ctrl+F4   Evaluate/modify\n"
     "   Ctrl+F3   Call stack                     Ctrl+F7   Add watch\n"
     "\n"
     "## Debug information\n"
     "\n"
     " The program is compiled with debug information when Options|Debugger|\n"
     " Source debugging is On.\n"
     "\n"
     "## Stepping\n"
     "\n"
     " The first Trace into / Step over starts the program and stops at the\n"
     " first statement of main; the green bar marks the statement that runs\n"
     " next.\n"
     "\n"
     "## Breakpoints\n"
     "\n"
     " Breakpoints (red lines) can have a condition (a C expression) and a\n"
     " pass count: Debug|Breakpoints.\n"
     "\n"
     "## Expressions\n"
     "\n"
     " Expressions may use local and global variables, ., ->, [], *, &, casts,\n"
     " sizeof and assignment. A format suffix selects the display: total,x\n"
     " (hex), ,d (decimal), ,c (character).\n"
     "\n"
     "## The program screen\n"
     "\n"
     " Display swapping (Options|Debugger) selects when the program screen is\n"
     " shown while stepping; Alt+F5 shows the user screen at any time.\n"
     "\n"
     " See also: {Differences from classic compilers}\n"},

    {"Differences from classic compilers", "dos difference int 64 float memory model far near",
     " Differences from classic DOS compilers\n"
     "\n"
     "## Target\n"
     "\n"
     " The target is 64-bit Linux, so int is 32 bits, long and pointers are 64\n"
     " bits. Memory models, near / far pointers, overlays and the floating-point\n"
     " emulator do not apply (the settings are kept for compatibility).\n"
     " Graphics, BIOS and interrupt calls are not provided.\n"
     "\n"
     "## Language\n"
     "\n"
     " Only C (ANSI C89) is supported. C++ is not supported, so there are no\n"
     " C++ options and files with C++ extensions are not compiled as C++.\n"
     "\n"
     "## Linking\n"
     "\n"
     " There is no separate object-file / link step: the compiler writes the\n"
     " executable directly. Compile and Link therefore produce the same file.\n"
     "\n"
     "## Debugger\n"
     "\n"
     " The source-level debugger runs the program under ptrace (see\n"
     " {Source-level debugger}); it needs Options|Debugger|Source debugging.\n"
     "\n"
     "## Library\n"
     "\n"
     " The standard library is built in (see {Libraries and linking}). Programs\n"
     " get their header files from the compiler itself, and your own include\n"
     " directories (Options|Directories) are searched first.\n"},

    {"About Flash C", "about version",
     " Flash C " FC_VERSION "\n"
     "\n"
     " A compact C compiler and text-mode IDE for Linux.\n"
     " The compiler translates C to native x86-64 executables.\n"
     "\n"
     " See {Contents} for a list of topics.\n"},
    {NULL, NULL, NULL}};

/* ------------------------------------------------------------------ topic database */
typedef struct {
    char *title, *keys, *alias, *text;
} Tp;

static Tp *tps;
static int ntps, captps;
static int ext_loaded, ext_files, ext_tried;

/* Returns the index of the topic with the given title or alias, or -1. */
static int tp_find(const char *name)
{
    for (int i = 0; i < ntps; i++) {
        if (!strcasecmp(tps[i].title, name)) {
            return i;
        }
    }
    for (int i = 0; i < ntps; i++) { /* aliases are separated by ';' */
        const char *a = tps[i].alias;
        while (a && *a) {
            while (*a == ' ' || *a == ';') {
                a++;
            }
            const char *e = a;
            while (*e && *e != ';') {
                e++;
            }
            const char *t = e;
            while (t > a && t[-1] == ' ') {
                t--;
            }
            if ((size_t)(t - a) == strlen(name) && !strncasecmp(a, name, t - a)) {
                return i;
            }
            a = e;
        }
    }
    return -1;
}

/* Adds an empty topic and returns its index. */
static int tp_new(const char *title)
{
    int i = tp_find(title);
    if (i >= 0 && !strcasecmp(tps[i].title, title)) { /* replaces a topic of the same title */
        free(tps[i].keys);
        free(tps[i].alias);
        free(tps[i].text);
        tps[i].keys = strdup("");
        tps[i].alias = strdup("");
        tps[i].text = strdup("");
        return i;
    }
    if (ntps == captps) {
        captps = captps ? captps * 2 : 256;
        tps = realloc(tps, captps * sizeof *tps);
    }
    tps[ntps].title = strdup(title);
    tps[ntps].keys = strdup("");
    tps[ntps].alias = strdup("");
    tps[ntps].text = strdup("");
    return ntps++;
}

/* Appends a line of text to topic i. */
static void tp_append(int i, const char *s)
{
    size_t a = strlen(tps[i].text), b = strlen(s);
    tps[i].text = realloc(tps[i].text, a + b + 1);
    memcpy(tps[i].text + a, s, b + 1);
}

/* Fills the topic database with the built-in basic topics. */
static void init_db(void)
{
    if (ntps) {
        return;
    }
    for (int i = 0; basic[i].title; i++) {
        int k = tp_new(basic[i].title);
        free(tps[k].keys);
        tps[k].keys = strdup(basic[i].keys);
        free(tps[k].text);
        tps[k].text = strdup(basic[i].text);
    }
}

/* ---- external help files (*.fch) ----
 *   ;; comment
 *   @topic Title          starts a topic (an existing topic of that title is replaced)
 *   @append Title         adds the following text to an existing topic
 *   @keys w1 w2 ...       words found by Topic search (Ctrl+F1)
 *   @alias n1; n2         more names for the Index and for links
 *   @example ... @end     program text; shown in the example colour and copied by Edit|Copy example
 *   {Title} {label|Title} a link; a line that starts with @@ stands for a text line that starts
 * with @ */
static void load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[600];
    int cur = -1, in_ex = 0;
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = 0;
        }
        nl = strchr(line, '\r');
        if (nl) {
            *nl = 0;
        }
        if (!strncmp(line, ";;", 2)) {
            continue;
        }
        if (line[0] == '@' && line[1] != '@') {
            char *arg = strchr(line, ' ');
            if (arg) {
                *arg++ = 0;
                while (*arg == ' ') {
                    arg++;
                }
            } else {
                arg = line + strlen(line);
            }
            if (!strcmp(line, "@topic") && *arg) {
                cur = tp_new(arg);
                in_ex = 0;
            } else if (!strcmp(line, "@append") && *arg) {
                cur = tp_find(arg);
                in_ex = 0;
            } else if (cur >= 0 && !strcmp(line, "@keys")) {
                free(tps[cur].keys);
                tps[cur].keys = strdup(arg);
            } else if (cur >= 0 && !strcmp(line, "@alias")) {
                free(tps[cur].alias);
                tps[cur].alias = strdup(arg);
            } else if (!strcmp(line, "@example")) {
                in_ex = 1;
            } else if (!strcmp(line, "@end")) {
                in_ex = 0;
            }
            continue;
        }
        if (cur < 0) {
            continue;
        }
        char *body = line[0] == '@' ? line + 1 : line;
        if (in_ex) {
            tp_append(cur, "\x01");
        }
        tp_append(cur, body);
        tp_append(cur, "\n");
    }
    fclose(f);
    ext_files++;
}

/* qsort comparison for an array of strings. */
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Loads every *.fch file in a directory; returns the number of files read. */
static int load_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    char *names[256];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 256) {
        size_t l = strlen(de->d_name);
        if (l > 4 && !strcmp(de->d_name + l - 4, ".fch")) {
            names[n++] = strdup(de->d_name);
        }
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    for (int i = 0; i < n; i++) {
        char p[1100];
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        load_file(p);
        free(names[i]);
    }
    return n;
}

/* where the help files may be installed */
static void help_dirs(char dirs[][700], int *n)
{
    *n = 0;
    const char *env = getenv("FC_HELP");
    if (env && *env) {
        snprintf(dirs[(*n)++], 700, "%s", env);
    }
    char exe[600];
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (l > 0) {
        exe[l] = 0;
        char *sl = strrchr(exe, '/');
        if (sl) {
            *sl = 0;
        }
        snprintf(dirs[(*n)++], 700, "%s/help", exe);
        snprintf(dirs[(*n)++], 700, "%s/../share/flashc/help", exe);
        snprintf(dirs[(*n)++], 700, "%s/../help", exe);
    }
    snprintf(dirs[(*n)++], 700, "%s", "/usr/local/share/flashc/help");
    snprintf(dirs[(*n)++], 700, "%s", "/usr/share/flashc/help");
    snprintf(dirs[(*n)++], 700, "%s", "help");
}

/* Loads the *.fch help files from the first directory in the search path that has any. */
static void help_load_external(void)
{
    if (ext_tried) {
        return;
    }
    ext_tried = 1;
    init_db();
    char dirs[10][700];
    int nd;
    help_dirs(dirs, &nd);
    for (int i = 0; i < nd && !ext_loaded; i++) {
        if (load_dir(dirs[i]) > 0) {
            ext_loaded = 1;
        }
    }
    if (!ext_loaded) {
        int c = tp_find("Contents");
        if (c >= 0) {
            tp_append(c, "\n The library reference and the language reference are not installed\n "
                         "(no *.fch files were found, see the help directory); only the basic\n "
                         "documentation is available.\n");
        }
    }
}

/* ------------------------------------------------------------------ parsed topic */
enum {
    PL_TEXT,
    PL_TITLE,
    PL_HEAD,
    PL_RULE
}; /* kinds of help lines: body, page title, section heading, rule under the title */

/* One displayed line of a parsed topic. */
typedef struct {
    char *text;
    int example, kind;
} PLine;

typedef struct {
    int row, col, len;
    char target[100];
} PLink;

typedef struct HS {
    int topic, top, link, hist[32], nhist;
    int follow; /* a key moved the selected link: scroll it into view */
    PLine *lines;
    int nlines;
    PLink *links;
    int nlinks;
    int sel_active, ar, ac, br, bc; /* text selection: anchor and current end (line, column) */
    int dragged, px, py; /* mouse: the pointer moved since the press; where it went down */
} HS;

/* Frees the parsed lines and links of the displayed topic. */
static void parse_free(HS *h)
{
    for (int i = 0; i < h->nlines; i++) {
        free(h->lines[i].text);
    }
    free(h->lines);
    free(h->links);
    h->lines = NULL;
    h->links = NULL;
    h->nlines = h->nlinks = 0;
    h->sel_active = 0;
}

/* Appends a line of the given kind to the parsed topic, growing the array as needed. */
static PLine *add_line(HS *h, int *cap, int kind, const char *text)
{
    if (h->nlines == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        h->lines = realloc(h->lines, *cap * sizeof *h->lines);
    }
    PLine *L = &h->lines[h->nlines++];
    L->example = 0;
    L->kind = kind;
    L->text = strdup(text);
    return L;
}

/* Topic text -> display lines.  The first line is the page title (drawn in the title colour with a
 * rule below it); a line "## Name" is a section heading and always has a blank line before it.
 * {Topic} and {text|Topic} are links;
 * "{ " is a literal brace. */
static void parse_topic(HS *h)
{
    parse_free(h);
    const char *text = tps[h->topic].text;
    int caplines = 0, caplinks = 0, first = 1;
    for (const char *p = text; *p;) {
        const char *e = strchr(p, '\n');
        if (!e) {
            e = p + strlen(p);
        }
        int example = *p == 1;
        const char *q = p + (example ? 1 : 0);
        int kind = PL_TEXT;
        if (!example && !strncmp(q, "## ", 3)) {
            kind = PL_HEAD;
            q += 3;
        } else if (!example && first && e > q) {
            kind = PL_TITLE;
        }
        if (!example && kind == PL_TEXT && e == q && h->nlines &&
            h->lines[h->nlines - 1].kind == PL_HEAD) {
            p = *e ? e + 1 : e;
            continue;
        } /* no blank line right after a heading */
        if (kind == PL_HEAD && h->nlines && h->lines[h->nlines - 1].text[0]) {
            add_line(h, &caplines, PL_TEXT, "");
        }
        char *out = malloc(e - q + 3);
        int o = 0;
        if (kind == PL_HEAD) {
            out[o++] = ' ';
        }
        int row = h->nlines;
        for (; q < e; q++) {
            if (!example && *q == '{' && q + 1 < e &&
                q[1] != ' ') { /* "{ " is a literal brace, "{Topic}" a link */
                const char *c = memchr(q, '}', e - q);
                if (c) {
                    const char *bar = memchr(q, '|', c - q);
                    const char *ls = q + 1, *le = bar ? bar : c;
                    const char *ts = bar ? bar + 1 : q + 1;
                    if (h->nlinks == caplinks) {
                        caplinks = caplinks ? caplinks * 2 : 32;
                        h->links = realloc(h->links, caplinks * sizeof *h->links);
                    }
                    PLink *k = &h->links[h->nlinks++];
                    k->row = row;
                    k->col = o;
                    k->len = (int)(le - ls);
                    int tl = (int)(c - ts);
                    if (tl > 99) {
                        tl = 99;
                    }
                    memcpy(k->target, ts, tl);
                    k->target[tl] = 0;
                    memcpy(out + o, ls, le - ls);
                    o += (int)(le - ls);
                    q = c;
                    continue;
                }
            }
            out[o++] = *q;
        }
        out[o] = 0;
        PLine *L = add_line(h, &caplines, kind, out);
        L->example = example;
        free(out);
        if (kind == PL_TITLE) { /* rule under the page title */
            int n = (int)strlen(h->lines[row].text);
            char *r = malloc(n + 2);
            memset(r, 0, n + 2);
            for (int i = 1; i < n; i++) {
                r[i - 1] = '-';
            }
            add_line(h, &caplines, PL_RULE, r);
            free(r);
            first = 0;
        } else if (e > p + (example ? 1 : 0)) {
            first = 0;
        }
        p = *e ? e + 1 : e;
    }
}

/*
 * Shows a topic in the help window; with push set the current one is remembered for
 * Help|Previous topic.
 */
static void help_goto(Win *w, int topic, int push)
{
    HS *h = w->data;
    if (push && h->topic >= 0 && h->nhist < 32) {
        h->hist[h->nhist++] = h->topic;
    }
    h->topic = topic;
    h->top = 0;
    h->link = 0;
    snprintf(w->title, sizeof w->title, "Help: %s", tps[topic].title);
    parse_topic(h);
}

/* Returns the help window, creating it if necessary (and loading the help files). */
static Win *help_window(void)
{
    help_load_external();
    Win *w = win_find_kind(W_HELP);
    if (w) {
        win_activate(w);
        return w;
    }
    int ww = SW > 80 ? 76 : 72, hh = SH - 6;
    if (nwins >= MAXWIN) {
        return NULL;
    }
    w = calloc(1, sizeof *w);
    w->kind = W_HELP;
    w->x = (SW - ww) / 2;
    w->y = 2;
    w->w = ww;
    w->h = hh;
    w->error_line = -1;
    HS *h = calloc(1, sizeof *h);
    h->topic = -1;
    w->data = h;
    wins[nwins++] = w;
    return w;
}

/* Frees the state of a help window. */
void help_free(Win *w)
{
    HS *h = w->data;
    if (h) {
        parse_free(h);
    }
}

/* Shows the topic with the given title (the first topic if it does not exist). */
void help_show(const char *topic)
{
    Win *w = help_window();
    if (!w) {
        return;
    }
    int t = tp_find(topic);
    help_goto(w, t < 0 ? 0 : t, ((HS *)w->data)->topic >= 0);
}

/* Shows the Contents topic. */
void help_contents(void)
{
    help_show("Contents");
}

/* ---- Index: every topic title and alias, with type-ahead */
typedef struct {
    char *name;
    int topic;
} IEnt;

static IEnt *ient;
static int nient;

/* qsort comparison for index entries (case-insensitive by name). */
static int cmp_ient(const void *a, const void *b)
{
    return strcasecmp(((const IEnt *)a)->name, ((const IEnt *)b)->name);
}

/* Builds the sorted Index list from the topic titles and keywords. */
static void build_index(void)
{
    for (int i = 0; i < nient; i++) {
        free(ient[i].name);
    }
    free(ient);
    ient = NULL;
    nient = 0;
    int cap = 0;
    for (int i = 0; i < ntps; i++) {
        const char *a = tps[i].alias;
        for (int pass = 0; pass < 2; pass++) {
            const char *s = pass ? a : tps[i].title;
            while (s && *s) {
                const char *e = pass ? strchr(s, ';') : NULL;
                if (!e) {
                    e = s + strlen(s);
                }
                const char *b = s;
                while (b < e && *b == ' ') {
                    b++;
                }
                const char *t = e;
                while (t > b && t[-1] == ' ') {
                    t--;
                }
                if (t > b) {
                    if (nient == cap) {
                        cap = cap ? cap * 2 : 256;
                        ient = realloc(ient, cap * sizeof *ient);
                    }
                    ient[nient].name = strndup(b, t - b);
                    ient[nient].topic = i;
                    nient++;
                }
                if (!pass || !*e) {
                    break;
                }
                s = e + 1;
            }
        }
    }
    qsort(ient, nient, sizeof *ient, cmp_ient);
}

static char ix_text[100], ix_last[100];

static DItem *dlg_find_list(
    Dialog *d) /* the labelled input adds a label item, so the list is not at a fixed index */
{
    for (int i = 0; i < d->n; i++) {
        if (d->it[i].type == D_LIST) {
            return &d->it[i];
        }
    }
    return NULL;
}

/* Index dialog callback: selects the entry that matches the text typed so far. */
static void ix_change(Dialog *d)
{
    if (!strcmp(ix_text, ix_last)) {
        return;
    }
    snprintf(ix_last, sizeof ix_last, "%s", ix_text);
    DItem *l = dlg_find_list(d);
    if (!l) {
        return;
    }
    size_t n = strlen(ix_text);
    for (int i = 0; i < nient; i++) {
        if (!strncasecmp(ient[i].name, ix_text, n)) {
            l->sel = i;
            if (l->sel < l->top || l->sel >= l->top + l->h) {
                l->top = i;
            }
            break;
        }
    }
}

/* Shows the Index dialog.  Returns the chosen topic or -1. */
static int index_dialog(const char *initial)
{
    help_load_external();
    build_index();
    if (!nient) {
        return -1;
    }
    char **items = malloc(nient * sizeof *items);
    for (int i = 0; i < nient; i++) {
        items[i] = ient[i].name;
    }
    snprintf(ix_text, sizeof ix_text, "%s", initial ? initial : "");
    ix_last[0] = 1;
    ix_last[1] = 0;
    Dialog d;
    dlg_init(&d, " Index ");
    d.w = 44;
    d.h = SH > 22 ? 20 : 16;
    d.on_change = ix_change;
    dlg_input(&d, 3, 1, 36, "~S~earch for", ix_text, sizeof ix_text,
              "Type the first letters of a topic name");
    int lh = d.h - 9; /* two free rows between the list and the buttons */
    dlg_list(&d, 3, 4, 36, lh, items, nient,
             "Use cursor keys to examine the list; Enter shows the topic");
    dlg_button(&d, 3, d.h - 3, 10, "~S~how", cmOK, 1, NULL);
    dlg_button(&d, 15, d.h - 3, 10, "Cancel", cmCancel, 0, NULL);
    ix_change(&d);
    int r = dlg_run(&d);
    DItem *list = dlg_find_list(&d);
    int t = (r == cmOK && list && list->sel < nient) ? ient[list->sel].topic : -1;
    free(items);
    return t;
}

/* Shows the Index dialog and opens the chosen topic. */
void help_index(void)
{
    int t = index_dialog(NULL);
    if (t < 0) {
        return;
    }
    Win *w = help_window();
    if (!w) {
        return;
    }
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
                while (*p == ' ') {
                    p++;
                }
                const char *e = p;
                while (*e && *e != ' ') {
                    e++;
                }
                if ((size_t)(e - p) == wl && !strncasecmp(p, word, wl)) {
                    hit = 1;
                }
                p = e;
            }
            if (hit) {
                cand[n++] = i;
            }
        }
        if (n == 1) {
            Win *w = help_window();
            if (w) {
                help_goto(w, cand[0], ((HS *)w->data)->topic >= 0);
            }
            return;
        }
        if (n > 1) {
            char **items = malloc(n * sizeof *items);
            for (int i = 0; i < n; i++) {
                items[i] = tps[cand[i]].title;
            }
            Dialog d;
            dlg_init(&d, " Topic search ");
            d.w = 44;
            d.h = n + 6 > 18 ? 18 : n + 6;
            dlg_list(&d, 3, 1, 36, d.h - 6, items, n, "Choose the topic to display");
            dlg_button(&d, 3, d.h - 3, 10, "~S~how", cmOK, 1, NULL);
            dlg_button(&d, 15, d.h - 3, 10, "Cancel", cmCancel, 0, NULL);
            int r = dlg_run(&d), sel = d.it[0].sel;
            free(items);
            if (r == cmOK) {
                Win *w = help_window();
                if (w) {
                    help_goto(w, cand[sel], ((HS *)w->data)->topic >= 0);
                }
            }
            return;
        }
        int t = index_dialog(word);
        if (t >= 0) {
            Win *w = help_window();
            if (w) {
                help_goto(w, t, ((HS *)w->data)->topic >= 0);
            }
        }
        return;
    }
    help_contents();
}

/* Returns to the previously shown topic (Help|Previous topic). */
void help_prev(void)
{
    Win *w = win_find_kind(W_HELP);
    if (!w) {
        help_contents();
        return;
    }
    HS *h = w->data;
    if (h->nhist) {
        int t = h->hist[--h->nhist];
        help_goto(w, t, 0);
    }
    win_activate(w);
}

/* the program text of the current topic, for Edit|Copy example (malloc'd, NULL if there is none) */
char *help_example_text(void)
{
    Win *w = win_find_kind(W_HELP);
    if (!w || win_active() != w) {
        return NULL;
    }
    HS *h = w->data;
    if (!h || h->topic < 0) {
        return NULL;
    }
    size_t n = 0;
    for (int i = 0; i < h->nlines; i++) {
        if (h->lines[i].example) {
            n += strlen(h->lines[i].text) + 1;
        }
    }
    if (!n) {
        return NULL;
    }
    char *r = malloc(n + 1);
    size_t o = 0;
    for (int i = 0; i < h->nlines; i++) {
        if (h->lines[i].example) {
            o += sprintf(r + o, "%s\n", h->lines[i].text);
        }
    }
    return r;
}

/* scroll the text by delta lines (mouse wheel, scroll bar); the selected link follows the view */
void help_scroll(Win *w, int delta)
{
    HS *h = w->data;
    int hh = w->h - 2, max = h->nlines - hh;
    if (max < 0) {
        max = 0;
    }
    h->top += delta;
    if (h->top > max) {
        h->top = max;
    }
    if (h->top < 0) {
        h->top = 0;
    }
    if (!h->nlinks) {
        return;
    }
    int k = h->link;
    while (k < h->nlinks - 1 && h->links[k].row < h->top) {
        k++;
    }
    while (k > 0 && h->links[k].row >= h->top + hh) {
        k--;
    }
    h->link = k;
}

/* click on the vertical scroll bar */
void help_bar_click(Win *w, int y)
{
    int hh = w->h - 2;
    if (y <= w->y + 1) {
        help_scroll(w, -1);
    } else if (y >= w->y + w->h - 2) {
        help_scroll(w, 1);
    } else if (y < w->y + w->h / 2) {
        help_scroll(w, -(hh - 1));
    } else {
        help_scroll(w, hh - 1);
    }
}

/* the selection as an ordered range; the end column is exclusive and both end cells are included */
static void sel_range(HS *h, int *sr, int *sc, int *er, int *ec)
{
    if (h->ar < h->br || (h->ar == h->br && h->ac <= h->bc)) {
        *sr = h->ar;
        *sc = h->ac;
        *er = h->br;
        *ec = h->bc + 1;
    } else {
        *sr = h->br;
        *sc = h->bc;
        *er = h->ar;
        *ec = h->ac + 1;
    }
}

/* Draws the help window: text, links, selection and scroll bar. */
void help_draw(Win *w, int active)
{
    HS *h = w->data;
    uint8_t norm = pal[P_HELP_NORM];
    scr_fill(w->x, w->y, w->w, w->h, ' ', norm);
    scr_box(w->x, w->y, w->w, w->h, active ? BOX_DOUBLE : BOX_SINGLE, pal[P_HELP_BORDER]);
    char t[140];
    snprintf(t, sizeof t, " %s ", w->title);
    if ((int)strlen(t) > w->w - 12) {
        t[w->w - 12] = 0;
    }
    scr_str(w->x + (w->w - (int)strlen(t)) / 2, w->y, t, pal[P_HELP_BORDER]);
    scr_put(w->x + 1, w->y, '[', pal[P_HELP_BORDER]);
    scr_put(w->x + 2, w->y, 0x25A0, pal[P_ED_ICON]);
    scr_put(w->x + 3, w->y, ']', pal[P_HELP_BORDER]);
    int hh = w->h - 2, wd = w->w - 2;
    if (h->link >= h->nlinks) {
        h->link = h->nlinks ? h->nlinks - 1 : 0;
    }
    if (h->nlinks && h->follow) {
        int r = h->links[h->link].row;
        if (r < h->top) {
            h->top = r;
        }
        if (r >= h->top + hh) {
            h->top = r - hh + 1;
        }
    }
    if (h->top > h->nlines - hh) {
        h->top = h->nlines - hh;
    }
    if (h->top < 0) {
        h->top = 0;
    }
    if (h->nlinks && !h->follow) { /* keep the selection on a link that can be seen */
        int k = h->link;
        while (k < h->nlinks - 1 && h->links[k].row < h->top) {
            k++;
        }
        while (k > 0 && h->links[k].row >= h->top + hh) {
            k--;
        }
        h->link = k;
    }
    h->follow = 0;
    for (int r = 0; r < hh && h->top + r < h->nlines; r++) {
        PLine *L = &h->lines[h->top + r];
        int y = w->y + 1 + r;
        if (L->example) {
            scr_fill(w->x + 1, y, wd, 1, ' ', pal[P_HELP_EXAMPLE]);
        }
        if (L->kind == PL_RULE) {
            int n = (int)strlen(L->text);
            for (int c = 0; c < n && c < wd; c++) {
                scr_put(w->x + 2 + c, y, 0x2500,
                        pal[P_HELP_BORDER]); /* thin line under the title */
            }
            continue;
        }
        uint8_t at = L->example           ? pal[P_HELP_EXAMPLE]
                     : L->kind == PL_TEXT ? norm
                                          : pal[P_HELP_TITLE];
        scr_strn(w->x + 1, y, L->text, wd, at);
    }
    if (w->h > 4) { /* scroll bar */
        int sx = w->x + w->w - 1;
        scr_put(sx, w->y + 1, 0x25B2, pal[P_ED_ARROWS]);
        scr_put(sx, w->y + w->h - 2, 0x25BC, pal[P_ED_ARROWS]);
        for (int yy = w->y + 2; yy < w->y + w->h - 2; yy++) {
            scr_put(sx, yy, 0x2592, pal[P_ED_SCROLL]);
        }
        int span = w->h - 5, max = h->nlines - hh;
        if (span > 0) {
            scr_put(sx, w->y + 2 + (max > 0 ? (int)((long)h->top * span / max) : 0), 0x25A0,
                    pal[P_ED_THUMB]);
        }
    }
    if (h->sel_active) {
        int sr, sc, er, ec;
        sel_range(h, &sr, &sc, &er, &ec);
        for (int r = 0; r < hh && h->top + r < h->nlines; r++) {
            int ln = h->top + r;
            if (ln < sr || ln > er || h->lines[ln].kind == PL_RULE) {
                continue;
            }
            const char *s = h->lines[ln].text;
            int len = (int)strlen(s);
            int c0 = ln == sr ? sc : 0, c1 = ln == er ? ec : len;
            if (c1 > len) {
                c1 = len;
            }
            for (int c = c0; c < c1 && c < wd; c++) {
                scr_put(w->x + 1 + c, w->y + 1 + r, (unsigned char)s[c], pal[P_ED_SEL]);
            }
        }
    }
    for (int i = 0; i < h->nlinks; i++) {
        PLink *k = &h->links[i];
        int r = k->row - h->top;
        if (r < 0 || r >= hh) {
            continue;
        }
        if (h->sel_active) {
            continue; /* the selection colours win over link colours */
        }
        uint8_t a = (i == h->link && active) ? pal[P_HELP_LINKSEL] : pal[P_HELP_LINK];
        const char *s = h->lines[k->row].text + k->col;
        for (int c = 0; c < k->len && k->col + c < wd; c++) {
            scr_put(w->x + 1 + k->col + c, w->y + 1 + r, (unsigned char)s[c], a);
        }
    }
}

/* Handles a key in the help window (scrolling, link selection, Enter to follow). */
int help_key(Win *w, int key)
{
    HS *h = w->data;
    int base = key & KMASK, nl = h->nlinks;
    if (key == KEY_ESC) {
        win_close(w);
        return 1;
    }
    if ((key & K_CTRL) && !(key & K_ALT)) {
        if (base == KEY_INS) {
            return help_copy(w);
        }
        if (base == 'a' && h->nlines) {
            h->sel_active = 1;
            h->ar = h->ac = 0;
            h->br = h->nlines - 1;
            h->bc = (int)strlen(h->lines[h->br].text);
            if (h->bc) {
                h->bc--;
            }
            return 1;
        }
        if (base == 'c') {
            return help_copy(w);
        }
    }
    switch (base) {
    case KEY_TAB:
        h->follow = 1;
        if (nl) {
            h->link = (h->link + ((key & K_SHIFT) ? nl - 1 : 1)) % nl;
        }
        return 1;
    case KEY_DOWN:
    case KEY_RIGHT:
        h->follow = 1;
        if (h->link < nl - 1 &&
            (base == KEY_RIGHT || h->links[h->link + 1].row <= h->top + w->h - 3)) {
            h->link++;
        } else {
            h->top++;
        }
        return 1;
    case KEY_UP:
    case KEY_LEFT:
        h->follow = 1;
        if (h->link > 0 && (base == KEY_LEFT || h->links[h->link - 1].row >= h->top)) {
            h->link--;
        } else if (h->top > 0) {
            h->top--;
        }
        return 1;
    case KEY_PGDN:
        h->top += w->h - 3;
        if (nl) {
            int k = h->link;
            while (k < nl - 1 && h->links[k].row < h->top) {
                k++;
            }
            h->link = k;
        }
        return 1;
    case KEY_PGUP:
        h->top -= w->h - 3;
        if (h->top < 0) {
            h->top = 0;
        }
        if (nl) {
            int k = h->link;
            while (k > 0 && h->links[k].row >= h->top + w->h - 2) {
                k--;
            }
            h->link = k;
        }
        return 1;
    case KEY_HOME:
        h->top = 0;
        h->link = 0;
        return 1;
    case KEY_END:
        h->top = h->nlines;
        h->link = nl ? nl - 1 : 0;
        return 1;
    case KEY_ENTER: {
        if (!nl) {
            return 1;
        }
        int t = tp_find(h->links[h->link].target);
        if (t >= 0) {
            help_goto(w, t, 1);
        }
        return 1;
    }
    case KEY_BKSP:
        help_prev();
        return 1;
    }
    return 0;
}

/* Returns true if text is selected in the help window. */
int help_has_selection(Win *w)
{
    HS *h = w->data;
    return h && h->sel_active;
}

/* Copies the selected text to the Clipboard; returns 0 when nothing is selected. */
int help_copy(Win *w)
{
    HS *h = w->data;
    if (!h || !h->sel_active) {
        return 0;
    }
    int sr, sc, er, ec;
    sel_range(h, &sr, &sc, &er, &ec);
    size_t cap = 256, n = 0;
    for (int r = sr; r <= er && r < h->nlines; r++) {
        cap += strlen(h->lines[r].text) + 1;
    }
    char *t = malloc(cap);
    for (int r = sr; r <= er && r < h->nlines; r++) {
        PLine *L = &h->lines[r];
        if (L->kind == PL_RULE) {
            continue;
        }
        int len = (int)strlen(L->text);
        int c0 = r == sr ? sc : 0, c1 = r == er ? ec : len;
        if (c1 > len) {
            c1 = len;
        }
        if (!L->example && c0 == 0 && len && L->text[0] == ' ') {
            c0 = 1; /* the one-column margin is not text */
        }
        if (c0 < c1) {
            memcpy(t + n, L->text + c0, c1 - c0);
            n += c1 - c0;
        }
        if (r < er) {
            t[n++] = '\n';
        }
    }
    t[n] = 0;
    clip_set_text(t);
    free(t);
    return 1;
}

/* Converts a screen position to a (row, column) in the topic text, clamped to the text. */
static void sel_pos(Win *w, HS *h, int x, int y, int *row, int *col)
{
    int r = y - w->y - 1 + h->top, c = x - w->x - 1;
    if (r >= h->nlines) {
        r = h->nlines - 1;
    }
    if (r < 0) {
        r = 0;
    }
    int len = h->nlines ? (int)strlen(h->lines[r].text) : 0;
    if (c >= len) {
        c = len ? len - 1 : 0;
    }
    if (c < 0) {
        c = 0;
    }
    *row = r;
    *col = c;
}

/* Mouse button pressed: starts a text selection (a click without a drag follows a link). */
void help_mouse_press(Win *w, int x, int y)
{
    HS *h = w->data;
    h->sel_active = 0;
    h->dragged = 0;
    h->px = x;
    h->py = y;
    if (h->nlines) {
        sel_pos(w, h, x, y, &h->ar, &h->ac);
    }
}

/* Mouse moved with the button held: extends the selection. */
void help_mouse_drag(Win *w, int x, int y)
{
    HS *h = w->data;
    if (!h->nlines || (x == h->px && y == h->py && !h->dragged)) {
        return;
    }
    h->dragged = 1;
    h->sel_active = 1;
    if (y <= w->y) {
        help_scroll(w, -1);
    } else if (y >= w->y + w->h - 1) {
        help_scroll(w, 1); /* scroll when dragging past the edge */
    }
    sel_pos(w, h, x, y, &h->br, &h->bc);
}

/* Mouse button released: if the pointer never moved this was an ordinary click. */
void help_mouse_release(Win *w)
{
    HS *h = w->data;
    if (!h->dragged) {
        help_click(w, h->px, h->py); /* no drag: an ordinary click */
    }
}

/* Handles a click at screen position (x, y). */
void help_click(Win *w, int x, int y)
{
    HS *h = w->data;
    int row = y - w->y - 1 + h->top, col = x - w->x - 1;
    for (int i = 0; i < h->nlinks; i++) {
        PLink *k = &h->links[i];
        if (k->row == row && col >= k->col && col < k->col + k->len) {
            h->link = i;
            help_key(w, KEY_ENTER);
            return;
        }
    }
}
