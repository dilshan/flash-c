/*
 * File:    wildargs.c
 * Purpose: wildargs - optional start-up object. When it is linked, command-line
 *          arguments that contain the wildcards * ? [set] are replaced by the
 *          sorted list of matching file names (before main runs).
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <_fcrt.h>
#include <stdlib.h>

/* Matches the name s against the wildcard pattern p (* ? and [set]). */
static int __wild_match(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') {
                p++;
            }
            if (!p[1]) {
                return 1;
            }
            for (; *s; s++) {
                if (__wild_match(p + 1, s)) {
                    return 1;
                }
            }
            return __wild_match(p + 1, s);
        }
        if (!*s) {
            return 0;
        }
        if (*p == '?') {
            continue;
        }
        if (*p == '[') {
            int ok = 0, neg = 0;
            const char *q = p + 1;
            if (*q == '!' || *q == '^') {
                neg = 1;
                q++;
            }
            for (; *q && (*q != ']' || q == p + 1 + neg); q++) {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*s >= q[0] && *s <= q[2]) {
                        ok = 1;
                    }
                    q += 2;
                } else if (*q == *s) {
                    ok = 1;
                }
            }
            if (ok == neg) {
                return 0;
            }
            p = q;
            continue;
        }
        if (*p != *s) {
            return 0;
        }
    }
    return !*s;
}

/* Allocates n bytes for the expanded argument list with mmap (no malloc this early). */
static char *__wild_mem(long n)
{
    long r = __syscall(SYS_mmap, 0, n, 3, 0x22, -1L, 0);
    return r < 0 ? (char *)0 : (char *)r;
}

/* Returns the length of a string. */
static long __wild_len(const char *s)
{
    long n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

/* Replaces every argument that contains wildcards by the matching file names. */
static void __wild_expand(void)
{
    int i, j, k, n = 0, cap = 4096;
    char **vec = (char **)__wild_mem(cap * 8L);
    char *pool = __wild_mem(1L << 20), *pp = pool;
    char *dbuf = __wild_mem(32768);
    if (!vec || !pool || !dbuf) {
        return;
    }
    for (i = 0; i < _argc && n < cap - 1; i++) {
        char *a = _argv[i];
        int wild = 0, first = n;
        long len = __wild_len(a);
        if (i > 0) {
            for (j = 0; a[j]; j++) {
                if (a[j] == '*' || a[j] == '?' || a[j] == '[') {
                    wild = 1;
                }
            }
        }
        if (wild) {
            char dir[512], *pat = a, *sl = 0;
            long fd;
            for (j = 0; a[j]; j++) {
                if (a[j] == '/') {
                    sl = a + j;
                }
            }
            if (sl) {
                pat = sl + 1;
                for (j = 0; j < (int)(sl - a) && j < 510; j++) {
                    dir[j] = a[j];
                }
                if (sl == a) {
                    dir[j++] = '/';
                }
                dir[j] = 0;
            } else {
                dir[0] = '.';
                dir[1] = 0;
            }
            fd = __syscall(SYS_open, dir, 0x10000, 0);
            if (fd >= 0) {
                long got;
                while ((got = __syscall(SYS_getdents64, fd, dbuf, 32768)) > 0) {
                    long off = 0;
                    while (off < got && n < cap - 1) {
                        char *rec = dbuf + off, *nm = rec + 19;
                        int reclen = (unsigned char)rec[16] | ((unsigned char)rec[17] << 8);
                        off += reclen;
                        if (nm[0] == '.' && pat[0] != '.') {
                            continue;
                        }
                        if (nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2]))) {
                            continue;
                        }
                        if (!__wild_match(pat, nm)) {
                            continue;
                        }
                        {
                            long dl = sl ? (sl - a) + 1 : 0, nl = __wild_len(nm);
                            if (pp + dl + nl + 1 > pool + (1L << 20)) {
                                continue;
                            }
                            for (k = 0; k < dl; k++) {
                                pp[k] = a[k];
                            }
                            for (k = 0; k <= nl; k++) {
                                pp[dl + k] = nm[k];
                            }
                            vec[n++] = pp;
                            pp += dl + nl + 1;
                        }
                    }
                }
                __syscall(SYS_close, fd);
            }
            /* sort the matches of this argument */
            for (j = first + 1; j < n; j++) {
                char *v = vec[j];
                k = j;
                while (k > first) {
                    const char *x = vec[k - 1], *y = v;
                    while (*x && *x == *y) {
                        x++;
                        y++;
                    }
                    if ((unsigned char)*x <= (unsigned char)*y) {
                        break;
                    }
                    vec[k] = vec[k - 1];
                    k--;
                }
                vec[k] = v;
            }
        }
        if (n == first) { /* no wildcard, or nothing matched: keep the argument */
            if (pp + len + 1 > pool + (1L << 20)) {
                vec[n++] = a;
                continue;
            }
            for (k = 0; k <= len; k++) {
                pp[k] = a[k];
            }
            vec[n++] = pp;
            pp += len + 1;
        }
    }
    vec[n] = (char *)0;
    _argc = n;
    _argv = vec;
}

#pragma startup __wild_expand 4
