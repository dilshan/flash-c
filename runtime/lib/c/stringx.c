/*
 * File:    stringx.c
 * Purpose: stringx - string functions that are extensions of the ANSI set
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Compares two strings ignoring case. */
int stricmp(const char *a, const char *b)
{
    while (*a && tolower(*a) == tolower(*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Compares two strings ignoring case. */
int strcmpi(const char *a, const char *b)
{
    return stricmp(a, b);
}

/* Compares at most n characters of two strings ignoring case. */
int strnicmp(const char *a, const char *b, size_t n)
{
    while (n && *a && tolower(*a) == tolower(*b)) {
        a++;
        b++;
        n--;
    }
    if (!n) {
        return 0;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Converts a string to upper case in place. */
char *strupr(char *s)
{
    char *p;
    for (p = s; *p; p++) {
        *p = toupper((unsigned char)*p);
    }
    return s;
}

/* Converts a string to lower case in place. */
char *strlwr(char *s)
{
    char *p;
    for (p = s; *p; p++) {
        *p = tolower((unsigned char)*p);
    }
    return s;
}

/* Reverses a string in place. */
char *strrev(char *s)
{
    size_t i = 0, j = strlen(s);
    while (j > i + 1) {
        char t = s[i];
        s[i] = s[j - 1];
        s[j - 1] = t;
        i++;
        j--;
    }
    return s;
}

/* Sets every character of a string to c. */
char *strset(char *s, int c)
{
    char *p;
    for (p = s; *p; p++) {
        *p = c;
    }
    return s;
}

/* Sets at most n characters of a string to c. */
char *strnset(char *s, int c, size_t n)
{
    char *p;
    for (p = s; *p && n; p++, n--) {
        *p = c;
    }
    return s;
}

/* Returns a malloc'd copy of a string. */
char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

/* Copies a string and returns a pointer to its terminating null character. */
char *stpcpy(char *d, const char *s)
{
    while ((*d = *s++) != 0) {
        d++;
    }
    return d;
}
