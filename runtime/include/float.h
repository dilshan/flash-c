/*
 * File:    float.h
 * Purpose: Limits and characteristics of the floating-point types.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_FLOAT_H
#define _FC_FLOAT_H
#define FLT_RADIX 2
#define FLT_ROUNDS 1
#define FLT_MANT_DIG 24
#define FLT_DIG 6
#define FLT_EPSILON 1.19209290e-07F
#define FLT_MAX 3.40282347e+38F
#define FLT_MIN 1.17549435e-38F
#define FLT_MAX_EXP 128
#define FLT_MIN_EXP (-125)
#define FLT_MAX_10_EXP 38
#define FLT_MIN_10_EXP (-37)
#define DBL_MANT_DIG 53
#define DBL_DIG 15
#define DBL_EPSILON 2.2204460492503131e-16
#define DBL_MAX 1.7976931348623157e+308
#define DBL_MIN 2.2250738585072014e-308
#define DBL_MAX_EXP 1024
#define DBL_MIN_EXP (-1021)
#define DBL_MAX_10_EXP 308
#define DBL_MIN_10_EXP (-307)
#define LDBL_MANT_DIG DBL_MANT_DIG
#define LDBL_DIG DBL_DIG
#define LDBL_EPSILON DBL_EPSILON
#define LDBL_MAX DBL_MAX
#define LDBL_MIN DBL_MIN
#define LDBL_MAX_EXP DBL_MAX_EXP
#define LDBL_MIN_EXP DBL_MIN_EXP
#define LDBL_MAX_10_EXP DBL_MAX_10_EXP
#define LDBL_MIN_10_EXP DBL_MIN_10_EXP
/* floating-point control and status word (x87 layout, kept for source compatibility) */
#define MCW_EM 0x003f
#define EM_INVALID 0x0001
#define EM_DENORMAL 0x0002
#define EM_ZERODIVIDE 0x0004
#define EM_OVERFLOW 0x0008
#define EM_UNDERFLOW 0x0010
#define EM_INEXACT 0x0020
#define MCW_IC 0x1000
#define IC_AFFINE 0x1000
#define IC_PROJECTIVE 0x0000
#define MCW_RC 0x0c00
#define RC_CHOP 0x0c00
#define RC_UP 0x0800
#define RC_DOWN 0x0400
#define RC_NEAR 0x0000
#define MCW_PC 0x0300
#define PC_24 0x0000
#define PC_53 0x0200
#define PC_64 0x0300
#define SW_INVALID 0x0001
#define SW_DENORMAL 0x0002
#define SW_ZERODIVIDE 0x0004
#define SW_OVERFLOW 0x0008
#define SW_UNDERFLOW 0x0010
#define SW_INEXACT 0x0020
#define CW_DEFAULT 0x1032
unsigned int _clear87(void);
unsigned int _control87(unsigned int newcw, unsigned int mask);
void _fpreset(void);
unsigned int _status87(void);
#endif
