/*
 * File:    dir.h
 * Purpose: Directory handling, wildcard searches and path name functions.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_DIR_H
#define _FC_DIR_H
#include <_defs.h>
#define MAXPATH 260
#define MAXDRIVE 3
#define MAXDIR 256
#define MAXFILE 256
#define MAXEXT 256
#define WILDCARDS 0x01
#define EXTENSION 0x02
#define FILENAME 0x04
#define DIRECTORY 0x08
#define DRIVE 0x10
#define FA_RDONLY 0x01
#define FA_HIDDEN 0x02
#define FA_SYSTEM 0x04
#define FA_LABEL 0x08
#define FA_DIREC 0x10
#define FA_ARCH 0x20
struct ffblk {
    char ff_reserved[64];
    char ff_attrib;
    int ff_ftime;
    int ff_fdate;
    long ff_fsize;
    char ff_name[256];
};
int chdir(const char *path);
int findfirst(const char *path, struct ffblk *ff, int attrib);
int findnext(struct ffblk *ff);
void fnmerge(char *path, const char *drive, const char *dir, const char *name, const char *ext);
int fnsplit(const char *path, char *drive, char *dir, char *name, char *ext);
int getcurdir(int drive, char *dir);
char *getcwd(char *buf, int n);
int getdisk(void);
int mkdir(const char *path);
char *mktemp(char *templ);
int rmdir(const char *path);
char *searchpath(const char *file);
int setdisk(int drive);
#endif
