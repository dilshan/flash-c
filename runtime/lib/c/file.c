/*
 * File:    file.c
 * Purpose: file - stream management: opening, closing, buffering, positioning,
 *          temporary files
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <_fcrt.h>
#include <_fcio.h>

int _fmode;

FILE _streams[_NFILE] = {
    {0, _F_READ | _F_USED}, {1, _F_WRITE | _F_USED}, {2, _F_WRITE | _F_RAW | _F_USED}};

/* Returns true if the descriptor refers to a terminal. */
int __fl_isatty(int fd)
{
    char t[64];
    return __syscall(SYS_ioctl, fd, 0x5401, t) == 0;
}

/* Writes out the buffered output of a stream; returns 0 or EOF. */
int __fl_flush(FILE *f)
{
    int r = 0;
    long done = 0, n;
    if (f->wlen) {
        while (done < f->wlen) {
            n = __syscall(SYS_write, f->fd, f->wbuf + done, f->wlen - done);
            if (n <= 0) {
                f->err = 1;
                __sysret(n);
                r = -1;
                break;
            }
            done += n;
        }
        f->wlen = 0;
    }
    return r;
}

/* Makes sure the stream has a write buffer; returns 0 if none could be allocated. */
int __fl_wbuf(FILE *f)
{
    if (f->wbuf) {
        return 1;
    }
    f->wbuf = malloc(__BUFSZ);
    if (!f->wbuf) {
        return 0;
    }
    if (!f->mode && !(f->flags & _F_RAW) && __fl_isatty(f->fd)) {
        f->flags |= _F_LINE; /* terminals are line buffered */
    }
    return 1;
}

/* Refills the read buffer of a stream; returns 0 at end of file or on error. */
int __fl_fill(FILE *f)
{
    long r;
    if (f->eof) {
        return 0;
    }
    if (f->fd == 0 && __stdin_prep) {
        __stdin_prep();
    }
    if (f->fd == 0) {
        fflush(stdout); /* prompts appear before the program waits for input */
    }
    if (f->wlen) {
        __fl_flush(f);
    }
    if (!f->rbuf) {
        f->rbuf = malloc(__BUFSZ);
        if (!f->rbuf) {
            f->err = 1;
            return 0;
        }
    }
    r = __syscall(SYS_read, f->fd, f->rbuf, (f->flags & _F_RAW) ? 1 : __BUFSZ);
    if (r <= 0) {
        f->eof = 1;
        if (r < 0) {
            f->err = 1;
            __sysret(r);
        }
        return 0;
    }
    f->rpos = 0;
    f->rlen = (int)r;
    return 1;
}

/* Writes buffered output of a stream to the file. Returns 0 on success, EOF on a write error. */
int fflush(FILE *f)
{
    int i, r = 0;
    if (!f) {
        for (i = 0; i < _NFILE; i++) {
            if ((_streams[i].flags & _F_USED) && _streams[i].wlen && __fl_flush(&_streams[i])) {
                r = EOF;
            }
        }
        return r;
    }
    if (!(f->flags & _F_USED)) {
        errno = EBADF;
        return EOF;
    }
    r = __fl_flush(f);
    if (f->rlen > f->rpos && (f->flags & _F_READ) && f != stdin) {
        __syscall(SYS_lseek, f->fd, -(long)(f->rlen - f->rpos), 1);
    }
    if (f != stdin || 1) {
        f->rpos = f->rlen = 0;
    }
    return r ? EOF : 0;
}

/* Flushes all open output streams; returns the number of open streams. */
int flushall(void)
{
    int i, n = 0;
    for (i = 0; i < _NFILE; i++) {
        if (_streams[i].flags & _F_USED) {
            n++;
            if (_streams[i].wlen) {
                __fl_flush(&_streams[i]);
            }
        }
    }
    return n;
}

/* Flushes all streams when the program ends. */
static void __stdio_exit(void)
{
    int i;
    for (i = 0; i < _NFILE; i++) {
        if ((_streams[i].flags & _F_USED) && _streams[i].wlen) {
            __fl_flush(&_streams[i]);
        }
    }
}

#pragma exit __stdio_exit 2

/* Returns a free entry of the stream table, or NULL. */
static FILE *__fl_slot(void)
{
    int i;
    for (i = 5; i < _NFILE; i++) {
        if (!(_streams[i].flags & _F_USED)) {
            return &_streams[i];
        }
    }
    errno = EMFILE;
    return NULL;
}

/* Initialises a stream for the descriptor fd with the given flags. */
static void __fl_init(FILE *f, int fd, int flags)
{
    f->fd = fd;
    f->flags = flags | _F_USED;
    f->rpos = f->rlen = f->wlen = f->eof = f->err = f->unget = f->mode = f->tmp = 0;
    f->rbuf = f->wbuf = NULL;
    f->name = NULL;
}

/* translate an fopen mode string; returns the open() flags or -1 */
static int __fl_mode(const char *m, int *rd, int *wr)
{
    int i, fl;
    *rd = *wr = 0;
    if (m[0] == 'r') {
        *rd = 1;
        fl = 0;
    } else if (m[0] == 'w') {
        *wr = 1;
        fl = 0x241;
    } else if (m[0] == 'a') {
        *wr = 1;
        fl = 0x441;
    } else {
        errno = EINVAL;
        return -1;
    }
    for (i = 1; m[i]; i++) {
        if (m[i] == '+') {
            *rd = *wr = 1;
            fl = (m[0] == 'r') ? 2 : (m[0] == 'w' ? 0x242 : 0x442);
        }
    }
    return fl;
}

/*
 * Opens a file and returns a stream. Returns a stream pointer, or NULL if the file could not
 * be opened (errno is set).
 */
FILE *fopen(const char *name, const char *mode)
{
    int rd, wr, fl, fd;
    FILE *f;
    fl = __fl_mode(mode, &rd, &wr);
    if (fl < 0) {
        return NULL;
    }
    f = __fl_slot();
    if (!f) {
        return NULL;
    }
    fd = (int)__syscall(SYS_open, name, fl, 0666);
    if (fd < 0) {
        __sysret(fd);
        return NULL;
    }
    __fl_init(f, fd, (rd ? _F_READ : 0) | (wr ? _F_WRITE : 0));
    return f;
}

/* Opens a file with sharing flags; the flags have no meaning on this system. */
FILE *_fsopen(const char *name, const char *mode, int shflg)
{
    return fopen(name, mode);
}

/* Associates a stream with an open file descriptor. */
FILE *fdopen(int fd, const char *mode)
{
    int rd, wr;
    FILE *f;
    if (__fl_mode(mode, &rd, &wr) < 0) {
        return NULL;
    }
    if (fd < 0) {
        errno = EBADF;
        return NULL;
    }
    f = __fl_slot();
    if (!f) {
        return NULL;
    }
    __fl_init(f, fd, (rd ? _F_READ : 0) | (wr ? _F_WRITE : 0));
    return f;
}

/* Closes a stream. Returns 0 on success, EOF if an error occurred. */
int fclose(FILE *f)
{
    int r;
    if (!f || !(f->flags & _F_USED)) {
        errno = EBADF;
        return EOF;
    }
    r = __fl_flush(f) ? EOF : 0;
    if (f->fd > 2) {
        if (__syscall(SYS_close, f->fd) < 0) {
            r = EOF;
        }
    }
    free(f->rbuf);
    free(f->wbuf);
    if (f->name) {
        free(f->name);
    }
    f->rbuf = f->wbuf = NULL;
    f->name = NULL;
    f->flags = 0;
    f->wlen = f->rlen = f->rpos = 0;
    return r;
}

/* Closes all open streams except the standard ones; returns how many were closed. */
int fcloseall(void)
{
    int i, n = 0;
    for (i = 3; i < _NFILE; i++) {
        if (_streams[i].flags & _F_USED) {
            fclose(&_streams[i]);
            n++;
        }
    }
    return n;
}

/* Reopens a stream on another file. Returns stream, or NULL if the file could not be opened. */
FILE *freopen(const char *name, const char *mode, FILE *f)
{
    int rd, wr, fl, nfd;
    int old = f->fd;
    if (!name) {
        return f;
    }
    fl = __fl_mode(mode, &rd, &wr);
    if (fl < 0) {
        return NULL;
    }
    __fl_flush(f);
    nfd = (int)__syscall(SYS_open, name, fl, 0666);
    if (nfd < 0) {
        __sysret(nfd);
        fclose(f);
        return NULL;
    }
    if (old <= 2) {
        __syscall(SYS_dup2, nfd, old);
        __syscall(SYS_close, nfd);
        nfd = old;
    } else {
        __syscall(SYS_close, old);
    }
    free(f->rbuf);
    free(f->wbuf);
    __fl_init(f, nfd, (rd ? _F_READ : 0) | (wr ? _F_WRITE : 0));
    return f;
}

/* Sets the buffering mode and buffer of a stream. Returns 0 on success, nonzero otherwise. */
int setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    if (mode < _IOFBF || mode > _IONBF) {
        return -1;
    }
    f->mode = mode + 1;
    f->flags &= ~(_F_LINE | _F_RAW);
    if (mode == _IOLBF) {
        f->flags |= _F_LINE;
    }
    if (mode == _IONBF) {
        f->flags |= _F_RAW;
    }
    return 0;
}

/* Sets the buffer of a stream. */
void setbuf(FILE *f, char *buf)
{
    if (buf) {
        setvbuf(f, buf, _IOFBF, BUFSIZ);
    } else {
        setvbuf(f, NULL, _IONBF, 0);
    }
}

/* Returns the file descriptor of a stream. */
int fileno(FILE *f)
{
    return f->fd;
}

/*
 * Tests the end-of-file indicator of a stream. Returns nonzero if the end-of-file indicator is
 * set, otherwise 0.
 */
int feof(FILE *f)
{
    return f->eof && f->rpos >= f->rlen && !f->unget;
}

/*
 * Tests the error indicator of a stream. Returns nonzero if an error occurred on the stream,
 * otherwise 0.
 */
int ferror(FILE *f)
{
    return f->err;
}

/* Clears the end-of-file and error indicators of a stream. */
void clearerr(FILE *f)
{
    f->err = 0;
    f->eof = 0;
}

/* Moves the file position of a stream. Returns 0 on success, nonzero on error. */
int fseek(FILE *f, long off, int whence)
{
    if (__fl_flush(f)) {
        return -1;
    }
    if (whence == 1) {
        off -= (f->rlen - f->rpos);
        if (f->unget) {
            off--;
        }
    }
    f->rpos = f->rlen = 0;
    f->eof = 0;
    f->unget = 0;
    return __sysret(__syscall(SYS_lseek, f->fd, off, whence)) < 0 ? -1 : 0;
}

/* Returns the current file position of a stream. */
long ftell(FILE *f)
{
    long p = __syscall(SYS_lseek, f->fd, 0, 1);
    if (p < 0) {
        __sysret(p);
        return -1;
    }
    return p - (f->rlen - f->rpos) - (f->unget ? 1 : 0) + f->wlen;
}

/* Sets the file position to the start of a stream. */
void rewind(FILE *f)
{
    fseek(f, 0, 0);
    f->err = 0;
}

/*
 * Stores the current file position of a stream. Returns 0 on success, nonzero on error (errno
 * is set).
 */
int fgetpos(FILE *f, fpos_t *pos)
{
    long p = ftell(f);
    if (p < 0) {
        return -1;
    }
    *pos = p;
    return 0;
}

/* Restores a file position saved by fgetpos. Returns 0 on success, nonzero on error. */
int fsetpos(FILE *f, const fpos_t *pos)
{
    return fseek(f, *pos, 0);
}

/* Deletes a file. Returns 0 on success, nonzero on error. */
int remove(const char *name)
{
    long r = __syscall(SYS_unlink, name);
    if (r == -21 || r == -1) {
        r = __syscall(SYS_rmdir, name); /* a directory: remove it when empty */
    }
    return __sysret(r) < 0 ? -1 : 0;
}

/* Deletes a file. Returns 0 on success, -1 on error (errno is set). */
int unlink(const char *name)
{
    return __sysret(__syscall(SYS_unlink, name)) < 0 ? -1 : 0;
}

/* Renames a file. Returns 0 on success, nonzero on error. */
int rename(const char *a, const char *b)
{
    return __sysret(__syscall(SYS_rename, a, b)) < 0 ? -1 : 0;
}

/* Writes an error message for errno to stderr. */
void perror(const char *msg)
{
    if (msg && *msg) {
        fputs(msg, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

/* ----- temporary files ----- */
static int __tmpseq;

/* Writes the decimal digits of v at p, padded with zeros to at least minlen digits. */
static void __utoa(unsigned long v, char *p, int minlen)
{
    char t[24];
    int i = 0, n;
    do {
        t[i++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (i < minlen) {
        t[i++] = '0';
    }
    for (n = 0; i;) {
        p[n++] = t[--i];
    }
    p[n] = 0;
}

/* Builds a name for a temporary file that does not exist yet. */
static char *__tmpname(char *buf, const char *dir, const char *prefix)
{
    int tries;
    for (tries = 0; tries < 10000; tries++) {
        char num[24];
        char *p = buf;
        __tmpseq++;
        strcpy(p, dir);
        p += strlen(p);
        if (p > buf && p[-1] != '/') {
            *p++ = '/';
        }
        strcpy(p, prefix);
        p += strlen(p);
        __utoa((unsigned long)__syscall(SYS_getpid) * 1000UL + __tmpseq % 1000, num, 1);
        strcpy(p, num);
        if (__syscall(SYS_access, buf, 0) < 0) {
            return buf;
        }
    }
    return NULL;
}

/* Generates a file name that is not in use. Returns the name, or NULL on failure. */
char *tmpnam(char *buf)
{
    static char sbuf[L_tmpnam];
    return __tmpname(buf ? buf : sbuf, P_tmpdir, "fc");
}

/* Returns a malloc'd unique temporary file name in dir (or $TMPDIR, or /tmp). */
char *tempnam(const char *dir, const char *prefix)
{
    char tmp[300];
    char *r;
    if (!dir) {
        dir = getenv("TMPDIR");
    }
    if (!dir) {
        dir = P_tmpdir;
    }
    if (!__tmpname(tmp, dir, prefix ? prefix : "fc")) {
        return NULL;
    }
    r = malloc(strlen(tmp) + 1);
    if (r) {
        strcpy(r, tmp);
    }
    return r;
}

/* Creates a temporary file. Returns a stream, or NULL if the file could not be created. */
FILE *tmpfile(void)
{
    char nm[L_tmpnam + 8];
    FILE *f;
    if (!tmpnam(nm)) {
        return NULL;
    }
    f = fopen(nm, "w+");
    if (!f) {
        return NULL;
    }
    __syscall(SYS_unlink, nm); /* the file disappears when it is closed */
    f->tmp = 1;
    return f;
}

/* Closes and removes the temporary files created by tmpfile(); returns how many. */
int rmtmp(void)
{
    int i, n = 0;
    for (i = 5; i < _NFILE; i++) {
        if ((_streams[i].flags & _F_USED) && _streams[i].tmp) {
            fclose(&_streams[i]);
            n++;
        }
    }
    return n;
}

/* Replaces the XXXXXX at the end of the template by a unique string. */
char *mktemp(char *templ)
{
    int n = (int)strlen(templ), i, tries;
    if (n < 6 || strcmp(templ + n - 6, "XXXXXX")) {
        return NULL;
    }
    for (tries = 0; tries < 10000; tries++) {
        unsigned long v = (unsigned long)__syscall(SYS_getpid) * 7 + (__tmpseq++);
        for (i = 0; i < 6; i++) {
            templ[n - 6 + i] = 'a' + (int)(v % 26);
            v /= 26;
            if (i == 3) {
                v += tries;
            }
        }
        if (__syscall(SYS_access, templ, 0) < 0) {
            return templ;
        }
    }
    return NULL;
}
