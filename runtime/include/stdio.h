/*
 * File:    stdio.h
 * Purpose: Standard input and output: streams, files and formatted I/O.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_STDIO_H
#define _FC_STDIO_H
#include <_defs.h>
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 1024
#define FOPEN_MAX 20
#define _NFILE 20
#define FILENAME_MAX 260
#define L_tmpnam 32
#define TMP_MAX 32767
#define P_tmpdir "/tmp"
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
typedef long fpos_t;

typedef struct _FILE {
    int fd, flags;
    int rpos, rlen, wlen, eof, err, unget;
    int mode, tmp;
    char *rbuf;
    char *wbuf;
    char *name;
} FILE;

/* FILE.flags */
#define _F_READ 1
#define _F_WRITE 2
#define _F_LINE 4
#define _F_RAW 8
#define _F_USED 16
#define _F_APPEND 32
extern FILE _streams[_NFILE];
#define stdin (&_streams[0])
#define stdout (&_streams[1])
#define stderr (&_streams[2])
#define stdaux (&_streams[3])
#define stdprn (&_streams[4])
void clearerr(FILE *f);
int fclose(FILE *f);
int fcloseall(void);
FILE *fdopen(int fd, const char *mode);
int feof(FILE *f);
int ferror(FILE *f);
int fflush(FILE *f);
int fgetc(FILE *f);
int fgetchar(void);
int fgetpos(FILE *f, fpos_t *pos);
char *fgets(char *buf, int n, FILE *f);
int fileno(FILE *f);
int flushall(void);
FILE *fopen(const char *name, const char *mode);
int fprintf(FILE *f, const char *fmt, ...);
int fputc(int c, FILE *f);
int fputchar(int c);
int fputs(const char *s, FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
FILE *freopen(const char *name, const char *mode, FILE *f);
int fscanf(FILE *f, const char *fmt, ...);
int fseek(FILE *f, long off, int whence);
int fsetpos(FILE *f, const fpos_t *pos);
FILE *_fsopen(const char *name, const char *mode, int shflg);
long ftell(FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int getc(FILE *f);
int getchar(void);
char *gets(char *buf);
int getw(FILE *f);
void perror(const char *msg);
int printf(const char *fmt, ...);
int putc(int c, FILE *f);
int putchar(int c);
int puts(const char *s);
int putw(int w, FILE *f);
int remove(const char *name);
int rename(const char *from, const char *to);
void rewind(FILE *f);
int rmtmp(void);
int scanf(const char *fmt, ...);
void setbuf(FILE *f, char *buf);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
char *tempnam(const char *dir, const char *prefix);
FILE *tmpfile(void);
char *tmpnam(char *buf);
int ungetc(int c, FILE *f);
int unlink(const char *name);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vfscanf(FILE *f, const char *fmt, va_list ap);
int vprintf(const char *fmt, va_list ap);
int vscanf(const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsscanf(const char *s, const char *fmt, va_list ap);
#endif
