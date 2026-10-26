/*
 * File:    fcntl.h
 * Purpose: File access mode and creation flags for open().
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_FCNTL_H
#define _FC_FCNTL_H
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_ACCMODE 0x0003
#define O_CREAT 0x0040
#define O_EXCL 0x0080
#define O_TRUNC 0x0200
#define O_APPEND 0x0400
#define O_NONBLOCK 0x0800
#define O_BINARY 0
#define O_TEXT 0
#define O_NOINHERIT 0
#define O_DENYALL 0
#define O_DENYWRITE 0
#define O_DENYREAD 0
#define O_DENYNONE 0
#define S_IREAD 0400
#define S_IWRITE 0200
#define S_IEXEC 0100
#endif
