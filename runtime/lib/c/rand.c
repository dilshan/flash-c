/*
 * File:    rand.c
 * Purpose: rand - pseudo-random numbers. The generator and its constants are the
 *          ones of the reference library, so srand(n) produces the same sequence
 *          of values.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdlib.h>

static unsigned long __seed = 1;

/* Seeds the random number generator. */
void srand(unsigned int s)
{
    __seed = s;
}

/* Returns a pseudo-random number. */
int rand(void)
{
    __seed = (__seed * 22695477UL + 1) & 0xFFFFFFFFUL;
    return (int)((__seed >> 16) & 0x7FFF);
}
