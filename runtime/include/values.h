/*
 * File:    values.h
 * Purpose: Limits of the basic types in the older <values.h> style.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_VALUES_H
#define _FC_VALUES_H
#define MAXSHORT 0x7fff
#define MAXINT 0x7fffffff
#define MAXLONG 0x7fffffffffffffffL
#define BITSPERBYTE 8
#define BITS(type) (BITSPERBYTE * (int)sizeof(type))
#define MAXDOUBLE 1.7976931348623157e+308
#define MAXFLOAT 3.4028234663852886e+38
#define MINDOUBLE 2.2250738585072014e-308
#define MINFLOAT 1.1754943508222875e-38
#define DMAXEXP 1024
#define FMAXEXP 128
#define DMINEXP (-1021)
#define FMINEXP (-125)
#define DSIGNIF 53
#define FSIGNIF 24
#define LN_MAXDOUBLE 7.0978271289338397e+02
#define LN_MINDOUBLE (-7.0839641853226408e+02)
#define HIBITS 0x8000
#define HIBITL 0x8000000000000000L
#endif
