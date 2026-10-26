/*
 * File:    direct.h
 * Purpose: Directory functions (compatibility header, see dir.h).
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_DIRECT_H
#define _FC_DIRECT_H
#include <dir.h>
int _chdrive(int drive);
char *_getdcwd(int drive, char *buf, int n);
int _getdrive(void);
#endif
