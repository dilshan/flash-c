# Flash C compiler reference

Everything about `fcc`, the command-line compiler, and the C that it accepts. The IDE uses the same
compiler; each switch below has a matching check box in the *Options* dialogs
(see the [user guide](USER_GUIDE.md#8-options)).
[Back to the README](../README.md) · Next: [Library reference](LIBRARY.md)

**Contents**

1. [Using fcc](#1-using-fcc)
2. [Command-line switches](#2-command-line-switches)
3. [The C language](#3-the-c-language)
4. [Diagnostics](#4-diagnostics)
5. [Optimizations](#5-optimizations)
6. [Profile-guided optimization](#6-profile-guided-optimization)
7. [The run-time library and linking](#7-the-run-time-library-and-linking)

## 1. Using fcc

```
fcc [options] file.c [file2.c ...]
```

Several files are compiled together as one program. The executable gets the name of the first file
without `.c` (change it with `-e`).

```
fcc hello.c                 # produces ./hello
fcc -O -ehello hello.c      # optimize, name the output
fcc -w -v a.c b.c           # all warnings, debug information, two files
fcc -h                      # list every switch
```

A trailing `-` turns a switch off (`-b-`). A `*` marks a default in `fcc -h`.

## 2. Command-line switches

### Input

| Switch | Meaning |
|---|---|
| `-Dname[=value]` | Define a macro. |
| `-Uname` | Undefine a macro. |
| `-Ipath` | Directory to search for include files. Searched before the built-in headers. |
| `-C` | Allow nested comments. |
| `-A` | ANSI keywords only: no `//` comments, and `near`, `far`, `huge`, `cdecl`, `pascal` and `interrupt` are ordinary identifiers. |
| `-K` | Plain `char` is unsigned. |
| `-iN` | Number of significant characters in an identifier. |

### Output

| Switch | Meaning |
|---|---|
| `-ename` | Name of the executable. |
| `-M`, `-Mp`, `-Md` | Write a link map `<output>.map`: segments, plus public symbols (`-Mp`) or the detailed segment map (`-Md`). Each symbol is attributed to its library module. |
| `-v` | Record source-level debug information for the IDE debugger. |
| `-lt` | Write an ELF symbol table. |
| `-lw` | Separate R+X code and R+W data segments (no segment that is both writable and executable). |

### Language and code generation

| Switch | Meaning |
|---|---|
| `-b` (default), `-b-` | Treat enums as `int`, or use the smallest integer type that fits. |
| `-a` (default), `-a-` | Align structure members, or pack them. |
| `-d` | Merge identical string literals. |
| `-N` | Test for stack overflow on function entry: the program stops with `Stack overflow!` and exit code 1. |
| `-ls<KB>` | Stack size in KB, set at start-up with `setrlimit` (also the limit used by `-N`). |
| `-f-`, `-f`, `-f87` | No floating-point support, or floating point (the default). |

### Optimization

| Switch | Meaning |
|---|---|
| `-O` | Turn on all optimizations (the tree optimizations and jump optimization). |
| `-O-` | Turn them all off (the default for `fcc`; the IDE starts with them on). |
| `-Os` | All optimizations, tuned for small code. |
| `-O:name,-name` | Switch single optimizations on or off. The names are in [section 5](#5-optimizations). `all` and `none` stand for everything and nothing; `jumps` and `peephole` are the two code-generator switches. |
| `-Z` | Suppress redundant loads: constant operands stay in a register. |
| `-Pg[file]` | Instrument the program so that it writes a profile when it ends (default `fcprof.dat`). |
| `-Pu[file]` | Optimize with a profile. See [section 6](#6-profile-guided-optimization). |

`-O:name` switches add to what came before, so `-O- -O:fold,cse` runs just those two, and
`-O -O:-unroll` runs everything except unrolling.

### Messages

| Switch | Meaning |
|---|---|
| `-w`, `-w-` | Display all warnings, or none. |
| `-wxxx`, `-w-xxx` | Turn one warning on or off (table below). |
| `-jN` | Stop after N errors (default 25). |
| `-gN` | Stop after N warnings (default 100). |

| Code | Warning | On by default |
|---|---|---|
| `use` | variable declared but never used | yes |
| `pro` | call to a function without a prototype | yes |
| `ret`, `rvl` | function should return a value | yes |
| `par` | non-portable pointer conversion | yes |
| `sus` | suspicious pointer conversion | yes |
| `ucp` | mixing pointers to signed and unsigned char | yes |
| `cpt` | non-portable pointer comparison | yes |
| `rch` | unreachable code | yes |
| `cln` | condition is always true or false | yes |
| `amb` | ambiguous operators need parentheses | no |
| `stv` | structure passed by value | no |

### Libraries

| Switch | Meaning |
|---|---|
| `-ln` | Do not search the standard libraries (only the start-up code is linked). |
| `-lnm` | Do not link the math library. |
| `wildargs.obj` | As a file name: link the wildcard argument expander, which expands `*` and `?` on the command line. |
| `-h` | Show the switch list. |

The switches that only make sense for 16-bit DOS compilers (memory models, Pascal calls, overlays,
segment names, instruction sets, register variables) are not offered. `tests/options.sh` checks
every switch.

## 3. The C language

Flash C implements C89 as used by DOS-era compilers, plus a few C99 conveniences: declarations
anywhere in a block, `//` comments and designated initializers.

### Types

`char`, `short`, `int`, `long` (each also `unsigned`), `float`, `double`, pointers, arrays,
`struct`, `union`, `enum`, `typedef` and function pointers.

| Type | Size |
|---|---|
| `char` | 1 byte |
| `short` | 2 bytes |
| `int` | 4 bytes (**not** the 16-bit DOS size) |
| `long`, pointers | 8 bytes |
| `float`, `double` | 4 and 8 bytes |

Whole `struct`s can be passed to and returned from functions by value.

### Statements, operators and the preprocessor

* Everything in C89: `switch`, `goto`, `?:`, the comma operator, all compound assignments, pointer
  arithmetic and `sizeof`.
* `#include` (user headers, `<...>` headers and the built-in headers; a macro that expands to a
  header name works), `#define` (object-like and function-like, `#`, `##`), `#if`, `#ifdef`,
  `#ifndef`, `#elif`, `#else`, `#endif`, `#undef`, `#error`, and `#pragma startup` and
  `#pragma exit`. Other pragmas are ignored.
* Predefined: `__FILE__`, `__LINE__`, `__DATE__`, `__TIME__` and `__STDC__`.
* Trigraphs (`??(`) are replaced before lexing.
* Wide characters and strings (`L'a'`, `L"text"`); `wchar_t` is a 32-bit `int`.
* `near`, `far`, `huge`, `cdecl`, `pascal` and `interrupt` are accepted and ignored.
* `#pragma startup fn [priority]` and `#pragma exit fn [priority]` run `fn` before `main` or after
  it returns. Priority is 0 to 255 (default 100); start-up functions run in ascending order, exit
  functions in descending order.

### Bit-fields

Bit-fields follow C89: members declared `int`, `signed int` or `unsigned int` with a constant width
of 1 to 32 bits (`unsigned flag : 1;`), unnamed fields (`int : 3;`) and `: 0` to start a new unit.
Fields are packed from the least significant bit into 32-bit units and never straddle a unit; a
plain `int` field is signed. Fields can be read, assigned, used with `++`, `--` and `op=`,
initialized in brace initializers, copied with their struct and shown by the debugger. Taking their
address or applying `sizeof` is an error, as are `char`, `short` and `long` fields.

### Type checking

* `const` is checked for scalars, pointers and arrays (`Cannot modify a const object`). It is not
  tracked for whole `struct` or `union` objects (their `const` members are).
* Calls through a prototype reject pointer and integer mismatches (`Type mismatch in parameter N in
  call to 'f'`).
* These are errors: objects of an incomplete struct type, a repeated `typedef`, `register` at file
  scope, and floating point in `#if`.

### Not implemented

Compound literals, variable-length arrays, `long long` as a distinct type (it is `long`) and `long
double` (it is `double`).

## 4. Diagnostics

Messages use the classic format:

```
Error hello.c 5: Undefined symbol 'x' in function main
Warning hello.c 8: 'count' is declared but never used
Linker Error: Undefined symbol _foo in module ...
```

The parser recovers at statement boundaries, so one run reports many errors. `tests/diag.sh` checks
the text and line numbers of the messages.

## 5. Optimizations

Flash C has 48 optimizations that can be switched one by one. They work on the syntax tree of each
function before its code is generated, then on a graph of basic blocks, then on the whole program.
The names in brackets are the `-O:name` switches; in the IDE each one is a check box in *Options →
Compiler → Optimizations* and has a help page (*Help → Contents → Optimization options*).

All of them are off for `fcc` unless you give `-O`, `-Os` or `-O:name`, and on in the IDE (except
*size*). With debug information (`-v` or *Source debugging*) the passes that would change
statements, variable values or the call stack are skipped, so stepping and watches match the
source. Variables whose address is taken, and functions that use `setjmp`, are left alone by the
data-flow passes.

### Expressions

| Name | Optimization |
|---|---|
| `fold` | Constant folding: `(char)300 + 2 * 5` becomes `54`. |
| `algebra` | Algebraic simplification: `x+0`, `x*1`, `x-x`, `x|0`. |
| `compare` | Evaluate comparisons with a known result. |
| `constprop` | Local constant propagation. |
| `copyprop` | Local copy propagation. |
| `deadstore` | Remove assignments whose value is never read. |
| `unreachable` | Remove statements after `return`, `break` or `continue`, and the bodies of `if (0)` and `while (0)`. |
| `casts` | Remove conversions that do not change the value. |
| `canon` | Put operands of commutative operators in a fixed order, constants last. |
| `cse` | Local common subexpression elimination. |
| `strength` | Strength reduction: multiplication by a power of two becomes a shift; unsigned division and modulo become a shift or a mask. |
| `bool` | Simplify `!!x`, `x == 0`, `c ? 1 : 0` and constant `&&` and `||`. |

### Control flow

| Name | Optimization |
|---|---|
| `cfg` | Build the graph of basic blocks. The passes below and in the next sections need it. |
| `gconst` | Global constant propagation across `if`, loops and labels. |
| `ccp` | Conditional constant propagation: a branch with a known condition is followed one way only. |
| `cfs` | Remove jumps to the next statement and empty statements. |
| `ublock` | Remove basic blocks that no path reaches, labels included. |
| `gdce` | Global dead-code elimination using live variables. |
| `gcopy` | Global copy propagation. |
| `merge` | Merge a block into its predecessor when its label is no longer a jump target. |
| `thread` | Jump threading: send a `goto` straight to its final target. |
| `live` | Live-variable analysis (needed by `gdce`). |
| `branch` | Remove a conditional jump to the next statement. |
| `tailrec` | Tail-recursion elimination: `return f(...)` inside `f` becomes a jump to the start. |

### Redundancy, calls and loops

| Name | Optimization |
|---|---|
| `rd` | Reaching-definitions analysis. |
| `avail` | Available-expressions analysis. |
| `gcse` | Global common subexpression elimination. |
| `pre` | Partial redundancy elimination (restricted; it never moves a load). |
| `inline` | Replace calls of small functions that only return an expression. |
| `tailcall` | Tail-call optimization: `return g(...)` jumps to `g` and does not grow the stack. |
| `loops` | Loop detection. |
| `licm` | Loop-invariant code motion. |
| `iv` | Induction-variable optimization: `i * 8` and `p + i` in a loop become a value advanced with the counter. |

### Machine code

| Name | Optimization |
|---|---|
| `regalloc` | Register allocation: the most used locals live in `rbx` and `r12`-`r15`. |
| `isel` | Instruction selection: immediate operands, direct operand loads, fused compare-and-branch code. |
| `layout` | Branch folding and block layout: invert a jump over a jump, test loop conditions at the bottom. |

### SSA and whole-program analysis

| Name | Optimization |
|---|---|
| `domtree` | Dominator tree and dominance frontiers. |
| `ssa` | Static single assignment form. |
| `sccp` | Sparse conditional constant propagation on the SSA form. |
| `adce` | Aggressive dead-code elimination: removes assignments that only feed each other, such as an unused sum in a loop. |
| `alias` | Alias analysis: loads of globals, array elements and members take part in global CSE. |
| `ipcp` | Interprocedural constant propagation: a parameter that every call passes as the same constant becomes that constant. |
| `ipdce` | Interprocedural dead-code elimination: unused arguments and results, calls of pure functions whose result is ignored, and functions nothing calls. |
| `recur` | Recursive function analysis: `n * f(n-1)` and `f(n-1) + n` become loops. |
| `sroa` | Scalarization: a local structure or constant-indexed array becomes separate variables. |
| `unroll` | Loop unrolling: loops of up to 8 constant iterations are replaced by copies; other small loops run two iterations per test. |
| `unswitch` | Loop unswitching: an invariant `if` is moved out of the loop and the loop is copied. |
| `size` | Code-size optimization (`-Os`): no unrolling, no unswitching, no PRE, no loop rotation and little inlining. Not part of `all`. |

### The two code-generator switches

| Switch | Meaning |
|---|---|
| `-O` / `jumps` | Remove jumps to the next instruction and use 2-byte jumps where the target is near. |
| `-Z` / `peephole` | Keep constant operands in a register instead of saving and restoring the left operand. |

### Limits

The optimizer is original work. It is tested by comparing programs compiled with and without it
(`tests/opt.sh` and the random-program test `tests/optfuzz.py`), not by comparison with the reference
compiler, whose optimizer could not be disassembled. The limits worth knowing:

* Sparse conditional constant propagation iterates in rounds rather than with a work-list.
* Aggressive dead-code elimination removes assignments only, never branches.
* Alias analysis is based on objects and constant offsets; loads take part in global CSE only.
* Loops are unrolled completely only up to 8 iterations, and partially only by 2.
* A function counts as pure if it stores only to locals, calls only pure functions and has no
  `goto` and no endless loop. Removing a call to a pure function that would have trapped (for
  example by dividing by zero) removes the trap.
* Identical-function folding is not done.

## 6. Profile-guided optimization

Two steps: run an instrumented program to record how it behaves, then compile again using the record.

```
fcc -Pg prog.c            # step 1: build with counters
./prog                    # run it with typical input; this writes fcprof.dat
fcc -O -Pu prog.c         # step 2: optimize using the profile
```

The profile counts how often each function is entered, each `if` is reached and takes its then
part, and each loop is entered and runs its body. `-Pgname` and `-Puname` use another file. With the
profile:

* the likely arm of an `if` with an `else` becomes the fall-through arm;
* the functions that ran most are generated first;
* functions that never ran are optimized for size;
* hot functions may be inlined at a larger size;
* loops that average fewer than three iterations are not unrolled.

The output of the program does not change. If the source changes, record the profile again;
entries that are not found are ignored. The IDE has no check box for this: run `fcc` from a Transfer
entry or a terminal.

## 7. The run-time library and linking

Flash C ships a standard run-time library modelled on the structure of a classic DOS C library. It
is written in Flash C itself (`runtime/lib/`), embedded into the compiler, and **linked per
function**: only what your program (transitively) uses ends up in the executable. The
[library reference](LIBRARY.md) lists every header and function.

| Part | Source | Contents |
|---|---|---|
| Start-up | `runtime/lib/c0.c` | entry point, `argc`/`argv`/`environ`, `atexit`, `exit`, `_exit`, `_cexit`, `abort`, the `#pragma startup/exit` runner |
| Standard library | `runtime/lib/c/*.c` | stdio, string and memory functions, ctype, stdlib (heap, `qsort`, `rand`, conversions), time, signal, setjmp, io (descriptors, `stat`), dir (`findfirst`, paths), process (`system`, `spawn*`, `exec*`), conio (ANSI console), dos and bios helpers, locale, assert |
| Math library | `runtime/lib/m/*.c` | `math.h` (with the classic `matherr` messages), `%e %f %g` and `strtod`, `atof`, `ecvt`, `fcvt`, `gcvt`, the `_control87` family |
| Wildcard arguments | `runtime/lib/wildargs.c` | optional: expands `*` and `?` in the command line |
| Profile writer | `runtime/lib/c/prof.c` | linked by `-Pg`; writes the profile when the program exits |

Public headers: `alloc assert bios conio ctype dir direct dirent dos errno fcntl float io limits
locale malloc math mem memory process search setjmp share signal stdarg stddef stdbool stdio
stdlib string time utime values varargs locking`, and `sys/stat sys/timeb sys/types`.

### How linking works

* **Floating point is optional.** A program that never uses `float` or `double` does not link the
  conversion code. If `printf("%f")` runs without it, the program stops with `printf : floating
  point formats not linked`, as the original library does. Using `float` or `double` pulls the math
  library in automatically.
* **Selecting libraries.** `-ln` links no libraries, `-lnm` omits the math library, `-f-` selects
  "no floating point", and `wildargs.obj` as an extra file enables wildcard arguments. In the IDE
  these are *Options → Linker*, *Options → Compiler → Code generation → Floating point*.
* **Your own headers.** Include directories (*Options → Directories* or `-I`) are searched before
  the built-in headers.
* **Link map.** `-M` writes `<output>.map` with the segments; `-Mp` adds public symbols and `-Md`
  the detailed segment map.

### What the library is underneath

An ordinary C library for Linux x86-64: files are real file descriptors, `conio` writes ANSI
sequences, `time` honours `TZ`, `signal` uses kernel signals, and `system`, `spawn` and `exec` use
`fork` and `execve`. DOS-only facilities (BIOS, ports, interrupts, far and overlay memory,
`gettext`, `puttext`, `movetext`, graphics) are intentionally absent.

`printf` and `strtod` are exact (correctly rounded, half to even) and match glibc in the regression
tests. `rand()` uses the classic generator, so `srand(1)` yields 346, 130, 10982, ...
