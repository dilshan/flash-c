/*
 * File:    process.h
 * Purpose: Running other programs: system, spawn and exec families.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_PROCESS_H
#define _FC_PROCESS_H
#include <_defs.h>
#define P_WAIT 0
#define P_NOWAIT 1
#define P_OVERLAY 2
#define _P_WAIT P_WAIT
#define _P_NOWAIT P_NOWAIT
#define _P_OVERLAY P_OVERLAY
void abort(void);
void _cexit(void);
void _c_exit(void);
int execl(const char *path, const char *arg0, ...);
int execle(const char *path, const char *arg0, ...);
int execlp(const char *path, const char *arg0, ...);
int execlpe(const char *path, const char *arg0, ...);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);
int execvp(const char *path, char *const argv[]);
int execvpe(const char *path, char *const argv[], char *const envp[]);
void exit(int code);
void _exit(int code);
int getpid(void);
int spawnl(int mode, const char *path, const char *arg0, ...);
int spawnle(int mode, const char *path, const char *arg0, ...);
int spawnlp(int mode, const char *path, const char *arg0, ...);
int spawnlpe(int mode, const char *path, const char *arg0, ...);
int spawnv(int mode, const char *path, char *const argv[]);
int spawnve(int mode, const char *path, char *const argv[], char *const envp[]);
int spawnvp(int mode, const char *path, char *const argv[]);
int spawnvpe(int mode, const char *path, char *const argv[], char *const envp[]);
int system(const char *cmd);
#endif
