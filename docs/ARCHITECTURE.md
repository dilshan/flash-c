# Flash C – architecture notes

## 1. Compiler (`src/compiler`)

```
source ──► lexer/preprocessor ──► parser + type checker ──► x86-64 code generator ──► ELF file
 (cpp.c)            (cpp.c)             (parse.c)                    (gen.c)
```

* **One address space, one arena.** All compiler data lives in a bump arena (`util.c`) that is
  released wholesale at the start of the next compile, so the IDE can call `fc_compile()` over
  and over without leaking. Identifiers are interned (pointer comparison).
* **Preprocessor** (`cpp.c`): the lexer produces a token vector, directives are executed on the
  vector, macros are expanded recursively with a "busy" flag instead of hide-sets. Built-in
  headers are looked up by name after the user's include directories.
* **Parser** (`parse.c`) is a recursive-descent parser that builds a *typed* AST in one pass:
  every `Node` has its C type, implicit conversions are explicit `N_CAST` nodes, pointer
  arithmetic is already scaled, and integer/floating constants are folded. Semantic errors are
  reported with the classic wording and the parser recovers at statement boundaries
  (`setjmp/longjmp`), so one run reports many errors.
* **Types**: `char`=1, `short`=2, `int`=4, `long`=8, pointer=8, `float`=4, `double`=8.
  Integer values are kept *normalised* in 64-bit registers (sign/zero-extended according to their
  type) so comparisons and shifts only need the 64-bit forms.
* **Code generator** (`gen.c`) is a straightforward accumulator machine: every expression leaves
  its value in `rax`, binary operators push the left operand. Floating values also travel in
  `rax` as their `double` bit pattern and are moved to `xmm0/xmm1` only for the arithmetic
  instruction (SSE2); `float` is stored in 4 bytes and widened on load. Calling convention is
  private: arguments pushed right-to-left (8 bytes each), caller pops, result in `rax`; this makes
  `va_arg` trivial (`stdarg.h` is three macros) and means no ABI compatibility with the host libc
  is needed or attempted.
* **ELF writer**: one `PT_LOAD` segment (code, data, bss) at `0x400000` (or, with `-lw`, an R+X segment for the
  header and code and a page-aligned R+W segment for data and bss; with `-lt` also `.symtab`/`.strtab` and section
  headers); absolute 32-bit
  addresses with fixups resolved after layout; entry stub calls the runtime function `_fc_start`
  which calls `main` and `exit`. System calls are made through the intrinsic
  `__syscall(n, args…)`; `__sqrt(x)` is the other intrinsic.
* **Optimizer (`-O`, `-Z`)**: `-Z` keeps a constant right operand in `rcx` instead of `push rax` / `pop rax` and
  removes `push rax; pop` pairs that are not separated by a label or line mark; `-O` drops a `jmp` to the next
  instruction and then relaxes the jumps of each function (rel32 to rel8 until a fixed point), remapping labels,
  fix-ups and the debugger line table. `-N` emits `cmp rsp, [__fc_stklimit]; jae; call __fc_stkover` in user functions;
  the limit is set by the start-up code from `RLIMIT_STACK` (`-ls` first applies the chosen size with `setrlimit`).
* **Dead-code elimination**: every function records the globals/functions it references while
  being parsed; only what is reachable from `main` and `_fc_start` is emitted.

### The run-time library
The library is a set of small Flash C modules (`runtime/lib/`), embedded in the compiler by
`tools/embed.c` as a table of `{kind, name, text}`: kind `0` start-up (`c0.c`, always), `1` standard
library (`c/*.c`), `2` math library (`m/*.c`), `3` optional objects (`wildargs.c`). On every compile
the enabled modules are preprocessed and parsed together with the user's program (about 5 ms),
under pseudo file names `c0/c0.c`, `libc/<name>.c`, `libm/<name>.c`; each symbol remembers its file, so
the link map and the "module" in linker errors name the library module.

* Library symbols are *hidden* from the user's program until a header declares them, which keeps the
  classic diagnostics honest (`Call to function 'foo' with no prototype`, `Undefined symbol`).
  A user function with the same name as a library function wins for the program.
* **Linking = reachability.** Only functions reachable from `main`, the start-up code, and the
  `#pragma startup/exit` functions of linked modules are emitted. A library module's pragma
  function is only kept if the module has another reachable symbol, so unused modules cost nothing.
* **Start-up** (`c0.c`): the ELF entry stub calls `_fc_start(argc, argv)`, which sets `_argc/_argv/environ`,
  runs the generated `__fc_init()` (the `#pragma startup` list, ascending priority), then
  `exit(main(...))`. `exit` runs `atexit` handlers and the generated `__fc_exit()` (the
  `#pragma exit` list, descending priority, which flushes `stdio`), then `_exit`.
* **Floating point is detected, not declared.** The parser sets `uses_fp` when user code declares
  or computes with `float/double`; this links `fpcvt.c`, whose `#pragma startup` installs
  `__RealCvtVector` and `__ScanTodVector`. `printf/scanf` call the converters only through those
  vectors, so integer-only programs carry no floating-point code (same design as the original).
* **Intrinsics** known to the code generator by name: `__syscall`, `__sqrt`, `__setjmp`,
  `__longjmp` (inline code: save/restore `rbp`, `rsp` and the resume address), plus two stubs written by the
  linker for kernel signal delivery (`__sigtramp`, `__sigrestorer`).
* **Structures by value:** an argument is copied into its stack slot; a result is built in a hidden
  frame slot of the caller whose address is passed as an extra first argument.
* **Link map** (`-M/-Mp/-Md`): `write_map` lists `_TEXT/_DATA/_BSS`, public symbols and the
  detailed segment map using the same symbol records.
* Library-internal helpers are `static` functions with globally unique `__prefix` names (statics
  share one scope across modules). Libraries may not use compound literals.

`printf("%f")` uses an exact big-number decimal expansion of the binary value and rounds half to
even, and matched glibc in every comparison in `tests/cases`. `math.h` is implemented with
argument-reduced series (accuracy ~1e-15). The function list is in [`LIBRARY.md`](LIBRARY.md).

## 2. IDE (`src/ide`)

| File | Responsibility |
|---|---|
| `term.c` | raw terminal: alternate screen, off-screen cell buffer (char + CGA attribute), diff flushing, truecolor/16-colour output, key & SGR-mouse decoding (CSI/SS3, Alt = ESC prefix, bracketed paste), SIGWINCH, restore-on-crash |
| `menu.c` | menu tables (items, shortcuts, hints), bar layout, pull-down + sub-menu navigation, mouse |
| `wm.c` | window list/z-order, Message/Output/Watch/Project/Register windows, status line, desktop, mouse (drag, resize, zoom, close, scroll bars) |
| `dialog.c` | dialog engine (inputs, check boxes, radio groups, buttons, lists, frames, custom items), flowing layout builder, message boxes, file dialog |
| `dialogs.c` | every Options dialog, Find/Replace, Go to line, Evaluate, Transfer, Colors, … |
| `editor.c`, `buf.c` | line buffer with undo/redo, cursor model, selection/blocks, WordStar-style commands, syntax highlighting, search incl. a small regex engine |
| `help.c` | hyper-linked help: built-in basic topics plus extended topics read from external `*.fch` files (`help/`) |
| `ide.c` | commands, build/make/run, Message tracking, projects, desktop file, main loop |
| `config.c` | colour table (named groups/items) and `fcconfig.fc` persistence |

Design points:
* **Immediate-mode drawing.** `ide_draw()` repaints the whole desktop into the cell buffer on
  every event; modal loops (menus, dialogs) call it first and paint on top. The diffing flush
  keeps terminal traffic small.
* **User screen.** The IDE runs on the terminal's alternate screen. *Run* leaves it, restores
  the terminal modes, runs the program on the normal screen and comes back; *Alt+F5* peeks at it.
* **Transactional dialogs.** Settings dialogs edit `cfg` in place; *Cancel* restores a copy.
* **Compiler integration.** The IDE links the same compiler library as `fcc`. Diagnostics arrive
  through a callback and are appended to the Message list; the first error is tracked in the
  editor automatically.

### Code style

The C sources (compiler, IDE, run-time library and tools) are formatted with `clang-format` using
the rules in `.clang-format` (4-space indent, 100 columns, braces on every block, a blank line
between definitions); run `make format` after editing. Every function has a comment that says what
it does, what its parameters mean and what it returns. The main structures (`Tok`, `Type`, `Sym`,
`Node`, `Win`, `Buf`, ...) document their fields.

## 3. Tests

* `tests/run.sh` compiles each `tests/cases/*.c` with Flash C and with the host `gcc` and compares
  program output byte for byte (an optional `name.in` file is fed to stdin).
* `tests/diag.sh` checks the text/line numbers of the diagnostics.
* `tests/libtest.sh` checks the library build options: link maps, `-ln/-lnm/-f-`, `#pragma startup/exit` order,
  wildcard arguments, exit codes, `abort`, `atexit`, and that every public header compiles on its own.
* `tests/ide_smoke.py` runs the real `fc` binary on a pseudo-terminal with a small VT emulator
  (`tests/tui.py`), types a program, compiles, runs, breaks it, follows the error, opens dialogs and
  help, and quits. `tui.py` can also render PNG screenshots.

* `tests/debug_smoke.py` drives the debugger in the real IDE: breakpoint, Run, Trace into, Call
  stack, Evaluate, Step over, Program reset, Go to cursor, program exit.
* `tools/dbgtest.c` is a headless driver for `dbgcore` (see below), used while developing.

### Debugger

* Compiler side (`-v` / `FcOptions.debug`): `gen.c` records a line table (`note_line`, one entry per
  statement start, in code order), user functions with their variables (frame offset, scope lines) and
  user globals, and exposes them as `fc_debug_info()` (`fc.h`, `FcDebug`). The data lives until the
  next compile.
* `src/ide/dbgcore.c`: runs the executable (a static ELF at fixed addresses) under `ptrace` with its
  stdio on a pseudo terminal. Breakpoints are `int3` bytes inserted only while the program runs.
  Trace/step single-step until the line changes; a `call` is detected from the opcode and stepped
  over with a temporary breakpoint at the return address (checking `rsp`, so recursion works).
  Frames are found by following the `rbp` chain; locals are `[rbp+off]`. The IDE switches to the
  user screen (`term_suspend`) only while the program runs freely; during that time keyboard input
  is forwarded to the pty.
* `src/ide/dbgeval.c`: the expression evaluator (C operators on typed values read from
  `/proc/<pid>/mem`), value formatter, inspector lines, frame descriptions and registers.
* `src/ide/debugger.c`: the UI (commands, Breakpoints / Call stack / Inspect dialogs, execution
  marker, desktop persistence). `ide_build` calls `dbgui_reset` first because a compile frees the
  debug tables.

## 4. Ideas for next steps
A real optimizer pass (the code generator is
deliberately simple), `long double`, locale-independent `strftime`/`time.h`, a proper `.fcp`
project dialog with per-file options.

## Help files

The basic documentation is compiled into `src/ide/help.c`. The extended help (the whole standard
library reference, formats, keywords, preprocessor) lives in `help/*.fch`, plain text files that
are read when help is first used. They are searched, in order, in `$FC_HELP`, `<exe>/help`,
`<exe>/../share/flashc/help`, `<exe>/../help`, `/usr/local/share/flashc/help`,
`/usr/share/flashc/help` and `./help`; the first directory with `.fch` files wins. If none is found,
only the basic topics are available and Contents says so.

File format: `@topic Title` starts a topic (same title replaces a built-in one), `@append Title`
adds text to one, `@keys` lists words for Ctrl+F1, `@alias` adds names to the Index, `@example` ...
`@end` marks program text (shown in the example colour, copied by Edit|Copy example), `{Title}` or
`{label|Title}` is a hyperlink, `;;` starts a comment, a leading `@@` is a literal `@`.
`help/library.fch` is generated by `tools/helpgen/gen.py` (`make help`).
