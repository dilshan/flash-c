/*
 * File:    types.h
 * Purpose: Basic system types (time_t, off_t and similar).
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_SYS_TYPES_H
#define _FC_SYS_TYPES_H
#include <time.h>
typedef short dev_t;
typedef short ino_t;
typedef short mode_t;
typedef short nlink_t;
typedef int uid_t;
typedef int gid_t;
typedef long off_t;
typedef int pid_t;
#endif
