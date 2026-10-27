/*
 * File:    mem.c
 * Purpose: mem - memory block functions
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Copies memory. Returns s1. */
void *memcpy(void *d, const void *s, size_t n)
{
    char *dp = d;
    const char *sp = s;
    while (n >= 8) {
        *(long *)dp = *(const long *)sp;
        dp += 8;
        sp += 8;
        n -= 8;
    }
    while (n--) {
        *dp++ = *sp++;
    }
    return d;
}

/* Copies memory that may overlap. Returns s1. */
void *memmove(void *d, const void *s, size_t n)
{
    char *dp = d;
    const char *sp = s;
    if (dp < sp) {
        while (n--) {
            *dp++ = *sp++;
        }
    } else {
        dp += n;
        sp += n;
        while (n--) {
            *--dp = *--sp;
        }
    }
    return d;
}

/* Fills memory with a byte. Returns s. */
void *memset(void *d, int c, size_t n)
{
    char *dp = d;
    long w = (unsigned char)c;
    w = w | (w << 8);
    w = w | (w << 16);
    w = w | (w << 32);
    while (n >= 8) {
        *(long *)dp = w;
        dp += 8;
        n -= 8;
    }
    while (n--) {
        *dp++ = c;
    }
    return d;
}

/*
 * Compares two blocks of memory. Returns negative, zero or positive if the first block is less
 * than, equal to or greater than the second.
 */
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y) {
            return *x - *y;
        }
        x++;
        y++;
    }
    return 0;
}

/* Compares two memory blocks ignoring case. */
int memicmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        int p = tolower(*x), q = tolower(*y);
        if (p != q) {
            return p - q;
        }
        x++;
        y++;
    }
    return 0;
}

/* Searches memory for a byte. Returns a pointer to the byte, or NULL if it is not found. */
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) {
            return (void *)p;
        }
        p++;
    }
    return NULL;
}

/*
 * Copies bytes until c has been copied or n bytes are done; returns the position after c or NULL.
 */
void *memccpy(void *d, const void *s, int c, size_t n)
{
    char *dp = d;
    const char *sp = s;
    while (n--) {
        *dp = *sp++;
        if ((unsigned char)*dp++ == (unsigned char)c) {
            return dp;
        }
    }
    return NULL;
}

/* Copies n bytes (the blocks may overlap). */
void movmem(const void *src, void *dst, unsigned n)
{
    memmove(dst, src, n);
}

/* Fills n bytes with c. */
void setmem(void *dst, unsigned n, char c)
{
    memset(dst, c, n);
}

/* Copies nbytes bytes swapping each pair of adjacent bytes. */
void swab(char *from, char *to, int nbytes)
{
    int i;
    for (i = 0; i + 1 < nbytes; i += 2) {
        char t = from[i];
        to[i] = from[i + 1];
        to[i + 1] = t;
    }
}
