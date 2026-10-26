# How the reference IDE (`TC.EXE`) works — evidence

This report documents what was learned from the reference installation in
`research/TC` and how each finding was used to design Flash C. Everything below can
be reproduced with:

```
python3 -I analysis/extract_evidence.py /path/to/TC/BIN      # writes text extracts (not distributed)
```

The raw extracts are not distributed with this repository; they can be regenerated with the script
above. Line references like `menus_items.txt:1-40` point into those extracts.

**Method.** Static analysis only: parse the executable headers, enumerate segments,
extract the text tables, read the settings file. Nothing was executed and no code
was copied; Flash C re-implements behaviour from observation.

---

## 1. What the files are

| File | Finding | Evidence |
|---|---|---|
| `BIN/TC.EXE` (1,263,536 B) | The IDE. An **MZ stub + 16-bit NE executable** (`e_lfanew = 0x10a0`, signature `NE`), linked for a DOS extender, so it runs in **protected mode** (NE flag `0x0a`, target-OS byte 5). | `tc_ne_header.txt:1-9` |
| NE layout | **183 segments**: 132 code (811,913 B) and 51 data (227,810 B). Many small code segments = the program is organised as dozens of independently loadable modules (overlay-style swapping; strings `Swapping...`, `:\TC000x.SWP`, `Memory manager could not %s`, EMS/extended-memory counters confirm a software swap manager). | `tc_ne_header.txt`, `pipeline_strings.txt` |
| `BIN/TCC.EXE` (541 KB) | **The command-line compiler**, a separate NE program. It holds the full diagnostic message table (about 390 message strings, ~150 of them with `%s` placeholders — counted from `tcc_strings_all.txt`) and the switch table. | `tcc_ne_header.txt`, `tcc_usage_and_options.txt` |
| `BIN/TLINK.EXE`, `MAKE.EXE`, `TLIB.EXE`, `TDUMP.EXE`, `GREP.COM` … | Companion tools. The IDE launches them (strings `tlink.exe`, `TASM`, `$TASM`, `MSG(TASM2MSG)`, `$CAP`, `$NOSWAP`) and parses their output through small "message translator" programs (`GREP2MSG.EXE`, `TASM2MSG.EXE`). | `pipeline_strings.txt`, `BIN/*.CFG` |
| `BIN/TCCONFIG.TC` (11,856 B) | Binary settings file (header text + tagged records). Contains the colour attribute table. | `config_palette.txt` |
| `BIN/TCDEF.DSK`, `TCDEF.DPR` | Desktop (open files, history) and default project files; extensions `.DSK`, `.DPR`, `.PRJ`, `.SYM` also appear in `TC.EXE`. | `pipeline_strings.txt` |
| `LIB/C0?.OBJ`, `C?.LIB`, `MATH?.LIB`, `EMU.LIB`, `FP87.LIB`; `INCLUDE/*.H` | One start-up object and one runtime library **per memory model** (t, s, m, c, l, h). | `tcc_strings_all.txt` (`c0t.obj … c0h.obj`, `cs.lib`, `cm.lib`, `maths.lib` …) |

## 2. The build pipeline the IDE implements

1. **Front end → assembly or object.** `TCC.EXE` accepts `-S` ("Produce assembly output") and `-B` ("Compile via assembly") in addition to direct object output. `TC.EXE` itself contains assembler-listing text (`?debug L`, `?debug S`, macros) — it embeds a code generator.
2. **Link.** `tlink.exe` is spawned with a start-up object `c0<model>.obj` and a library `c<model>.lib` + `math<model>.lib` (`tcc_strings_all.txt`).
3. **Status UI.** While building, the IDE shows a window with the labels
   `Main file:`, `Compiling:`, `Total File`, `Lines compiled:`, `Warnings:`, `Errors:`, `Available memory:` and finishes with `Success : Press any key` or `Errors : Press any key`
   (`pipeline_strings.txt`, `statusline_and_progress.txt`). Make additionally prints `Checking dependencies . . .` and `is up to date`.
4. **Messages.** Diagnostics use the pattern ` %f %n: ` (file, line) preceded by one of `Fatal`, `Error`, `Warning`; linker output is prefixed `Linker Error:` / `Linker Warning:`. TCC prints text such as `Undefined symbol 'x' in function main`, `Statement missing ;`, `Call to function 'f' with no prototype`, `Function should return a value` (`tcc_diagnostic_samples.txt`). They go to a **Message window**; the status line then offers `Space View source`, `Enter Edit source`, `Alt-F7 Prev Msg`, `Alt-F8 Next Msg`.
5. **Run.** The program runs on a separate "user screen" (`Window|User screen Alt+F5`, `Switch to the full-screen user output`) and `Program exit code:` is reported.

The command-line switch set (27 lines in `tcc_usage_and_options.txt`) mirrors the dialogs: `-I`/`-L` ↔ Directories, `-D` ↔ Defines, `-w…`/`-g`/`-j` ↔ Messages, `-C` ↔ Source|Nested comments, `-K` ↔ Unsigned characters, `-O/-G/-Z` ↔ Optimizations, `-m?` ↔ Model, `-1/-2/-f` ↔ Instruction set / Floating point.

## 3. The IDE's user interface

### 3.1 Menu bar and pulldowns
The pulldown strings are stored in one contiguous table (`menus_items.txt`, in on-screen order, `~x~`-style hot letters stripped in the table, shortcuts right-aligned with spaces):

* **≡**: About…, Clear desktop, Repaint desktop, then user "Transfer" programs.
* **File**: New · Open… F3 · Save F2 · Save as… · Save all · — · Change dir… · Print · DOS shell · — · Quit Alt+X
* **Edit**: Undo Alt+BkSp · Redo Shift+Alt+BkSp · — · Cut Shift+Del · Copy Ctrl+Ins · Paste Shift+Ins · Clear Ctrl+Del · — · Copy example · Show clipboard
* **Search**: Find… · Replace… · Search again Ctrl+L · Go to line number… · Previous error Alt+F7 · Next error Alt+F8 · Locate function…
* **Run**: Run Ctrl+F9 · Program reset Ctrl+F2 · Go to cursor F4 · Trace into F7 · Step over F8 · Arguments…
* **Compile**: Compile Alt+F9 · Make F9 · Link · Build all · — · Information… · Remove messages
* **Debug**: Inspect… Alt+F4 · Evaluate/modify… Ctrl+F4 · Call stack… Ctrl+F3 · — · Watches ▶ (Add watch Ctrl+F7 · Delete · Edit · Remove all) · Toggle breakpoint Ctrl+F8 · Breakpoints…
* **Project**: Open project… · Close project · — · Add item… · Delete item · Local options… · Include files…
* **Options**: Application… · Compiler ▶ (Code generation, Advanced code generation, Entry/Exit Code, Optimizations, Source, Messages ▶ [Display, Portability, ANSI violations, Frequent errors, Less frequent errors], Names) · Transfer… · Make… · Linker ▶ (Settings, Libraries) · Librarian… · Debugger… · Directories… · Environment ▶ (Preferences, Editor, Mouse, Desktop, Startup, Colors) · Save…
* **Window**: Size/Move Ctrl+F5 · Zoom F5 · Tile · Cascade · Next F6 · Close Alt+F3 · Close all · — · Message · Output · Watch · User screen Alt+F5 · Register · Project · Project notes · — · List all… Alt+0
* **Help**: Contents · Index Shift+F1 · Topic search Ctrl+F1 · Previous topic Alt+F1 · Help on help

The bar itself is stored as ` File `, ` Edit `, … ` Help ` (space-padded titles). Flash C's `src/ide/menu.c` reproduces this table, except that Options items and settings that have no effect for this compiler (application, advanced code generation, entry/exit code, optimizations, names, librarian, startup, C++ options, memory models, overlays and similar) were removed (Alt+F4 is kept as shown; it is a terminal/WM shortcut on many systems, every command is also reachable through the menus).

### 3.2 Status line and per-item help
* Default key bar: `F1 Help  F2 Save  F3 Open  Alt-F9 Compile  F9 Make  F10 Menu` (strings ` Help `, ` Save `, ` Open `, `Alt-F9`, ` Compile `, ` Make `, ` Menu`).
* Alternate bars: message window (`Space View source`, `Edit source`, `Prev Msg`, `Next Msg`), help (`Help on help`, `Previous topic`, `Help index`, `Close help`), project (`Add`, `Delete`, `Edit`, `Includes`).
* ≈225 one-line **hint strings** shown on the status line while a menu entry or dialog control is highlighted (`hint_texts.txt`), e.g. *"Make and run the current program"*, *"Use small memory model (64K for code, 64K for static data)"*. Flash C shows its own wording for each menu item and dialog control.

### 3.3 Dialog boxes
`dialog_controls.txt` and `dialog_search_etc.txt` list the control captions (hot letters marked with `~`). They define the dialogs Flash C implements: Open a File / Save File As (Name, Files list, Open/Replace/Cancel/Help, info panel with name, size, date, time), Find Text / Replace Text (Text to Find, Options: Case-sensitive, Whole words only, Regular expression, Prompt on replace; Direction; Scope; Origin; Change All), Go to Line Number, Locate Function, Program Arguments, Evaluate and Modify, Window List, Transfer, Local Options, Include Files, and all Options pages (Code Generation with Model / Floating Point / Instruction Set groups, Advanced Code Generation, Entry/Exit Code, C++ Options, Optimization, Source, five warning pages, Names, Make, Linker, Libraries, Librarian, Debugger, Directories, Application, Preferences, Editor, Mouse, Desktop, Startup, Colors).

### 3.4 Look and feel
* Colours are **CGA attributes** (`bg<<4 | fg`): `config_palette.txt` shows a table of such bytes (`0x7f 0x7e 0x70 0x7e 0x70 …`, `0x1f 0x1e 0x17 0x1a 0x1b …`, `0x30 0x3e 0x2f 0x38 0x3f …`) — grey-on-blue editor text, black-on-grey menus, black-on-cyan message window, black-on-green selection bars.
* The *Colors* dialog enumerates the groups and items (`color_groups_items.txt`): Main Menu, Pulldown Menus, Popup Menus, Edit Window, Output/Message/Watch Window, Modal Help, Status Line, Project/Inspector Window, Dialog Window/Controls/Messages/Buttons/Text Entry/Lists/Groups, Desktop; items such as Normal Text, First Letter, Selection Bar, Selected Letter, Disabled Entry, Border, Title, Scroll Bar, Arrows, Thumb Tab, Error Line, Break Point Line, and syntax classes (Comment, Reserved, Identifier, Symbol, String, Integer, Float, Octal, Character, Preprocessor, Illegal Char). Flash C's palette table (`src/ide/config.c`) uses the same names and groups.
* Window decoration strings: `Drag the title bar to move window`, `Drag the window corner to resize window`; screen size options `25 lines` / `43/50 lines`; mouse options (right button actions, double-click speed, reverse buttons).

### 3.5 Editor behaviour
Editor Options page: Create backup files, Insert mode, Autoindent, Use tab character, Optimal fill, Syntax highlighting, Backspace unindents, Cursor through tabs, Group undo, Persistent blocks, Overwrite blocks, Tab size, Default extension. WordStar-style block/quick commands and the classic `Ctrl+Ins / Shift+Ins / Shift+Del` clipboard keys appear in the menu shortcuts. New buffers are named `NONAME00.<ext>`.

## 4. How the findings were applied in Flash C

| Observation | Flash C |
|---|---|
| IDE + separate command-line compiler sharing the same options | `build/fc` (IDE) and `build/fcc` (CLI) link the *same* compiler library |
| Per-model start-up object + runtime library + external linker | Not needed on Linux x86-64: the code generator writes a complete ELF executable; a run-time library written in Flash C (`runtime/lib/`: start-up, standard library, math library) is parsed with the program and only the reachable functions are emitted, see section 6 |
| `-w`, `-g`, `-j`, `-I`, `-D`, `-C`, `-K`, `-i` switches | same letters in `fcc`; IDE dialogs map to the same `FcOptions` |
| `Error file 5: …` / `Warning …` / `Linker Error:` | same prefixes and wording for the diagnostics Flash C can produce |
| Message window with source tracking | `Message` window, Enter/Space/Alt-F7/Alt-F8 behave as in the status-line strings |
| Compile status window, press-any-key | `show_build_status()` in `src/ide/ide.c` |
| User screen | The IDE lives on the terminal's alternate screen; programs run on the normal screen, `Alt+F5` shows it |
| Menu/dialog tables | `menu.c`, `dialogs.c`, `dialog.c` |
| CGA attribute colours | 16-colour VGA palette rendered as 24-bit colour (or 16 ANSI colours when unavailable) |
| Config / desktop / project files | `fcconfig.fc` (text), `fcdef.dsk` (text), `*.fcp` project files (text) — **not** binary-compatible with the originals by design |

## 5. Limits of this analysis (what is inferred, not proven)

* The *code generator internals* of the reference compiler (register allocation, OMF record emission) were **not** reverse-engineered: that was unnecessary because Flash C targets a different CPU mode and object format. Statements about it above are limited to the observable interface (switches, messages, file names).
* Colour defaults in Flash C are chosen to match the observed attribute table and the classic appearance; the exact per-item mapping inside `TCCONFIG.TC` records was not decoded.
* Menu item **order** inside each pulldown follows the order of the string table plus the documented layout of this product family; the strings themselves, shortcut keys and hint texts are direct evidence.
* The debugger (Inspect, Call stack, breakpoints, trace) exists in the reference IDE (strings `Inspecting %s`, `Too many inspectors`, `Cannot inspect: type not available`, breakpoint dialogs); see §7 for how Flash C implements the same feature set.

## 6. The run-time libraries of the reference product

*Method.* REA could not open the OMF `.LIB` archives (unsupported binary format), so `extract_libs.py` parses the Intel OMF
records statically (THEADR, LNAMES, SEGDEF, GRPDEF, PUBDEF, EXTDEF, COMDEF, LEDATA, FIXUPP; the archive header and page size) and writes
plain-text extracts (`lib_inventory.txt`, `lib_modules_<LIB>.txt`, `lib_dependencies.txt`, `startup_objects.txt`,
`headers_inventory.txt`). Brand names are redacted to `<brand>`. Nothing was executed or disassembled.

*Findings.*

| Observation (dump) | Evidence | Consequence for Flash C |
|---|---|---|
| One standard library per memory model (`CS CM CC CL CH`), each with **588 modules / ~870 public symbols**, about one function per module | `lib_inventory.txt` | library is split into one function group per module; the linker keeps only what is referenced (Flash C: function-level reachability) |
| Math is a separate library per model (`MATHx`, 76 modules: `strtod, atof, ecvt, fcvt, gcvt, __matherr, _fpreset, _control87, ...`) plus `FP87`/`EMU` for the coprocessor/emulator | `lib_modules_MATHS.txt`, `lib_modules_FP87.txt` | `runtime/lib/m/` is a separate library, linked only when floating point is used |
| One start-up object per model (`C0x.OBJ`) exporting `_argc/_argv/_environ`, `__RealCvtVector`, `__ScanTodVector`, `__cleanup`, `_abort`, `_stklen` and the `_INIT_/_INITEND_/_EXIT_/_EXITEND_` segments | `startup_objects.txt` | `runtime/lib/c0.c`: same names, `#pragma startup/exit` lists in generated `__fc_init/__fc_exit` |
| `printf/scanf` reach float conversion only through the two **vectors** in the start-up object, which are set by an init-segment entry of the math library | `startup_objects.txt`, `lib_dependencies.txt` | `__RealCvtVector/__ScanTodVector` + `fpcvt.c` registering them with `#pragma startup`; without them: `printf : floating point formats not linked` |
| `WILDARGS.OBJ` is an optional start-up object | `startup_objects.txt` | `runtime/lib/wildargs.c`, enabled by listing `wildargs.obj` or *Options -> Libraries -> Wildcard arguments* |
| Link order in the IDE/driver strings: start-up object, user objects, [graphics], math, emulator/8087, standard library | strings in the driver | same order is used for module selection and the link map |
| IDE: *Options -> Linker* (Map file Off/Segments/Publics/Detailed; Initialize segments; Default libraries; Warn duplicate symbols; "No stack" warning; Case-sensitive link) and *Libraries* (Graphics, Vision, Container Class, Standard Run Time); floating point None/Emulation/8087 | menu and dialog string tables | Map file -> `-M/-Mp/-Md`; Default libraries -> `-ln`; floating point -> `-f-/-f/-f87`; the Libraries dialog now offers Standard run-time, Math and Wildcard arguments (graphics, class libraries and overlays are out of scope) |
| `GRAPHICS.LIB` (4 modules, 122 publics) and `OVERLAY.LIB` | `lib_inventory.txt` | deliberately not implemented |
| Public headers (`headers_inventory.txt`): ~50 headers, e.g. `stdio stdlib string ctype math time io dir dos conio alloc mem process signal setjmp ...` | `headers_inventory.txt` | the same header set is provided (graphics, `bios`/ports/interrupt calls reduced to what makes sense on a terminal) |

*What differs on purpose.* The reference library targets 16-bit DOS (`int` is 16 bits, far pointers, BIOS/DOS interrupts); Flash C's
library targets Linux x86-64 (`int` 32 bits, `long` and pointers 64 bits, Linux system calls), so module internals are original
code; only the **structure** (start-up object, per-function modules, vectors for floating point, init/exit segments, header set, the
documented behaviour of each function such as the `rand()` generator and `matherr` messages) follows the evidence.

## 7. The source-level debugger

`TC.EXE` is a New Executable (NE) image; the analysis engine used for the rest of this study could not open it as a
whole, so the debugger's feature set is established from its string tables (`tc_strings_all.txt`,
`menus_items.txt`, `hint_texts.txt`), which carry every menu entry, dialog caption, control and message. The internal
design (symbol-table format, how it resumes the program) is not observable that way and Flash C does not copy it.

| Evidence (strings) | Meaning | Flash C |
|---|---|---|
| Run: `Program reset Ctrl+F2`, `Go to cursor F4`, `Trace into F7`, `Step over F8`, `Arguments...` | execution control | same keys and items |
| Debug: `Inspect... Alt+F4`, `Evaluate/modify... Ctrl+F4`, `Call stack... Ctrl+F3`, `Watches` (Add Ctrl+F7, Delete, Edit, Remove all), `Toggle breakpoint Ctrl+F8`, `Breakpoints...` | inspection and breakpoints | same |
| Windows `Watch`, `Register (%s)` | live windows | Watch shows live values; Register shows the CPU registers |
| `Options|Debugger`: Source debugging On/Standalone/None, Display swapping None/Smart/Always, Inspectors (decimal/hex/both), Program heap size | behaviour switches | honoured: source debugging selects debug info, swapping selects when the user screen appears, the radix applies to values |
| Compiler: `Line numbers debug info`, `Debug info in OBJs`; assembler `?debug L/B/E/S` records | the compiler emits line and block info | `note_line` line table, per-function variable scopes |
| Dialogs: Breakpoints list (`Breakpoint List`, `Line#`, `Condition`, `Pass`; OK, Edit, Delete, View, At), Modify/New breakpoint (`Condition`, `Pass Count`, `File Name`, `Line Number`), Breakpoint at function (`Symbol Name`) | conditional breakpoints with pass counts | `Debug|Breakpoints...` |
| `Evaluate and Modify` (`Expression`, `Result`, `New Value`), `Data Inspect` / `Modify Data`, `Range Inspect` (`Starting Index`, `Count`), `Call Stack` | data access | same dialogs |
| Messages `No program loaded.`, `Program terminated.`, `Program exit code: `, `Cannot be modified`, `Does not have range.`, `Cannot inspect: type not available`, `Too many inspectors` | status text | the same wording where it applies |
| Mouse options: right button invokes Go to cursor / Toggle breakpoint / Inspect / Evaluate / Add watch; desktop options `Save all watch expressions` / `breakpoints` | | desktop file stores watches and breakpoints |

## 8. Bit-fields

REA could not open `TCC.EXE` (a 16-bit NE image), so the evidence is again the compiler's string table. It contains exactly
these diagnostics about bit-fields, which Flash C reproduces word for word:

| Message in the reference compiler | Flash C condition |
|---|---|
| `Bit fields must be signed or unsigned int` | field declared `char`, `short` or `long` |
| `Bit fields must have integral type` | field of float, pointer, array or aggregate type |
| `Bit fields must contain at least one bit` | named field of width 0 (or negative) |
| `Bit field too large` | width above the size of `int` (32 in Flash C, 16 in the 16-bit original) |
| `Illegal to take address of bit field` | unary `&` applied to a field |
| `sizeof may not be applied to a bit field` | `sizeof` of a field |

There is no IDE option for bit-fields in the reference product (the Options dialogs contain no related switch), so no
dialog changed. The allocation order inside the storage unit is not observable from strings; Flash C uses the common
least-significant-bit-first layout (`tests/cases/t21_bitfields.c` is checked against the host compiler).


## 9. The help system

`TCHELP.TCH` (about 1.5 MB) was examined with REA's byte-entropy tool and `strings`/`xxd`. Observed:
a text signature header, a small index block, then a long high-entropy region (about 6.7-6.9 bits
per byte), i.e. compressed topic text; plain strings only show keyword index entries (`#define`,
`#include`, function names). So the reference uses one compressed database with a keyword index and
cross-references. REA could not open the file as an executable. Only these structural facts were used:
one external file, a keyword index, hyper-linked topics, context lookup of the word at the cursor,
Index, Previous topic and examples that can be copied into the editor. Flash C's help text is
original; its file format (`.fch`, plain text) is unrelated to the reference format.


## 10. Compiler options for Linux executables

*Evidence.* REA cannot open `TCC.EXE`/`TC.EXE` (NE images), so the switch table (`TCC.EXE` usage text: `-A -C -D -N -O -U -Z -a -b -d
-k -l -r -u -y -w…`) and the dialog captions/hints of `TC.EXE` (Code Generation, Entry/Exit Code, Optimization Options, Source Options,
the five warning pages, Linker, Segment Names) are the source.

| Reference option (switch) | Used? | Flash C |
|---|---|---|
| Treat enums as ints (`-b`) | yes | `Code generation`, `-b` / `-b-` |
| Duplicate strings merged (`-d`) | yes | `Code generation`, `-d` |
| Word alignment (`-a`) | adapted | "Align structure members" (default on); off packs structs, `-a-` |
| Test stack overflow (`-N`) | yes | `Code generation`, `-N`, run-time check against the `RLIMIT_STACK` based limit |
| Jump optimization (`-O`), Suppress redundant loads (`-Z`) | yes | new `Optimizations` dialog |
| Keywords: Turbo C++ / ANSI (`-A`) | partly | `Source`: Flash C / ANSI (UNIX V and K&R keyword sets have no meaning here) |
| Undefine macro (`-U`) | yes | `Code generation`, Undefine symbols |
| Warnings `-rch -amb -stv -cln -ucp -cpt` | yes | the matching pages of `Messages` |
| Linker "Initialize segments", segment names, "No stack" warning, case-sensitive link | no | no segments or linker exist |
| Stack size (`_stklen`) | adapted | `Linker`: Stack size (KB), applied with `setrlimit` |
| `-y`/`-v` debug info in OBJs | adapted | `Linker`: ELF symbol table (`.symtab`) for `nm`/`gdb`/`perf` |
| (none in the reference) | new | `Linker`: Protect code (W^X) for the ELF output |
| Memory models, instruction sets (`-1 -2`), Pascal calls (`-p`), standard stack frame (`-k`), register variables (`-r`), underscores (`-u`), overlays, assembler output (`-B -S`), C++ switches | no | meaningless for flat x86-64 Linux code |
