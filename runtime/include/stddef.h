/*
 * File:    stddef.h
 * Purpose: Common definitions: size_t, ptrdiff_t, NULL and offsetof.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_STDDEF_H
#define _FC_STDDEF_H
#include <_defs.h>
#define offsetof(type, member) ((size_t)&(((type *)0)->member))
#endif
