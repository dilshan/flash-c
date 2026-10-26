/*
 * File:    putc.c
 * Purpose: putc - character and string output
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <_fcrt.h>
#include <_fcio.h>

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;
    if (!(f->flags & _F_WRITE)) { f->err = 1; errno = EBADF; return EOF; }
    if (f->rlen > f->rpos) __syscall(SYS_lseek, f->fd, -(long)(f->rlen - f->rpos), 1);      /* leaving read mode */
    f->rpos = f->rlen = 0; f->unget = 0;
    if (!__fl_wbuf(f)) {                           /* no memory for a buffer: write the character directly */
        if (__syscall(SYS_write, f->fd, &ch, 1) != 1) { f->err = 1; return EOF; }
        return ch;
    }
    f->wbuf[f->wlen++] = ch;
    if (f->wlen >= __BUFSZ || (f->flags & _F_RAW) || ((f->flags & _F_LINE) && ch == '\n'))
        if (__fl_flush(f)) return EOF;
    return ch;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fputchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f)
{
    while (*s) if (fputc((unsigned char)*s++, f) == EOF) return EOF;
    return 0;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 0;
}

int putw(int w, FILE *f)
{
    return fwrite(&w, sizeof w, 1, f) == 1 ? w : EOF;
}
