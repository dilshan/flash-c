/*
 * File:    errno.c
 * Purpose: errno - error numbers, messages and the system-call result helpers
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <errno.h>
#include <string.h>
#include <_fcrt.h>

int errno;
int _doserrno;

char *sys_errlist[] = {"Error 0",
                       "Operation not permitted",
                       "No such file or directory",
                       "No such process",
                       "Interrupted system call",
                       "Input/output error",
                       "No such device or address",
                       "Argument list too long",
                       "Exec format error",
                       "Bad file descriptor",
                       "No child processes",
                       "Resource temporarily unavailable",
                       "Not enough memory",
                       "Permission denied",
                       "Bad address",
                       "Block device required",
                       "Device or resource busy",
                       "File exists",
                       "Cross-device link",
                       "No such device",
                       "Not a directory",
                       "Is a directory",
                       "Invalid argument",
                       "Too many open files in system",
                       "Too many open files",
                       "Inappropriate ioctl for device",
                       "Text file busy",
                       "File too large",
                       "No space left on device",
                       "Illegal seek",
                       "Read-only file system",
                       "Too many links",
                       "Broken pipe",
                       "Math argument out of domain of function",
                       "Result too large",
                       "Resource deadlock avoided",
                       "File name too long",
                       "No locks available",
                       "Function not implemented",
                       "Directory not empty",
                       "Too many levels of symbolic links"};
int sys_nerr = 41;

/* Converts the result of a system call: a negative error code sets errno and gives -1. */
long __sysret(long r)
{
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        _doserrno = errno;
        return -1;
    }
    return r;
}

/* Returns the text of an error number. */
char *strerror(int e)
{
    static char buf[40];
    if (e >= 0 && e < sys_nerr) {
        return sys_errlist[e];
    }
    strcpy(buf, "Unknown error ");
    {
        char d[12];
        int i = 0, n = e < 0 ? -e : e;
        if (!n) {
            d[i++] = '0';
        }
        while (n) {
            d[i++] = '0' + n % 10;
            n /= 10;
        }
        {
            char *p = buf + 14;
            if (e < 0) {
                *p++ = '-';
            }
            while (i) {
                *p++ = d[--i];
            }
            *p = 0;
        }
    }
    return buf;
}

/* Builds "s: message" for the current errno in a static buffer. */
char *_strerror(const char *s)
{
    static char buf[160];
    buf[0] = 0;
    if (s && *s) {
        strncat(buf, s, 100);
        strcat(buf, ": ");
    }
    strncat(buf, strerror(errno), 50);
    strcat(buf, "\n");
    return buf;
}
