/*
 * File:    prof.c
 * Purpose: prof - run-time part of profile-guided optimization: the counters of a program compiled
 *          with -Pg and the routine that writes them to the profile file when the program ends
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <_fcrt.h>

#define __PROF_SLOTS 2048
#define __PROF_KEYLEN 40

long __fc_prof_ctr[__PROF_SLOTS];                 /* one counter for every instrumented place */
char __fc_prof_keys[__PROF_SLOTS * __PROF_KEYLEN] = {1}; /* their names, filled in by the compiler */
int __fc_prof_n = 1;                              /* number of counters in use */
char __fc_prof_file[260] = {1};                   /* name of the profile file */

/* Writes the counters to the profile file, one "key count" line for each. */
static void __fc_prof_dump(void)
{
    char line[96];
    int fd, i, k, len;
    long v;
    char tmp[24];
    int nt;

    if (__fc_prof_n <= 0 || __fc_prof_n > __PROF_SLOTS) {
        return;
    }
    fd = (int)__syscall(SYS_open, __fc_prof_file, 0x241, 0644); /* O_WRONLY | O_CREAT | O_TRUNC */
    if (fd < 0) {
        return;
    }
    for (i = 0; i < __fc_prof_n; i++) {
        len = 0;
        for (k = 0; k < __PROF_KEYLEN && __fc_prof_keys[i * __PROF_KEYLEN + k]; k++) {
            line[len++] = __fc_prof_keys[i * __PROF_KEYLEN + k];
        }
        line[len++] = ' ';
        v = __fc_prof_ctr[i];
        nt = 0;
        do {
            tmp[nt++] = (char)('0' + v % 10);
            v /= 10;
        } while (v > 0);
        while (nt > 0) {
            line[len++] = tmp[--nt];
        }
        line[len++] = '\n';
        __syscall(SYS_write, fd, line, len);
    }
    __syscall(SYS_close, fd);
}

#pragma exit __fc_prof_dump 20
