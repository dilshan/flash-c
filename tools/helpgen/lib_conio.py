# File:    lib_conio.py
# Purpose: Help text for the functions of <conio.h>, used by gen.py.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

H = "conio.h"
R0 = "Nothing."
F = [
("cgets","char *cgets(char *buf);","Reads a string from the console.","buf[0] must hold the maximum number of characters. The function stores the number of characters read in buf[1] and the string, ended with a null character, from buf[2]. Backspace edits the line; Enter ends it and echoes a newline.","A pointer to buf[2].","getch cscanf getpass","#include <conio.h>\n\nint main(void)\n{\n    char buf[82];\n    buf[0] = 80;\n    cputs(\"Name: \");\n    cgets(buf);\n    cprintf(\"Hello, %s (%d chars)\\r\\n\", buf + 2, buf[1]);\n    return 0;\n}"),
("clreol","void clreol(void);","Clears from the cursor to the end of the line in the text window.","The cursor does not move.",R0,"clrscr delline",None),
("clrscr","void clrscr(void);","Clears the text window and moves the cursor to its top left corner.","Only the current window (see window) is cleared.",R0,"window clreol gotoxy",None),
("cprintf","int cprintf(const char *fmt, ...);","Writes formatted output to the text window.","Same conversions as printf (see {printf formats}), but the output goes to the console using the current colours and window. A newline moves down only; use \\r\\n to start a new line at the left edge.","The number of characters written.","cputs putch printf","#include <conio.h>\n\nint main(void)\n{\n    clrscr();\n    textcolor(YELLOW);\n    textbackground(BLUE);\n    gotoxy(10, 5);\n    cprintf(\"Value: %d\", 42);\n    getch();\n    return 0;\n}"),
("cputs","int cputs(const char *s);","Writes a string to the text window.","No newline is added. Uses the current colours and window.","The last character written.","cprintf putch puts",None),
("cscanf","int cscanf(const char *fmt, ...);","Reads formatted input from the console.","Reads a line with cgets (echoed) and converts it as scanf does. See {scanf formats}.","The number of items assigned.","cgets scanf",None),
("delline","void delline(void);","Deletes the line with the cursor.","Lines below move up and a blank line appears at the bottom of the window.",R0,"insline clreol",None),
("getch","int getch(void);","Reads a key without echo.","Waits for a key. Extended keys (function keys, cursor keys) return 0 first, and the next call returns a scan code. The terminal is switched to raw mode while reading.","The character code.","getche kbhit ungetch","#include <conio.h>\n#include <stdio.h>\n\nint main(void)\n{\n    int c;\n    cputs(\"Press a key: \");\n    c = getch();\n    if (c == 0)\n        printf(\"extended key, scan code %d\\n\", getch());\n    else\n        printf(\"you pressed %c\\n\", c);\n    return 0;\n}"),
("getche","int getche(void);","Reads a key and echoes it.","Same as getch, but a printable character is written to the window.","The character code.","getch kbhit",None),
("gettextinfo","void gettextinfo(struct text_info *ti);","Gets the state of the text window.","Fills in the {text_info} structure: window corners, attributes, mode, screen size and cursor position.",R0,"window textmode wherex",None),
("getpass","char *getpass(const char *prompt);","Reads a password without echo.","The prompt is written first; the text is read until Enter. The result is in a static buffer of 127 characters.","A pointer to the password.","cgets getch",None),
("gotoxy","void gotoxy(int x, int y);","Moves the cursor inside the text window.","x and y count from 1 at the top left corner of the window. Positions outside the window are ignored.",R0,"wherex wherey window",None),
("highvideo","void highvideo(void);","Selects high intensity characters.","Sets the intensity bit of the text colour.",R0,"lowvideo normvideo textcolor",None),
("insline","void insline(void);","Inserts a blank line at the cursor.","Lines below move down; the bottom line of the window is lost.",R0,"delline",None),
("kbhit","int kbhit(void);","Tests whether a key is waiting.","Does not read the key. Use getch to read it.","Nonzero if a key can be read, otherwise 0.","getch","#include <conio.h>\n#include <stdio.h>\n#include <dos.h>\n\nint main(void)\n{\n    int n = 0;\n    while (!kbhit()) {\n        printf(\"\\rwaiting %d\", n++);\n        fflush(stdout);\n        delay(100);\n    }\n    getch();\n    return 0;\n}"),
("lowvideo","void lowvideo(void);","Selects low intensity characters.","Clears the intensity bit of the text colour.",R0,"highvideo normvideo",None),
("normvideo","void normvideo(void);","Restores the colours that were in use when the program started.","",R0,"highvideo lowvideo",None),
("putch","int putch(int c);","Writes a character to the text window.","Handles \\a, \\b, \\n and \\r; the window scrolls at the bottom.","c.","cputs cprintf putchar",None),
("_setcursortype","void _setcursortype(int type);","Selects the cursor shape.","type is _NOCURSOR (hidden), _SOLIDCURSOR (block) or _NORMALCURSOR (the terminal's default).",R0,"gotoxy",None),
("textattr","void textattr(int attr);","Sets text colour and background together.","attr = foreground + (background << 4), plus BLINK (128) for blinking. Colours are the constants listed in {Console colors}.",R0,"textcolor textbackground","#include <conio.h>\n\nint main(void)\n{\n    textattr(YELLOW + (RED << 4));\n    clrscr();\n    cputs(\"Yellow on red\");\n    getch();\n    normvideo();\n    return 0;\n}"),
("textbackground","void textbackground(int color);","Sets the background colour.","color is 0 (BLACK) to 7 (LIGHTGRAY); see {Console colors}.",R0,"textcolor textattr",None),
("textcolor","void textcolor(int color);","Sets the foreground colour.","color is 0 to 15, optionally plus BLINK; see {Console colors}.",R0,"textbackground textattr",None),
("textmode","void textmode(int mode);","Selects a text mode.","The terminal decides the real size. The call resets the window to the whole screen and the colours to normal. mode is one of BW40, C40, BW80, C80, MONO, C4350 or LASTMODE; only the screen size reported by gettextinfo follows the terminal, and the 43/50 line mode is not forced.",R0,"gettextinfo window",None),
("ungetch","int ungetch(int c);","Pushes a character back to the keyboard.","One character can be pushed back; it is returned by the next getch or getche.","c, or EOF if a character is already waiting.","getch",None),
("wherex","int wherex(void);","Returns the column of the cursor in the text window.","Counted from 1.","The column.","wherey gotoxy",None),
("wherey","int wherey(void);","Returns the row of the cursor in the text window.","Counted from 1.","The row.","wherex gotoxy",None),
("window","void window(int left, int top, int right, int bottom);","Defines the active text window.","The corners are screen coordinates counted from 1. If they are invalid, the call is ignored. Output with cprintf, cputs, putch and clrscr is limited to the window, and the cursor moves to its top left corner.",R0,"gotoxy clrscr gettextinfo","#include <conio.h>\n\nint main(void)\n{\n    textbackground(BLUE);\n    window(10, 5, 50, 15);\n    clrscr();\n    cputs(\"Inside the window\\r\\n\");\n    getch();\n    return 0;\n}"),
]
TYPES = [
("text_info","struct text_info, filled by gettextinfo: unsigned char winleft, wintop, winright, winbottom (window corners), attribute (current colours), normattr (colours at start), currmode (text mode), screenheight, screenwidth, curx, cury (cursor position in the window). Declared in conio.h."),
("Console colors","Constants for textcolor, textbackground and textattr: BLACK 0, BLUE 1, GREEN 2, CYAN 3, RED 4, MAGENTA 5, BROWN 6, LIGHTGRAY 7, DARKGRAY 8, LIGHTBLUE 9, LIGHTGREEN 10, LIGHTCYAN 11, LIGHTRED 12, LIGHTMAGENTA 13, YELLOW 14, WHITE 15, BLINK 128 (add to the foreground). Backgrounds use 0 to 7. Text modes: BW40 0, C40 1, BW80 2, C80 3, MONO 7, C4350 64, LASTMODE -1. Cursor types: _NOCURSOR 0, _SOLIDCURSOR 1, _NORMALCURSOR 2."),
("directvideo","int directvideo. Kept for compatibility. Console output always goes through the terminal, so the value has no effect."),
]
