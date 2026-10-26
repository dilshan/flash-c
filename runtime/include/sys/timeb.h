/*
 * File:    timeb.h
 * Purpose: The timeb structure and ftime().
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_SYS_TIMEB_H
#define _FC_SYS_TIMEB_H
#include <time.h>
struct timeb {
    long time;
    short millitm;
    short timezone;
    short dstflag;
};
void ftime(struct timeb *tp);
#endif
