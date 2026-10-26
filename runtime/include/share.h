/*
 * File:    share.h
 * Purpose: File sharing mode constants for sopen().
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_SHARE_H
#define _FC_SHARE_H
#define SH_COMPAT 0x0000
#define SH_DENYRW 0x0010
#define SH_DENYWR 0x0020
#define SH_DENYRD 0x0030
#define SH_DENYNO 0x0040
#endif
