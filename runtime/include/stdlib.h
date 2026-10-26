/*
 * File:    stdlib.h
 * Purpose: General utilities: memory, conversions, random numbers, sorting,
 *          program control.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_STDLIB_H
#define _FC_STDLIB_H
#include <_defs.h>
#include <time.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767
#define MB_CUR_MAX 1
typedef struct _div_t { int quot, rem; } div_t;
typedef struct _ldiv_t { long quot, rem; } ldiv_t;
typedef void (*atexit_t)(void);
extern int _argc;
extern char **_argv;
extern char **environ;
extern unsigned _stklen;
extern int _fmode;
void abort(void);
int abs(int v);
int atexit(atexit_t fn);
double atof(const char *s);
int atoi(const char *s);
long atol(const char *s);
void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void *calloc(size_t n, size_t size);
div_t div(int num, int den);
char *ecvt(double v, int ndig, int *dec, int *sign);
void exit(int code);
void _exit(int code);
void _cexit(void);
void _c_exit(void);
char *fcvt(double v, int ndig, int *dec, int *sign);
void free(void *p);
char *gcvt(double v, int ndig, char *buf);
char *getenv(const char *name);
char *itoa(int v, char *buf, int base);
long labs(long v);
ldiv_t ldiv(long num, long den);
void *lfind(const void *key, const void *base, size_t *n, size_t size, int (*cmp)(const void *, const void *));
void *lsearch(const void *key, void *base, size_t *n, size_t size, int (*cmp)(const void *, const void *));
char *ltoa(long v, char *buf, int base);
void *malloc(size_t n);
int mblen(const char *s, size_t n);
size_t mbstowcs(wchar_t *dst, const char *src, size_t n);
int mbtowc(wchar_t *wc, const char *s, size_t n);
int putenv(const char *entry);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
int rand(void);
void *realloc(void *p, size_t n);
void srand(unsigned int seed);
double strtod(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
void swab(char *from, char *to, int nbytes);
int system(const char *cmd);
char *ultoa(unsigned long v, char *buf, int base);
size_t wcstombs(char *dst, const wchar_t *src, size_t n);
int wctomb(char *s, wchar_t wc);
unsigned int _rotl(unsigned int v, int count);
unsigned int _rotr(unsigned int v, int count);
unsigned long _lrotl(unsigned long v, int count);
unsigned long _lrotr(unsigned long v, int count);
char *_fullpath(char *buf, const char *path, size_t n);
void _makepath(char *path, const char *drive, const char *dir, const char *name, const char *ext);
void _splitpath(const char *path, char *drive, char *dir, char *name, char *ext);
void _searchenv(const char *file, const char *varname, char *buf);
#define random(num) (rand() % (num))
#define randomize() srand((unsigned)time(0))
#endif
