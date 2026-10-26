/*
 * File:    stat.h
 * Purpose: File status information (stat, fstat) and the file mode bits.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_SYS_STAT_H
#define _FC_SYS_STAT_H
#include <sys/types.h>
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFIFO 0010000
#define S_IREAD 0400
#define S_IWRITE 0200
#define S_IEXEC 0100
struct stat {
    short st_dev, st_ino;
    short st_mode, st_nlink;
    int st_uid, st_gid;
    short st_rdev;
    long st_size;
    long st_atime, st_mtime, st_ctime;
};
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
#endif
