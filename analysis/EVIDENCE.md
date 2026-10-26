# How the reference IDE (`TC.EXE`) works — evidence

This report documents what was learned from the reference installation in
`research/TC` and how each finding was used to design Flash C. Everything below can
be reproduced with:

```
python3 -I analysis/extract_evidence.py /path/to/TC/BIN      # writes analysis/dumps/*.txt
```

Raw extracts are in `analysis/dumps/`. Product names of the reference tool are
replaced with `<brand>` in every dump (the Flash C sources contain none).
Line references like `menus_items.txt:1-40` point into those dumps.

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
* **Options**: Application… · Compiler ▶ (Code generation, Advanced code generation, Entry/Exit Code, C++ options, Optimizations, Source, Messages ▶ [Display, Portability, ANSI violations, C++ warnings, Frequent errors, Less frequent errors], Names) · Transfer… · Make… · Linker ▶ (Settings, Libraries) · Librarian… · Debugger… · Directories… · Environment ▶ (Preferences, Editor, Mouse, Desktop, Startup, Colors) · Save…
* **Window**: Size/Move Ctrl+F5 · Zoom F5 · Tile · Cascade · Next F6 · Close Alt+F3 · Close all · — · Message · Output · Watch · User screen Alt+F5 · Register · Project · Project notes · — · List all… Alt+0
* **Help**: Contents · Index Shift+F1 · Topic search Ctrl+F1 · Previous topic Alt+F1 · Help on help

The bar itself is stored as ` File `, ` Edit `, … ` Help ` (space-padded titles). Flash C's `src/ide/menu.c` reproduces this table one-to-one (Alt+F4 is kept as shown; it is a terminal/WM shortcut on many systems, every command is also reachable through the menus).

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
| Per-model start-up object + runtime library + external linker | Not needed on Linux x86-64: the code generator writes a complete ELF executable; a small runtime (written in Flash C, `runtime/rt.c`) is compiled in and only the reachable functions are emitted |
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
* The debugger (Inspect, Call stack, breakpoints, trace) exists in the reference IDE (strings `Inspecting %s`, `Too many inspectors`, `Cannot inspect: type not available`, breakpoint dialogs); Flash C exposes the menu entries but does not implement a debugger yet.
