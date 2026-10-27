/*
 * File:    io.c
 * Purpose: io - low-level (descriptor) file access, file status and attributes
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <io.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>
#include <_fcrt.h>

/*
 * Opens a file with the O_* flags (a mode argument follows O_CREAT); returns a descriptor or -1.
 */
int open(const char *path, int oflag, ...)
{
    va_list ap;
    int mode = 0666;
    long r;
    va_start(ap, oflag);
    if (oflag & O_CREAT) {
        mode = va_arg(ap, int);
    }
    if (mode & 0xFF00) {
        mode &= 0777;
    }
    r = __syscall(SYS_open, path, oflag & ~(O_BINARY | O_TEXT), mode);
    return (int)__sysret(r);
}

/* Opens an existing file. */
int _open(const char *path, int oflag)
{
    return open(path, oflag);
}

/* Opens a file with sharing flags (ignored on this system). */
int sopen(const char *path, int oflag, int shflag, ...)
{
    va_list ap;
    int mode = 0666;
    va_start(ap, shflag);
    if (oflag & O_CREAT) {
        mode = va_arg(ap, int);
    }
    return open(path, oflag, mode);
}

/* Creates or truncates a file for writing. */
int creat(const char *path, int mode)
{
    if (mode & 0xFF00) {
        mode &= 0777;
    }
    return (int)__sysret(
        __syscall(SYS_open, path, O_WRONLY | O_CREAT | O_TRUNC, mode ? mode : 0666));
}

/* Creates or truncates a file; the DOS attribute decides if it is read-only. */
int _creat(const char *path, int attrib)
{
    return creat(path, 0666);
}

/* Creates a new file; fails if it exists. */
int creatnew(const char *path, int mode)
{
    return (int)__sysret(__syscall(SYS_open, path, O_RDWR | O_CREAT | O_EXCL, 0666));
}

/* Creates a unique new file: a unique name is appended to the directory path (which must end in
 * a slash). */
int creattemp(char *path, int attrib)
{
    char *p = path + strlen(path);
    int fd;
    strcat(path, "XXXXXX");
    if (!mktemp(path)) {
        *p = 0;
        return -1;
    }
    fd = creatnew(path, 0);
    return fd;
}

/* Closes a file descriptor. */
int close(int fd)
{
    return (int)__sysret(__syscall(SYS_close, fd));
}

/* Closes a file descriptor. */
int _close(int fd)
{
    return close(fd);
}

/* Reads up to len bytes; returns the number read, 0 at end of file or -1. */
int read(int fd, void *buf, unsigned len)
{
    return (int)__sysret(__syscall(SYS_read, fd, buf, len));
}

/* Reads up to len bytes; returns the number read, 0 at end of file or -1. */
int _read(int fd, void *buf, unsigned len)
{
    return read(fd, buf, len);
}

/* Writes len bytes; returns the number written or -1. */
int write(int fd, const void *buf, unsigned len)
{
    return (int)__sysret(__syscall(SYS_write, fd, buf, len));
}

/* Writes len bytes; returns the number written or -1. */
int _write(int fd, const void *buf, unsigned len)
{
    return write(fd, buf, len);
}

/* Moves the file position; returns the new position or -1. */
long lseek(int fd, long off, int whence)
{
    return __sysret(__syscall(SYS_lseek, fd, off, whence));
}

/* Returns the current file position. */
long tell(int fd)
{
    return lseek(fd, 0L, 1);
}

/* Returns the length of an open file. */
long filelength(int fd)
{
    long cur = lseek(fd, 0L, 1), end;
    if (cur < 0) {
        return -1;
    }
    end = lseek(fd, 0L, 2);
    lseek(fd, cur, 0);
    return end;
}

/* Returns 1 at end of file, 0 if more data follows, -1 on error. */
int eof(int fd)
{
    long cur = lseek(fd, 0L, 1), end;
    if (cur < 0) {
        return -1;
    }
    end = filelength(fd);
    return cur >= end;
}

/* Changes the length of an open file. */
int chsize(int fd, long size)
{
    long cur = filelength(fd);
    if (cur < 0) {
        return -1;
    }
    if (size <= cur) {
        return (int)__sysret(__syscall(SYS_ftruncate, fd, size));
    }
    { /* extending: zero-fill, like the DOS call */
        char z[512];
        long pos = lseek(fd, 0L, 1);
        memset(z, 0, sizeof z);
        lseek(fd, 0L, 2);
        while (cur < size) {
            long n = size - cur > 512 ? 512 : size - cur;
            if (write(fd, z, (unsigned)n) != n) {
                return -1;
            }
            cur += n;
        }
        lseek(fd, pos, 0);
    }
    return 0;
}

/* Duplicates a file descriptor. */
int dup(int fd)
{
    return (int)__sysret(__syscall(SYS_dup, fd));
}

/* Duplicates a descriptor onto a given number. */
int dup2(int fd, int fd2)
{
    return (int)__sysret(__syscall(SYS_dup2, fd, fd2)) < 0 ? -1 : 0;
}

/* Returns true if the descriptor refers to a terminal. */
int isatty(int fd)
{
    char tio[64];
    return __syscall(SYS_ioctl, fd, 0x5401, tio) >= 0;
}

/* Device control request on a descriptor. */
int ioctl(int fd, int cmd, ...)
{
    va_list ap;
    void *arg;
    va_start(ap, cmd);
    arg = va_arg(ap, void *);
    return (int)__sysret(__syscall(SYS_ioctl, fd, cmd, arg));
}

/* Tests whether a file exists or can be read, written or executed. */
int access(const char *path, int amode)
{
    return (int)__sysret(__syscall(SYS_access, path, amode & 7));
}

/* Changes the permissions of a file. */
int chmod(const char *path, int amode)
{
    struct stat st;
    int mode;
    if (stat(path, &st) < 0) {
        return -1;
    }
    mode = st.st_mode & 0777;
    if (amode & S_IREAD) {
        mode |= 0444;
    }
    if (amode & S_IWRITE) {
        mode |= 0222;
    } else if (amode & S_IREAD) {
        mode &= ~0222;
    }
    return (int)__sysret(__syscall(SYS_chmod, path, mode));
}

/* Returns the DOS attribute word of a file: 1 if it is read-only, else 0.  func is ignored. */
int _chmod(const char *path, int func, ...)
{
    struct stat st;
    if (stat(path, &st) < 0) {
        return -1;
    }
    return (st.st_mode & 0222) ? 0 : 1; /* attribute word: 1 = read only */
}

/* Sets the file creation mask and returns the old one. */
int umask(int mask)
{
    return (int)__syscall(SYS_umask, mask);
}

/* There is no text translation: always returns O_BINARY. */
int setmode(int fd, int mode)
{
    return O_BINARY;
}

/* the descriptor does not use text translation, so locking is advisory only */
int lock(int fd, long offset, long length)
{
    return 0;
}

/* Releases a lock obtained with locking(); a no-op here. */
int unlock(int fd, long offset, long length)
{
    return 0;
}

/* Locks or unlocks a region of a file; accepted but has no effect. */
int locking(int fd, int mode, long length)
{
    return 0;
}

/* convert the kernel's struct stat (144 bytes) to the library's */
static void __cvt_stat(long *k, struct stat *s)
{
    int *w = (int *)k;
    s->st_dev = (short)k[0];
    s->st_ino = (short)k[1];
    s->st_nlink = (short)k[2];
    s->st_mode = (short)(w[6]);
    s->st_uid = w[7];
    s->st_gid = w[8];
    s->st_rdev = (short)k[5];
    s->st_size = k[6];
    s->st_atime = k[9];
    s->st_mtime = k[11];
    s->st_ctime = k[13];
}

/* Gets information about the file at path. */
int stat(const char *path, struct stat *buf)
{
    long k[24];
    if (__sysret(__syscall(SYS_stat, path, k)) < 0) {
        return -1;
    }
    __cvt_stat(k, buf);
    return 0;
}

/* Gets information about an open file. */
int fstat(int fd, struct stat *buf)
{
    long k[24];
    if (__sysret(__syscall(SYS_fstat, fd, k)) < 0) {
        return -1;
    }
    __cvt_stat(k, buf);
    return 0;
}

/* Sets the access and modification times of a file. */
int utime(const char *path, struct utimbuf *times)
{
    long ts[4];
    if (times) {
        ts[0] = times->actime;
        ts[1] = 0;
        ts[2] = times->modtime;
        ts[3] = 0;
    } else {
        ts[0] = 0;
        ts[1] = (1L << 30) - 1;
        ts[2] = 0;
        ts[3] = (1L << 30) - 1;
    } /* UTIME_NOW */
    return (int)__sysret(__syscall(SYS_utimensat, -100, path, ts, 0));
}

/* file time as DOS fields */
int getftime(int fd, struct ftime *ft)
{
    struct stat st;
    struct tm *tm;
    time_t t;
    if (fstat(fd, &st) < 0) {
        return -1;
    }
    t = st.st_mtime;
    tm = localtime(&t);
    ft->ft_tsec = tm->tm_sec / 2;
    ft->ft_min = tm->tm_min;
    ft->ft_hour = tm->tm_hour;
    ft->ft_day = tm->tm_mday;
    ft->ft_month = tm->tm_mon + 1;
    ft->ft_year = tm->tm_year - 80;
    return 0;
}

/* Sets the date and time of an open file. */
int setftime(int fd, struct ftime *ft)
{
    struct tm tm;
    long ts[4];
    memset(&tm, 0, sizeof tm);
    tm.tm_sec = ft->ft_tsec * 2;
    tm.tm_min = ft->ft_min;
    tm.tm_hour = ft->ft_hour;
    tm.tm_mday = ft->ft_day;
    tm.tm_mon = ft->ft_month - 1;
    tm.tm_year = ft->ft_year + 80;
    tm.tm_isdst = -1;
    ts[0] = 0;
    ts[1] = (1L << 30) - 2;
    ts[2] = mktime(&tm);
    ts[3] = 0; /* UTIME_OMIT for the access time */
    return (int)__sysret(__syscall(SYS_utimensat, fd, 0, ts, 0)) < 0 ? -1 : 0;
}
