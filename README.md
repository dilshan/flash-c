# Flash C

A small C compiler and a full-screen text-mode IDE for Linux.

* **`fcc`** – command-line compiler. Reads C source and writes a native **x86-64 Linux
  executable** directly (no assembler, linker or C library involved).
* **`fc`** – the IDE: pull-down menus, overlapping edit windows, Message window with error
  tracking, option dialogs, online help – laid out like the classic DOS-era C IDEs.

![editor](docs/screenshots/editor.png)

The IDE structure (menu tables, dialog controls, status line, colour groups, compile/make/run
behaviour) was derived from a static analysis of the reference IDE binaries in
`research/TC`. The evidence and method are written up in
[`analysis/EVIDENCE.md`](analysis/EVIDENCE.md) with reproducible extraction output in
`analysis/dumps/`.

## Build and run

Requirements: a C compiler (gcc/clang), `make`, a terminal of at least 80×25 with UTF-8.
24-bit colour is used when `COLORTERM=truecolor`; otherwise 16 ANSI colours.

```
make                    # builds build/fc and build/fcc
./build/fc hello.c      # IDE (several files / a project.fcp may be given)
./build/fcc hello.c     # compiler only:  produces ./hello
./hello
make test               # compiler tests, diagnostics tests, IDE end-to-end test
```

`make install` copies both programs to `/usr/local/bin`.

## Using the IDE

| Key | Action | Key | Action |
|---|---|---|---|
| F10 / Alt+letter | menu bar | F3 / F2 | open / save |
| Alt+F9 | compile | F9 | make |
| Ctrl+F9 | run | Alt+F5 | user screen (program output) |
| Alt+F7 / Alt+F8 | previous / next error | F6 / F5 | next window / zoom |
| Alt+F3 | close window | Alt+0 | window list |
| F1 | help | Ctrl+F1 | help on word at cursor |
| Alt+X | quit | Alt+Backspace | undo |

Editor commands include the classic block commands (`Ctrl-K B/K/C/V/Y/R/W`), quick
commands (`Ctrl-Q …`), `Shift+arrows` selection, `Ctrl+Ins / Shift+Ins / Shift+Del`
clipboard, regular-expression find/replace, syntax highlighting, auto-indent and the mouse
(menus, window drag/resize/zoom/close, scroll bars, text selection).

Typical session: **F10 → File → New**, type a program, **F2** to save, **F9** to build.
Errors open the *Message* window; **Enter** on a message jumps to the source line. **Ctrl+F9**
runs the program on the normal terminal screen (your terminal's scroll-back keeps the output;
**Alt+F5** shows it again).

Menus not mentioned above: Project (multi-file programs, `.fcp` project files), Options
(compiler, linker, environment, colours, directories – saved with *Options → Save* in
`fcconfig.fc`), Window (tile, cascade, size/move, Message/Output/Watch windows).
Some terminals reserve a few function-key combinations; every command is reachable from the
menus.

## The language

Flash C implements the C89 language as used by DOS-era compilers, plus a few C99
conveniences (declarations anywhere in a block, `//` comments, designated initialisers).

* Types: `char short int long` (+`unsigned`), `float`, `double`, pointers, arrays, `struct`,
  `union`, `enum`, `typedef`, function pointers. **`int` is 32 bits, `long` and pointers are
  64 bits** (the target is x86-64, so the 16-bit DOS `int` does not apply).
* Statements/operators: everything in C89, including `switch`, `goto`, `?:`, comma, all compound
  assignments, pointer arithmetic and `sizeof`.
* Preprocessor: `#include` (user, `<…>` and built-in headers), `#define` (object- and
  function-like, `#`, `##`, `__FILE__`, `__LINE__`), `#if/#ifdef/#ifndef/#elif/#else/#endif`,
  `#undef`, `#error`, `#pragma` (ignored).
* Keywords `near`, `far`, `huge`, `cdecl`, `pascal`, `interrupt` are accepted and ignored.

**Not implemented yet:** bit-fields, passing/returning whole `struct`s by value, compound
literals, variable-length arrays, `long long` as a distinct type (it is `long`), `long double`
(it is `double`), a source-level debugger (the Debug menu offers *Evaluate* and *Watches* for
constant expressions; Trace/Step/Breakpoints are disabled and say so).

### Library

No external C library is needed. A compact runtime written in Flash C itself
(`runtime/rt.c`) is compiled into every program, only the functions that are actually used are
emitted. Headers are built into the compiler, so `#include <stdio.h>` just works; your own
include directories (*Options → Directories* or `-I`) are searched first.

`stdio.h` (`printf` family with `%d %i %u %x %X %o %c %s %p %f %e %g`, flags, width,
precision; `scanf` family; `fopen/fread/fwrite/fgets/fseek…`), `stdlib.h` (`malloc` family,
`atoi/atof/strtol/strtod`, `rand`, `qsort`, `bsearch`, `itoa`, `exit`), `string.h`, `ctype.h`,
`math.h` (`sqrt pow exp log sin cos tan atan atan2 asin acos floor ceil fmod …`), `limits.h`,
`float.h`, `stdarg.h`, `stddef.h`, `stdbool.h`, `assert.h`, `errno.h`, and DOS-flavoured
`conio.h` (`clrscr gotoxy getch getche putch cputs`), `dos.h` (`delay sleep`), `alloc.h`,
`mem.h`, `process.h`. Floating-point output is exact (correctly rounded, half-to-even) and
matched glibc's `printf` in the regression tests.

## Compiler command line

```
fcc [options] file.c [file2.c ...]     * several files are compiled as one program
  -Dname[=value]  define macro         -Idir     include directory
  -C              nested comments      -K        char is unsigned
  -w / -w-        all / no warnings    -w-use -w-pro -w-ret -w-par   individual warnings
  -jN  -gN        stop after N errors / warnings        -iN  identifier length
  -ename          output file name
```

Diagnostics use the classic format: `Error hello.c 5: Undefined symbol 'x' in function main`,
`Warning …`, `Linker Error: Undefined symbol _foo in module …`.

## Source layout

```
src/compiler/   lexer + preprocessor (cpp.c), parser/type checker (parse.c),
                x86-64 code generator + ELF writer (gen.c), driver (compile.c)
src/ide/        terminal layer (term.c), windows/menus/dialogs (wm.c menu.c dialog.c),
                editor (editor.c buf.c), option dialogs (dialogs.c), help (help.c), commands (ide.c)
src/cli/        fcc main program
runtime/        rt.c (run-time library, written in Flash C) and include/*.h (built-in headers)
tests/          run.sh (compiler vs gcc), diag.sh (message text), ide_smoke.py (pty-driven IDE test)
analysis/       EVIDENCE.md + extract_evidence.py + dumps/  (study of the reference IDE)
docs/           ARCHITECTURE.md, screenshots/
```

More detail: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
