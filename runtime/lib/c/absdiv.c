/*
 * File:    absdiv.c
 * Purpose: absdiv - integer absolute value and division
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdlib.h>

int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

div_t div(int num, int den)
{
    div_t r;
    r.quot = num / den;
    r.rem = num % den;
    return r;
}

ldiv_t ldiv(long num, long den)
{
    ldiv_t r;
    r.quot = num / den;
    r.rem = num % den;
    return r;
}

unsigned int _rotl(unsigned int v, int count)
{
    count &= 31;
    return count ? (v << count) | (v >> (32 - count)) : v;
}

unsigned int _rotr(unsigned int v, int count)
{
    count &= 31;
    return count ? (v >> count) | (v << (32 - count)) : v;
}

unsigned long _lrotl(unsigned long v, int count)
{
    count &= 63;
    return count ? (v << count) | (v >> (64 - count)) : v;
}

unsigned long _lrotr(unsigned long v, int count)
{
    count &= 63;
    return count ? (v >> count) | (v << (64 - count)) : v;
}
