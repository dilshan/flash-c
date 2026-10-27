/*
 * File:    getc.c
 * Purpose: getc - character and line input
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <_fcrt.h>
#include <_fcio.h>

/*
 * Reads the next character from a stream. Returns the character, or EOF at end of file or on
 * error.
 */
int fgetc(FILE *f)
{
    if (!(f->flags & _F_READ)) {
        f->err = 1;
        errno = EBADF;
        return EOF;
    }
    if (f->unget) {
        int c = f->unget - 1;
        f->unget = 0;
        return c;
    }
    if (f->rpos >= f->rlen) {
        f->rpos = f->rlen = 0;
        if (!__fl_fill(f)) {
            return EOF;
        }
    }
    return (unsigned char)f->rbuf[f->rpos++];
}

/*
 * Reads the next character from a stream. Returns the character, or EOF at end of file or on
 * error.
 */
int getc(FILE *f)
{
    return fgetc(f);
}

/* Reads the next character from stdin. Returns the character, or EOF. */
int getchar(void)
{
    return fgetc(stdin);
}

/* Reads a character from standard input (same as getchar). */
int fgetchar(void)
{
    return fgetc(stdin);
}

/* Pushes a character back onto an input stream. Returns c, or EOF on failure. */
int ungetc(int c, FILE *f)
{
    if (c == EOF || f->unget || !(f->flags & _F_READ)) {
        return EOF;
    }
    f->unget = (unsigned char)c + 1;
    f->eof = 0;
    return (unsigned char)c;
}

/*
 * Reads a line from a stream. Returns s, or NULL at end of file with nothing read, or on
 * error.
 */
char *fgets(char *buf, int n, FILE *f)
{
    int i = 0, c;
    if (n <= 0) {
        return NULL;
    }
    while (i < n - 1) {
        c = fgetc(f);
        if (c == EOF) {
            break;
        }
        buf[i++] = c;
        if (c == '\n') {
            break;
        }
    }
    if (!i) {
        return NULL;
    }
    buf[i] = 0;
    return buf;
}

/* Reads a line from stdin. Returns s, or NULL at end of file with nothing read, or on error. */
char *gets(char *buf)
{
    int i = 0, c;
    for (;;) {
        c = fgetc(stdin);
        if (c == EOF || c == '\n') {
            break;
        }
        buf[i++] = c;
    }
    if (c == EOF && !i) {
        return NULL;
    }
    buf[i] = 0;
    return buf;
}

/* Reads an int in the machine's binary format from a stream. */
int getw(FILE *f)
{
    int w;
    return fread(&w, sizeof w, 1, f) == 1 ? w : EOF;
}
