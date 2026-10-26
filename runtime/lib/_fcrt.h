/*
 * File:    _fcrt.h
 * Purpose: internal declarations shared by the run-time library modules (not part
 *          of the public headers)
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef __FCRT_H
#define __FCRT_H
#include <_defs.h>

long __syscall(long n, ...);          /* intrinsic: Linux system call, up to six arguments */
double __sqrt(double x);              /* intrinsic: sqrtsd */

/* x86-64 Linux system call numbers used by the library */
#define SYS_flock 73
#define SYS_fsync 74
#define SYS_lstat 6
#define SYS_fchmod 91
#define SYS_readlink 89
#define SYS_pipe 22
#define SYS_getppid 110
#define SYS_sysinfo 99
#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_poll 7
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_brk 12
#define SYS_rt_sigaction 13
#define SYS_rt_sigprocmask 14
#define SYS_ioctl 16
#define SYS_access 21
#define SYS_dup 32
#define SYS_dup2 33
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_fork 57
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
#define SYS_kill 62
#define SYS_fcntl 72
#define SYS_ftruncate 77
#define SYS_getcwd 79
#define SYS_chdir 80
#define SYS_rename 82
#define SYS_mkdir 83
#define SYS_rmdir 84
#define SYS_unlink 87
#define SYS_chmod 90
#define SYS_umask 95
#define SYS_gettimeofday 96
#define SYS_statfs 137
#define SYS_settimeofday 164
#define SYS_time 201
#define SYS_getdents64 217
#define SYS_clock_gettime 228
#define SYS_exit_group 231
#define SYS_utimensat 280

/* values shared with the start-up module (c0) */
extern int (*__RealCvtVector)(char *out, double x, int conv, int prec, int alt, int upper);
extern double (*__ScanTodVector)(const char *s, char **end);
extern void (*__abort_hook)(void);
extern void (*__stdin_prep)(void);
void __fp_not_linked(const char *who);
void __fc_init(void);                 /* written by the linker: runs the #pragma startup procedures */
void __fc_exit(void);                 /* written by the linker: runs the #pragma exit procedures */

/* errno and system call results */
extern int errno;
extern int _doserrno;
long __sysret(long r);                /* r < 0: errno = -r and returns -1 */
void __wr2(const char *s);            /* write a message to descriptor 2 */

#define __DIM(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif
