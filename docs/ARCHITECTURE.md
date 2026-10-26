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
* **ELF writer**: one `PT_LOAD` segment (code, data, bss) at `0x400000`; absolute 32-bit
  addresses with fixups resolved after layout; entry stub calls the runtime function `_fc_start`
  which calls `main` and `exit`. System calls are made through the intrinsic
  `__syscall(n, args…)`; `__sqrt(x)` is the other intrinsic.
* **Dead-code elimination**: every function records the globals/functions it references while
  being parsed; only what is reachable from `main` and `_fc_start` is emitted.

### The run-time library
`runtime/rt.c` is ordinary Flash C compiled together with the user's program. It is parsed
first; its symbols are *hidden* from the user's program until the user (usually through a header)
declares them. This keeps the classic diagnostics (`Call to function 'foo' with no prototype`,
`Undefined symbol`) honest: forgetting `#include <stdio.h>` is still reported. If the program
defines a function with the same name as a library function, the program's version wins for the
program while the library keeps using its own.

`printf("%f")` uses an exact big-number decimal expansion of the binary value and rounds half to
even, and matched glibc in every comparison in `tests/cases`. `math.h` is implemented with
argument-reduced series (accuracy ~1e-15).

## 2. IDE (`src/ide`)

| File | Responsibility |
|---|---|
| `term.c` | raw terminal: alternate screen, off-screen cell buffer (char + CGA attribute), diff flushing, truecolor/16-colour output, key & SGR-mouse decoding (CSI/SS3, Alt = ESC prefix, bracketed paste), SIGWINCH, restore-on-crash |
| `menu.c` | menu tables (items, shortcuts, hints), bar layout, pull-down + sub-menu navigation, mouse |
| `wm.c` | window list/z-order, Message/Output/Watch/Project/Register windows, status line, desktop, mouse (drag, resize, zoom, close, scroll bars) |
| `dialog.c` | dialog engine (inputs, check boxes, radio groups, buttons, lists, frames, custom items), flowing layout builder, message boxes, file dialog |
| `dialogs.c` | every Options dialog, Find/Replace, Go to line, Evaluate, Transfer, Colors, … |
| `editor.c`, `buf.c` | line buffer with undo/redo, cursor model, selection/blocks, WordStar-style commands, syntax highlighting, search incl. a small regex engine |
| `help.c` | built-in hyper-linked help |
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

## 3. Tests

* `tests/run.sh` compiles each `tests/cases/*.c` with Flash C and with the host `gcc` and compares
  program output byte for byte (an optional `name.in` file is fed to stdin).
* `tests/diag.sh` checks the text/line numbers of the diagnostics.
* `tests/ide_smoke.py` runs the real `fc` binary on a pseudo-terminal with a small VT emulator
  (`tests/tui.py`), types a program, compiles, runs, breaks it, follows the error, opens dialogs and
  help, and quits. `tui.py` can also render PNG screenshots.

## 4. Ideas for next steps
Source-level debugger via `ptrace` (the Debug menu and the Watch/Register windows are in
place), bit-fields, struct pass/return by value, a real optimizer pass (the code generator is
deliberately simple), `long double`, locale-independent `strftime`/`time.h`, a proper `.fcp`
project dialog with per-file options.
