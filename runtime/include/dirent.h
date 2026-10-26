/*
 * File:    dirent.h
 * Purpose: Directory stream access (opendir, readdir, closedir).
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_DIRENT_H
#define _FC_DIRENT_H
#include <_defs.h>
#define NAME_MAX 255
struct dirent {
    unsigned short d_reclen;
    char d_name[256];
};
typedef struct __dirstream {
    int fd, pos, len;
    char buf[2048];
    struct dirent ent;
} DIR;
DIR *opendir(const char *name);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);
#endif
