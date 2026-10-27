/*
 * File:    qsort.c
 * Purpose: qsort - sorting and searching: quicksort with insertion sort for short
 *          runs, binary search
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdlib.h>
#include <string.h>

typedef int (*__cmp_t)(const void *, const void *);

/* Exchanges two elements of size sz. */
static void __swap(char *a, char *b, size_t sz)
{
    while (sz >= 8) {
        long t = *(long *)a;
        *(long *)a = *(long *)b;
        *(long *)b = t;
        a += 8;
        b += 8;
        sz -= 8;
    }
    while (sz--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* Sorts the small range lo..hi with insertion sort. */
static void __insertion(char *lo, char *hi, size_t sz, __cmp_t cmp)
{
    char *a, *b;
    for (a = lo + sz; a <= hi; a += sz) {
        for (b = a; b > lo && cmp(b - sz, b) > 0; b -= sz) {
            __swap(b - sz, b, sz);
        }
    }
}

/* Sorts an array. */
void qsort(void *base, size_t n, size_t sz, __cmp_t cmp)
{
    char *los[64], *his[64];
    int sp = 0;
    char *lo, *hi;
    if (n < 2 || !sz) {
        return;
    }
    lo = base;
    hi = lo + (n - 1) * sz;
    for (;;) {
        while (lo < hi) {
            size_t cnt = (size_t)(hi - lo) / sz + 1;
            char *mid, *i, *j, *piv;
            if (cnt <= 8) {
                __insertion(lo, hi, sz, cmp);
                break;
            }
            mid = lo + (cnt / 2) * sz;
            if (cmp(mid, lo) < 0) {
                __swap(mid, lo, sz);
            }
            if (cmp(hi, mid) < 0) {
                __swap(hi, mid, sz);
                if (cmp(mid, lo) < 0) {
                    __swap(mid, lo, sz);
                }
            }
            piv = hi - sz;
            __swap(mid, piv, sz); /* the median of three waits next to the end */
            i = lo;
            j = piv;
            for (;;) {
                do {
                    i += sz;
                } while (cmp(i, piv) < 0);
                do {
                    j -= sz;
                } while (cmp(j, piv) > 0);
                if (i >= j) {
                    break;
                }
                __swap(i, j, sz);
            }
            __swap(i, piv, sz);
            /* sort the smaller part now, remember the larger one */
            if (i - lo < hi - i) {
                if (sp < 64) {
                    los[sp] = i + sz;
                    his[sp] = hi;
                    sp++;
                }
                hi = i - sz;
            } else {
                if (sp < 64) {
                    los[sp] = lo;
                    his[sp] = i - sz;
                    sp++;
                }
                lo = i + sz;
            }
        }
        if (!sp) {
            break;
        }
        sp--;
        lo = los[sp];
        hi = his[sp];
    }
}

/*
 * Searches a sorted array with a binary search. Returns a pointer to a matching element, or
 * NULL if there is none.
 */
void *bsearch(const void *key, const void *base, size_t n, size_t sz, __cmp_t cmp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *p = (const char *)base + mid * sz;
        int r = cmp(key, p);
        if (r == 0) {
            return (void *)p;
        }
        if (r < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

/* Linear search for key in an array of *n elements; returns the element or NULL. */
void *lfind(const void *key, const void *base, size_t *n, size_t sz, __cmp_t cmp)
{
    size_t i;
    const char *p = base;
    for (i = 0; i < *n; i++, p += sz) {
        if (cmp(key, p) == 0) {
            return (void *)p;
        }
    }
    return NULL;
}

/* Like lfind, but appends the key to the array if it is not found. */
void *lsearch(const void *key, void *base, size_t *n, size_t sz, __cmp_t cmp)
{
    void *p = lfind(key, base, n, sz, cmp);
    if (p) {
        return p;
    }
    p = (char *)base + *n * sz;
    memcpy(p, key, sz);
    (*n)++;
    return p;
}
