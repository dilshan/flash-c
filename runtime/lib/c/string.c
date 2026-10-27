/*
 * File:    string.c
 * Purpose: string - ANSI string functions
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <string.h>
#include <stdlib.h>

/* Returns the length of a string. */
size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) {
        p++;
    }
    return p - s;
}

/* Copies a string. Returns s1. */
char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

/* Copies at most n characters of a string. Returns s1. */
char *strncpy(char *d, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && s[i]; i++) {
        d[i] = s[i];
    }
    for (; i < n; i++) {
        d[i] = 0;
    }
    return d;
}

/* Appends a string. Returns s1. */
char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) {
        d++;
    }
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

/* Appends at most n characters of a string. Returns s1. */
char *strncat(char *d, const char *s, size_t n)
{
    char *r = d;
    while (*d) {
        d++;
    }
    while (n-- && *s) {
        *d++ = *s++;
    }
    *d = 0;
    return r;
}

/*
 * Compares two strings. Returns negative, zero or positive if s1 is less than, equal to or
 * greater than s2.
 */
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/*
 * Compares at most n characters of two strings. Returns negative, zero or positive, like
 * strcmp.
 */
int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n) {
        return 0;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/*
 * Finds the first occurrence of a character in a string. Returns a pointer to the character,
 * or NULL if it is not found.
 */
char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) {
            return (char *)s;
        }
        if (!*s) {
            return NULL;
        }
    }
}

/*
 * Finds the last occurrence of a character in a string. Returns a pointer to the character, or
 * NULL if it is not found.
 */
char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) {
            r = s;
        }
        if (!*s) {
            return (char *)r;
        }
    }
}

/*
 * Finds a substring. Returns a pointer to the first occurrence of s2 in s1, s1 if s2 is empty,
 * or NULL.
 */
char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) {
        return (char *)h;
    }
    for (; *h; h++) {
        if (*h == *n && !strncmp(h, n, nl)) {
            return (char *)h;
        }
    }
    return NULL;
}

/*
 * Counts the characters at the start of a string that are in a set. Returns the length of the
 * initial part of s1 that consists only of characters of s2.
 */
size_t strspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && strchr(set, s[n])) {
        n++;
    }
    return n;
}

/*
 * Counts the characters at the start of a string that are not in a set. Returns the length of
 * the initial part of s1 that contains no character of s2.
 */
size_t strcspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && !strchr(set, s[n])) {
        n++;
    }
    return n;
}

/*
 * Finds the first character of a string that is in a set. Returns a pointer to the character,
 * or NULL.
 */
char *strpbrk(const char *s, const char *set)
{
    for (; *s; s++) {
        if (strchr(set, *s)) {
            return (char *)s;
        }
    }
    return NULL;
}

static char *__tok_save;

/*
 * Splits a string into tokens. Returns a pointer to the next token, or NULL when there are no
 * more.
 */
char *strtok(char *s, const char *delim)
{
    char *start;
    if (!s) {
        s = __tok_save;
    }
    if (!s) {
        return NULL;
    }
    s += strspn(s, delim);
    if (!*s) {
        __tok_save = NULL;
        return NULL;
    }
    start = s;
    s += strcspn(s, delim);
    if (*s) {
        *s = 0;
        __tok_save = s + 1;
    } else {
        __tok_save = NULL;
    }
    return start;
}

/*
 * Compares two strings using the current locale. Returns negative, zero or positive, like
 * strcmp.
 */
int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

/*
 * Transforms a string so that strcmp on the results equals strcoll on the originals. Returns
 * the length of the transformed string.
 */
size_t strxfrm(char *d, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (n) {
        size_t k = l < n - 1 ? l : n - 1;
        memcpy(d, s, k);
        d[k] = 0;
    }
    return l;
}
