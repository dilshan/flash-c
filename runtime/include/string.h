/*
 * File:    string.h
 * Purpose: Memory block and string functions.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_STRING_H
#define _FC_STRING_H
#include <_defs.h>
void *memccpy(void *d, const void *s, int c, size_t n);
void *memchr(const void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memicmp(const void *a, const void *b, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void movmem(const void *src, void *dst, unsigned n);
void setmem(void *dst, unsigned n, char c);
char *stpcpy(char *d, const char *s);
char *strcat(char *d, const char *s);
char *strchr(const char *s, int c);
int strcmp(const char *a, const char *b);
int strcmpi(const char *a, const char *b);
int strcoll(const char *a, const char *b);
char *strcpy(char *d, const char *s);
size_t strcspn(const char *s, const char *set);
char *strdup(const char *s);
char *strerror(int errnum);
char *_strerror(const char *s);
int stricmp(const char *a, const char *b);
size_t strlen(const char *s);
char *strlwr(char *s);
char *strncat(char *d, const char *s, size_t n);
int strncmp(const char *a, const char *b, size_t n);
char *strncpy(char *d, const char *s, size_t n);
int strnicmp(const char *a, const char *b, size_t n);
char *strnset(char *s, int c, size_t n);
char *strpbrk(const char *s, const char *set);
char *strrchr(const char *s, int c);
char *strrev(char *s);
char *strset(char *s, int c);
size_t strspn(const char *s, const char *set);
char *strstr(const char *h, const char *n);
char *strtok(char *s, const char *delim);
char *strupr(char *s);
size_t strxfrm(char *d, const char *s, size_t n);
#endif
