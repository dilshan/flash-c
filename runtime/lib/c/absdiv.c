/*
 * File:    absdiv.c
 * Purpose: absdiv - integer absolute value and division
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdlib.h>

/* Returns the absolute value of an int. */
int abs(int v)
{
    return v < 0 ? -v : v;
}

/* Returns the absolute value of a long. */
long labs(long v)
{
    return v < 0 ? -v : v;
}

/*
 * Divides two ints and returns quotient and remainder. Returns a div_t with the members quot
 * and rem.
 */
div_t div(int num, int den)
{
    div_t r;
    r.quot = num / den;
    r.rem = num % den;
    return r;
}

/*
 * Divides two longs and returns quotient and remainder. Returns an ldiv_t with the members
 * quot and rem.
 */
ldiv_t ldiv(long num, long den)
{
    ldiv_t r;
    r.quot = num / den;
    r.rem = num % den;
    return r;
}

/* Rotates an unsigned int left by count bits. */
unsigned int _rotl(unsigned int v, int count)
{
    count &= 31;
    return count ? (v << count) | (v >> (32 - count)) : v;
}

/* Rotates an unsigned int right by count bits. */
unsigned int _rotr(unsigned int v, int count)
{
    count &= 31;
    return count ? (v >> count) | (v << (32 - count)) : v;
}

/* Rotates an unsigned long left by count bits. */
unsigned long _lrotl(unsigned long v, int count)
{
    count &= 63;
    return count ? (v << count) | (v >> (64 - count)) : v;
}

/* Rotates an unsigned long right by count bits. */
unsigned long _lrotr(unsigned long v, int count)
{
    count &= 63;
    return count ? (v >> count) | (v << (64 - count)) : v;
}
