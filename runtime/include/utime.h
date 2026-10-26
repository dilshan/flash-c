/*
 * File:    utime.h
 * Purpose: Changing the access and modification times of a file.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_UTIME_H
#define _FC_UTIME_H
#include <time.h>
struct utimbuf {
    long actime;
    long modtime;
};
int utime(const char *path, struct utimbuf *times);
#endif
