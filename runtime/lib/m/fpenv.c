/*
 * File:    fpenv.c
 * Purpose: fpenv - floating-point control: status and control words as seen
 *          through the SSE control register
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <float.h>


static unsigned int __fp_cw = 0x037F;           /* default control word: all exceptions masked, 53-bit precision */
static unsigned int __fp_sw;

unsigned int _status87(void) { return __fp_sw; }
unsigned int _clear87(void) { unsigned int s = __fp_sw; __fp_sw = 0; return s; }

unsigned int _control87(unsigned int newcw, unsigned int mask)
{
    __fp_cw = (__fp_cw & ~mask) | (newcw & mask);
    return __fp_cw;
}

void _fpreset(void) { __fp_cw = 0x037F; __fp_sw = 0; }
