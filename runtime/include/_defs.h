/*
 * File:    _defs.h
 * Purpose: common types shared by the library headers
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef __FC_DEFS_H
#define __FC_DEFS_H
#ifndef _FC_SIZE_T
#define _FC_SIZE_T
typedef unsigned long size_t;
#endif
#ifndef _FC_PTRDIFF_T
#define _FC_PTRDIFF_T
typedef long ptrdiff_t;
#endif
#ifndef _FC_WCHAR_T
#define _FC_WCHAR_T
typedef int wchar_t;
#endif
#ifndef _FC_VA_LIST
#define _FC_VA_LIST
typedef char *va_list;
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
/* the DOS-era keywords are accepted by the compiler and ignored */
#endif
