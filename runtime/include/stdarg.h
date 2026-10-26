/*
 * File:    stdarg.h
 * Purpose: Variable argument lists: va_start, va_arg, va_end.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_STDARG_H
#define _FC_STDARG_H
#include <_defs.h>
/* every argument occupies one 8-byte slot (see the calling convention) */
#define va_start(ap, last) ((ap) = (char *)&(last) + 8)
#define va_arg(ap, type) (*(type *)(((ap) += 8) - 8))
#define va_end(ap) ((void)0)
#define va_copy(dst, src) ((dst) = (src))
#endif
