/*
 * File:    bios.h
 * Purpose: The few BIOS-style services that make sense on a terminal (keyboard,
 *          equipment, time).
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_BIOS_H
#define _FC_BIOS_H
/* the few BIOS services that make sense on a terminal */
long biostime(int cmd, long newtime);
int bioskey(int cmd);
int biosequip(void);
int biosmemory(void);
#endif
