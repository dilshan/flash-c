/*
 * File:    setjmp.h
 * Purpose: Non-local jumps: setjmp and longjmp.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_SETJMP_H
#define _FC_SETJMP_H

/* frame pointer, stack pointer and resume address of the setjmp call */
typedef struct __jmp_buf_tag {
    long bp, sp, ip, pad;
} jmp_buf[1];

int __setjmp(jmp_buf env);
void __longjmp(jmp_buf env, int val);
#define setjmp(env) __setjmp(env)
#define longjmp(env, val) __longjmp(env, val)
#endif
