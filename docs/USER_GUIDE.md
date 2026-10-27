# Flash C user guide

How to use the `fc` IDE: write, build, run and debug C programs without leaving the terminal.
[Back to the README](../README.md) · Next: [Compiler reference](COMPILER.md)

**Contents**

1. [Starting the IDE](#1-starting-the-ide)
2. [The screen](#2-the-screen)
3. [Your first program](#3-your-first-program)
4. [Keyboard reference](#4-keyboard-reference)
5. [The menus](#5-the-menus)
6. [The editor](#6-the-editor)
7. [Projects](#7-projects)
8. [Options](#8-options)
9. [The source-level debugger](#9-the-source-level-debugger)
10. [Online help](#10-online-help)
11. [Tips and troubleshooting](#11-tips-and-troubleshooting)

## 1. Starting the IDE

```
fc                      # start with an empty desktop
fc hello.c              # open hello.c (it is created when you save it)
fc a.c b.c              # open several files
fc myproject.fcp        # open a project
```

The IDE needs a terminal of at least 80×25 that understands UTF-8 and ANSI escape sequences. When
`COLORTERM=truecolor` is set it uses 24-bit colour, otherwise 16 colours. The mouse works in
terminals that report it (menus, window drag, resize, zoom and close, scroll bars, text selection).

## 2. The screen

![The editor](screenshots/editor.png)

* **Menu bar** (top). Press **F10**, or **Alt** plus the highlighted letter (Alt+F for File), and
  move with the arrow keys. **Esc** closes a menu.
* **Edit windows** hold your source files. The number at the top right is the window number
  (**Alt+0** lists them), the numbers at the bottom left are the line and column.
* **Message window** shows compiler errors and warnings.
* **Status line** (bottom) shows the keys that work right now. While a menu item is highlighted it
  shows a one-line description instead.

Dialog boxes work as you would expect: **Tab** moves between controls, **Space** switches a check
box, **Enter** presses the default button, **Esc** cancels. Every dialog has a **Help** button (or
press **F1**).

## 3. Your first program

1. Press **F10**, choose **File → New**, and type a program:

   ```c
   #include <stdio.h>

   int main()
   {
       printf("Hello, world\n");
       return 0;
   }
   ```

2. Press **F2** to save it (the IDE asks for a name the first time).
3. Press **F9** to build it. A box shows the number of lines, warnings and errors.
4. Press **Ctrl+F9** to run it. The program runs on the normal terminal screen, so your terminal's
   scroll-back keeps its output. Press **Alt+F5** to look at that output again.

If the build has errors, the Message window opens:

![Message window](screenshots/messages.png)

Press **Enter** on a message to jump to the source line, or **Space** to look at the line without
leaving the Message window. **Alt+F7** and **Alt+F8** move to the previous and next message from
the editor. Editing the text clears the red error bar.

## 4. Keyboard reference

Some terminals keep a few of these combinations for themselves (F11, Alt+F4 and Ctrl+F5 are common
examples). Every command is also in the menus.

### Files and windows

| Key | Action | Key | Action |
|---|---|---|---|
| F2 | Save | F3 | Open |
| Alt+X | Quit | Alt+0 | Window list |
| F5 | Zoom the window | F6 | Next window |
| Alt+F3 | Close the window | Ctrl+F5 | Size or move the window |
| F10 | Menu bar | Alt+F5 | User screen (program output) |

### Compile and run

| Key | Action | Key | Action |
|---|---|---|---|
| Alt+F9 | Compile the current file | F9 | Make |
| Ctrl+F9 | Run | Alt+F7 / Alt+F8 | Previous / next message |

### Editing

| Key | Action | Key | Action |
|---|---|---|---|
| Ctrl+Ins | Copy | Shift+Del | Cut |
| Shift+Ins | Paste | Ctrl+Del | Clear the selection |
| Alt+Backspace | Undo | Shift+Alt+Backspace | Redo |
| Ctrl+Y | Delete the line | Ctrl+T | Delete the word on the right |
| Ctrl+N | Insert a line | Ins | Insert / overwrite |
| Ctrl+Left / Right | Move by word | Ctrl+PgUp / PgDn | Start / end of the file |
| Shift + movement keys | Select text | Ctrl+L | Search again |

### Help

| Key | Action | Key | Action |
|---|---|---|---|
| F1 | Help contents | Ctrl+F1 | Help on the word at the cursor |
| Shift+F1 | Help index | Alt+F1 | Previous help topic |

### Debugger

| Key | Action | Key | Action |
|---|---|---|---|
| Ctrl+F8 | Set or clear a breakpoint | F4 | Run to the cursor |
| F7 | Trace into | F8 | Step over |
| Ctrl+F2 | End the debug session | Ctrl+F4 | Evaluate or modify |
| Alt+F4 | Inspect | Ctrl+F3 | Call stack |
| Ctrl+F7 | Add a watch | | |

## 5. The menus

| Menu | What it does |
|---|---|
| **≡** (system) | About, clear the desktop, repaint the screen. |
| **File** | New, Open, Save, Save as, Save all, Change directory, Print, Shell (back with `exit`), Quit. |
| **Edit** | Undo, Redo, Cut, Copy, Paste, Clear, *Copy example* (puts a help example in the editor), Show clipboard. |
| **Search** | Find, Replace (regular expressions are supported), Search again, Go to line number, Previous / Next error, Locate function. |
| **Run** | Run, Program reset, Go to cursor, Trace into, Step over, Arguments (the program's command line). |
| **Compile** | Compile, Make, Link, Build all, Information, Remove messages. |
| **Debug** | Inspect, Evaluate/modify, Call stack, Watches, Toggle breakpoint, Breakpoints. |
| **Project** | Open and close a project, add and delete items, Local options, Include files. |
| **Options** | Compiler, Transfer, Make, Linker, Debugger, Directories, Environment, Save. |
| **Window** | Size/Move, Zoom, Tile, Cascade, Next, Close, Close all, Message, Output, Watch, User screen, Register, Project, Project notes, List all. |
| **Help** | Contents, Index, Topic search, Previous topic, Help on help. |

![The Compile menu](screenshots/compile-menu.png)

**Compile versus Make versus Build all.** *Compile* checks the file in the active window. *Make*
compiles what changed and produces the executable. *Build all* recompiles everything. *Run* makes
the program first, then runs it.

## 6. The editor

* **Moving.** Arrows, Home, End, PgUp and PgDn; Ctrl+Left and Ctrl+Right move by word.
* **Selecting.** Hold Shift with a movement key, or drag with the mouse.
* **Block commands** (Ctrl+K, then a letter): `B` mark the start, `K` mark the end, `C` copy, `V`
  move, `Y` delete, `H` hide, `R` read a file into the text, `W` write the block to a file, `T`
  mark the word, `L` mark the line.
* **Quick commands** (Ctrl+Q, then a letter): `S` line start, `D` line end, `R` file start, `C`
  file end, `E` window top, `X` window bottom, `F` find, `A` replace, `Y` delete to the end of the
  line, `[` or `]` jump to the matching bracket.
* **Find and Replace** have options for case, whole words, regular expressions, prompting on
  replace, direction, scope (global or selected text) and origin (from the cursor or the whole text).
* **Syntax highlighting**, **auto-indent**, tab characters and *Backspace unindents* can be set in
  *Options → Environment → Editor*.

## 7. Projects

A project is a list of source files that make up one program, stored in a `.fcp` file.

* **Project → Open project** loads (or creates) the file. **Add item** and **Delete item** change
  the list; **Window → Project** shows it.
* All files of the project are compiled together. A function or global variable can be used from any
  file as long as it is declared before use; put shared declarations in a header file.
* **Project → Local options** sets, for the selected file: extra compiler switches such as `-O` or
  `-DDEBUG` (a project is compiled in one run, so the switches of all items apply to the whole
  build), an output path, *Exclude debug info* (the debugger will not step into this file) and
  *Exclude from link*.

## 8. Options

*Options → Save* writes all settings to `fcconfig.fc`; the IDE reads it from the current directory,
or else from `~/.fcconfig.fc`. *Options → Environment → Desktop* also saves the open windows,
breakpoints and watches in `fcdef.dsk`.

| Options entry | Settings |
|---|---|
| **Compiler → Code generation** | enum size, duplicate strings, structure alignment, stack-overflow test, floating point, `-D` and `-U` macros. |
| **Compiler → Optimizations** | the check list described below. |
| **Compiler → Source** | nested comments, Flash C or ANSI keywords, identifier length. |
| **Compiler → Messages** | which warnings and errors are displayed, in five groups. |
| **Linker** | map file, stack size, ELF symbol table, separate code and data segments, libraries. |
| **Debugger** | source debugging (On, Standalone, None), display swapping, number format. |
| **Directories** | include directories and the output directory. |
| **Environment** | preferences, editor, mouse, desktop, colours. |

![Code generation options](screenshots/code-generation-dialog.png)

### Optimizations

*Options → Compiler → Optimizations* is a check list of every optimization the compiler has: 48
entries, then jump optimization and redundant-load removal. Move with the arrow keys, PgUp and
PgDn, and press **Space** (or click the box) to switch the marked entry. The status line describes
the marked entry. Everything is on by default except *Code-size optimization*. **Cancel** restores
the old settings.

![Optimization options](screenshots/optimization-dialog.png)

Press **Help** or **F1** in the dialog to open the page about optimizations. It stays in front of
the dialog until you press **Esc**.

![Help for the optimization options](screenshots/optimization-help.png)

What each entry does is described in the [compiler reference](COMPILER.md#5-optimizations) and in
the online help. With *Source debugging* on, the optimizations that would change what the debugger
shows are skipped automatically.

## 9. The source-level debugger

*Options → Debugger → Source debugging* is **On** by default. The compiler then records line,
function, variable and type information, and the IDE runs the program under `ptrace`. (*Standalone*
records the information for a program run outside the IDE; *None* records nothing.)

![The debugger](screenshots/debugger.png)

The current statement is shown with a green bar, breakpoints with a red bar. In the picture the
program stopped inside `add` after a trace; the Watch window shows `total`.

| Command | Key | What it does |
|---|---|---|
| Run | Ctrl+F9 | runs to the next breakpoint, or to the end |
| Program reset | Ctrl+F2 | ends the debug session |
| Go to cursor | F4 | runs to the line under the cursor |
| Trace into | F7 | executes the next statement and enters user functions |
| Step over | F8 | executes the next statement; calls count as one |
| Toggle breakpoint | Ctrl+F8 | sets or clears a breakpoint |
| Breakpoints… | | lists, edits (condition, pass count, file, line) and deletes breakpoints |
| Evaluate/modify… | Ctrl+F4 | evaluates a C expression, assigns a new value |
| Inspect… | Alt+F4 | inspects structures, arrays and pointers; range inspect |
| Call stack… | Ctrl+F3 | functions that led here, with arguments; picks the frame used for evaluation |
| Watches | Ctrl+F7 | live values in the Watch window |
| Window → Register | | CPU registers of the stopped program |

Expressions understand locals, globals, `.`, `->`, `[]`, `*`, `&`, casts, `sizeof` and
assignment, and a `,x`, `,d` or `,c` suffix selects hexadecimal, decimal or character output. The
program's own output goes to the user screen (Alt+F5); *Display swapping* controls when it is
shown during a step.

Limits: expressions cannot call functions, and there is no reverse execution.

## 10. Online help

![Online help](screenshots/help.png)

* **F1** opens the contents. **Tab** moves between links, **Enter** follows one, **Alt+F1** goes
  back and **Esc** closes the help window.
* **Ctrl+F1** shows the topic for the word under the cursor, for example `printf` or `while`.
* **Shift+F1** opens a searchable index.
* The library reference (every function, with syntax, return value and examples) and the C language
  topics are read from the files in `help/`. **Edit → Copy example** puts an example into the
  editor.

Without the `help/` files only the basic topics built into the IDE are shown. The IDE looks for
them in `$FC_HELP`, next to the program, and in `/usr/local/share/flashc/help`, which is where
`make install` puts them.

## 11. Tips and troubleshooting

* **Keys do nothing or print odd characters.** Your terminal may be catching the key. Use the menus,
  or check the terminal's key bindings.
* **The screen looks wrong after a program ran.** Choose *≡ → Repaint desktop*.
* **Colours look different from the screenshots.** Use a terminal with 24-bit colour
  (`COLORTERM=truecolor`), or change the palette in *Options → Environment → Colors*.
* **The debugger does not stop.** Check that *Source debugging* is *On* and that the breakpoint is
  on a line that contains a statement.
* **Help only shows the basic topics.** The `help/` files were not found (the Contents page says
  so); set `FC_HELP` to their directory.
