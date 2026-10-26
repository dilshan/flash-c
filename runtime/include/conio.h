/*
 * File:    conio.h
 * Purpose: Console I/O: windows, colours, cursor control and unbuffered keyboard
 *          input.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_CONIO_H
#define _FC_CONIO_H
#include <_defs.h>
/* text modes */
#define LASTMODE (-1)
#define BW40 0
#define C40 1
#define BW80 2
#define C80 3
#define MONO 7
#define C4350 64
/* colours */
#define BLACK 0
#define BLUE 1
#define GREEN 2
#define CYAN 3
#define RED 4
#define MAGENTA 5
#define BROWN 6
#define LIGHTGRAY 7
#define DARKGRAY 8
#define LIGHTBLUE 9
#define LIGHTGREEN 10
#define LIGHTCYAN 11
#define LIGHTRED 12
#define LIGHTMAGENTA 13
#define YELLOW 14
#define WHITE 15
#define BLINK 128
#define _NOCURSOR 0
#define _SOLIDCURSOR 1
#define _NORMALCURSOR 2
struct text_info {
    unsigned char winleft, wintop, winright, winbottom;
    unsigned char attribute, normattr;
    unsigned char currmode;
    unsigned char screenheight, screenwidth;
    unsigned char curx, cury;
};
extern int directvideo;
char *cgets(char *buf);
void clreol(void);
void clrscr(void);
int cprintf(const char *fmt, ...);
int cputs(const char *s);
int cscanf(const char *fmt, ...);
void delline(void);
int getch(void);
int getche(void);
void gettextinfo(struct text_info *ti);
char *getpass(const char *prompt);
void gotoxy(int x, int y);
void highvideo(void);
void insline(void);
int kbhit(void);
void lowvideo(void);
void normvideo(void);
int putch(int c);
void _setcursortype(int type);
void textattr(int attr);
void textbackground(int color);
void textcolor(int color);
void textmode(int mode);
int ungetch(int c);
int wherex(void);
int wherey(void);
void window(int left, int top, int right, int bottom);
#endif
