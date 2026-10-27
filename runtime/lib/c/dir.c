/*
 * File:    dir.c
 * Purpose: dir - directories, wildcard searches (findfirst/findnext), and path
 *          name handling
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <dir.h>
#include <direct.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <_fcrt.h>

/* Stores the current directory in buf (size n); returns buf or NULL on error. */
char *getcwd(char *buf, int n)
{
    char tmp[4096];
    long r = __syscall(SYS_getcwd, tmp, sizeof tmp);
    if (r < 0) {
        __sysret(r);
        return NULL;
    }
    if (!buf) {
        buf = malloc(n > (int)r ? n : (int)r);
        if (!buf) {
            return NULL;
        }
        n = 4096;
    }
    if ((int)r > n) {
        errno = ERANGE;
        return NULL;
    }
    strcpy(buf, tmp);
    return buf;
}

/* Changes the current directory; returns 0 on success. */
int chdir(const char *path)
{
    return (int)__sysret(__syscall(SYS_chdir, path));
}

/* Creates a directory; returns 0 on success. */
int mkdir(const char *path)
{
    return (int)__sysret(__syscall(SYS_mkdir, path, 0777));
}

/* Removes an empty directory; returns 0 on success. */
int rmdir(const char *path)
{
    return (int)__sysret(__syscall(SYS_rmdir, path));
}

/* there is one file system: drive A: is the root, "drive 2" (C:) stands for it */
int getdisk(void)
{
    return 2;
}

/* DOS compatibility: there is a single file system, so this only returns the drive count (26). */
int setdisk(int drive)
{
    return 26;
}

/* DOS compatibility: always drive 3 ("C:"), which stands for the whole file system. */
int _getdrive(void)
{
    return 3;
}

/* DOS compatibility: changing the drive has no effect; returns 0. */
int _chdrive(int drive)
{
    return 0;
}

/* Stores the current directory (without the leading slash) in dir; returns 0 on success. */
int getcurdir(int drive, char *dir)
{
    char tmp[4096];
    if (!getcwd(tmp, sizeof tmp)) {
        return -1;
    }
    strcpy(dir, tmp[0] == '/' ? tmp + 1 : tmp);
    return 0;
}

/* Stores the current directory of a drive in buf (size n); returns buf. */
char *_getdcwd(int drive, char *buf, int n)
{
    return getcwd(buf, n);
}

/* ---- directory streams */
DIR *opendir(const char *name)
{
    DIR *d;
    int fd = (int)__syscall(SYS_open, name, 0200000, 0); /* O_DIRECTORY */
    if (fd < 0) {
        __sysret(fd);
        return NULL;
    }
    d = malloc(sizeof(DIR));
    if (!d) {
        __syscall(SYS_close, fd);
        errno = ENOMEM;
        return NULL;
    }
    d->fd = fd;
    d->pos = d->len = 0;
    return d;
}

/* Returns the next entry of an open directory, or NULL at the end. */
struct dirent *readdir(DIR *d)
{
    char *e;
    if (d->pos >= d->len) {
        long n = __syscall(SYS_getdents64, d->fd, d->buf, sizeof d->buf);
        if (n <= 0) {
            if (n < 0) {
                __sysret(n);
            }
            return NULL;
        }
        d->len = (int)n;
        d->pos = 0;
    }
    e = d->buf + d->pos;
    d->pos += *(unsigned short *)(e + 16);
    strcpy(d->ent.d_name, e + 19);
    d->ent.d_reclen = (unsigned short)strlen(d->ent.d_name);
    return &d->ent;
}

/* Closes a directory opened by opendir(). */
int closedir(DIR *d)
{
    int r = (int)__sysret(__syscall(SYS_close, d->fd));
    free(d);
    return r;
}

/* Restarts the reading of an open directory. */
void rewinddir(DIR *d)
{
    __syscall(SYS_lseek, d->fd, 0, 0);
    d->pos = d->len = 0;
}

/* ---- wildcard matching: * ? and [set]; DOS style, case-sensitive here */
static int __wmatch(const char *p, const char *s)
{
    for (; *p; p++) {
        if (*p == '*') {
            while (p[1] == '*') {
                p++;
            }
            if (!p[1]) {
                return 1;
            }
            for (; *s; s++) {
                if (__wmatch(p + 1, s)) {
                    return 1;
                }
            }
            return __wmatch(p + 1, s);
        }
        if (!*s) {
            return 0;
        }
        if (*p == '?') {
            s++;
            continue;
        }
        if (*p == '[') {
            int ok = 0, neg = 0;
            p++;
            if (*p == '^' || *p == '!') {
                neg = 1;
                p++;
            }
            while (*p && *p != ']') {
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    if (*s >= p[0] && *s <= p[2]) {
                        ok = 1;
                    }
                    p += 3;
                } else {
                    if (*s == *p) {
                        ok = 1;
                    }
                    p++;
                }
            }
            if (ok == neg) {
                return 0;
            }
            s++;
            continue;
        }
        if (*p != *s) {
            return 0;
        }
        s++;
    }
    return !*s;
}

/* the search state lives in ff_reserved */
struct __fsrch {
    DIR *d;
    int attrib;
    char dir[44];
    char pat[80];
};

/* Finds the next directory entry that matches the search and stores it in the ffblk. */
static int __fill(struct ffblk *ff)
{
    struct __fsrch *s = (struct __fsrch *)ff->ff_reserved;
    struct dirent *e;
    while ((e = readdir(s->d)) != NULL) {
        struct stat st;
        char full[1100];
        struct tm *tm;
        time_t t;
        if (e->d_name[0] == '.' && !(s->pat[0] == '.') &&
            (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0)) && 0) {
            continue;
        }
        if (!__wmatch(s->pat, e->d_name)) {
            continue;
        }
        if (e->d_name[0] == '.' && s->pat[0] != '.') {
            continue; /* hidden files need an explicit dot */
        }
        strcpy(full, s->dir[0] ? s->dir : ".");
        strcat(full, "/");
        strcat(full, e->d_name);
        if (stat(full, &st) < 0) {
            continue;
        }
        ff->ff_attrib = 0;
        if ((st.st_mode & S_IFMT) == S_IFDIR) {
            ff->ff_attrib |= 0x10;
        }
        if (!(st.st_mode & S_IWRITE)) {
            ff->ff_attrib |= 0x01;
        }
        if (e->d_name[0] == '.') {
            ff->ff_attrib |= 0x02;
        }
        if ((ff->ff_attrib & 0x10) && !(s->attrib & 0x10)) {
            continue;
        }
        t = st.st_mtime;
        tm = localtime(&t);
        ff->ff_ftime = (tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2);
        ff->ff_fdate = ((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday;
        ff->ff_fsize = st.st_size;
        strncpy(ff->ff_name, e->d_name, 255);
        ff->ff_name[255] = 0;
        return 0;
    }
    closedir(s->d);
    s->d = NULL;
    errno = ENOENT;
    return -1;
}

/* Starts a wildcard search; fills ff with the first match.  Returns 0 if one was found. */
int findfirst(const char *path, struct ffblk *ff, int attrib)
{
    struct __fsrch *s = (struct __fsrch *)ff->ff_reserved;
    const char *slash = strrchr(path, '/');
    int dl = slash ? (int)(slash - path) : 0;
    const char *pat = slash ? slash + 1 : path;
    char dirbuf[1024];
    if (dl >= (int)sizeof s->dir || strlen(pat) >= sizeof s->pat) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(s->dir, path, dl);
    s->dir[dl] = 0;
    if (slash && !dl) {
        strcpy(s->dir, "/");
    }
    strcpy(s->pat, pat);
    s->attrib = attrib;
    strcpy(dirbuf, s->dir[0] ? s->dir : ".");
    s->d = opendir(dirbuf);
    if (!s->d) {
        return -1;
    }
    return __fill(ff);
}

/* Continues a search started by findfirst(); returns 0 if another match was found. */
int findnext(struct ffblk *ff)
{
    struct __fsrch *s = (struct __fsrch *)ff->ff_reserved;
    if (!s->d) {
        errno = ENOENT;
        return -1;
    }
    return __fill(ff);
}

/* ---- path names */
int fnsplit(const char *path, char *drive, char *dir, char *name, char *ext)
{
    const char *p = path, *slash, *dot;
    int flags = 0, n;
    if (drive) {
        drive[0] = 0;
    }
    if (p[0] && p[1] == ':') {
        if (drive) {
            drive[0] = p[0];
            drive[1] = ':';
            drive[2] = 0;
        }
        p += 2;
        flags |= DRIVE;
    }
    slash = strrchr(p, '/');
    if (slash) {
        n = (int)(slash - p) + 1;
        if (dir) {
            if (n > 65) {
                n = 65;
            }
            memcpy(dir, p, n);
            dir[n] = 0;
        }
        flags |= DIRECTORY;
        p = slash + 1;
    } else if (dir) {
        dir[0] = 0;
    }
    dot = strrchr(p, '.');
    if (dot && dot != p) {
        n = (int)(dot - p);
        if (n > 8) {
            n = 8;
        }
        if (name) {
            memcpy(name, p, n);
            name[n] = 0;
        }
        if (ext) {
            strncpy(ext, dot, 4);
            ext[4] = 0;
        }
        flags |= FILENAME | EXTENSION;
    } else {
        if (name) {
            strncpy(name, p, 8);
            name[8] = 0;
        }
        if (ext) {
            ext[0] = 0;
        }
        if (*p) {
            flags |= FILENAME;
        }
    }
    if (strpbrk(path, "*?")) {
        flags |= WILDCARDS;
    }
    return flags;
}

/* Builds a path from drive, directory, name and extension. */
void fnmerge(char *path, const char *drive, const char *dir, const char *name, const char *ext)
{
    path[0] = 0;
    if (drive && *drive) {
        strcat(path, drive);
        if (!strchr(drive, ':')) {
            strcat(path, ":");
        }
    }
    if (dir && *dir) {
        strcat(path, dir);
        if (dir[strlen(dir) - 1] != '/') {
            strcat(path, "/");
        }
    }
    if (name) {
        strcat(path, name);
    }
    if (ext && *ext) {
        if (*ext != '.') {
            strcat(path, ".");
        }
        strcat(path, ext);
    }
}

/* Splits a path into drive, directory, name and extension. */
void _splitpath(const char *path, char *drive, char *dir, char *name, char *ext)
{
    fnsplit(path, drive, dir, name, ext);
}

/* Builds a path from drive, directory, name and extension. */
void _makepath(char *path, const char *drive, const char *dir, const char *name, const char *ext)
{
    fnmerge(path, drive, dir, name, ext);
}

/* absolute form of a path, with . and .. resolved (the file need not exist) */
char *_fullpath(char *buf, const char *path, size_t n)
{
    char tmp[4200], *out, *seg;
    char work[4200];
    int len = 0;
    if (path[0] == '/') {
        strcpy(work, path);
    } else {
        if (!getcwd(work, sizeof work - 2)) {
            return NULL;
        }
        strcat(work, "/");
        strcat(work, path);
    }
    out = tmp;
    *out = 0;
    seg = work;
    while (*seg) {
        char *e;
        while (*seg == '/') {
            seg++;
        }
        if (!*seg) {
            break;
        }
        e = seg;
        while (*e && *e != '/') {
            e++;
        }
        if (e - seg == 1 && seg[0] == '.') {
        } else if (e - seg == 2 && seg[0] == '.' && seg[1] == '.') {
            char *sl = strrchr(tmp, '/');
            if (sl) {
                *sl = 0;
            }
        } else {
            strcat(tmp, "/");
            strncat(tmp, seg, e - seg);
        }
        seg = e;
    }
    if (!tmp[0]) {
        strcpy(tmp, "/");
    }
    len = (int)strlen(tmp);
    if (!buf) {
        buf = malloc(len + 1);
        if (!buf) {
            return NULL;
        }
    } else if (len >= (int)n) {
        errno = ERANGE;
        return NULL;
    }
    strcpy(buf, tmp);
    return buf;
}

/* Looks for a file in the current directory and in the directories of $PATH. */
char *searchpath(const char *file)
{
    static char buf[1100];
    const char *p;
    if (strchr(file, '/')) {
        if (__syscall(SYS_access, file, 0) == 0) {
            strcpy(buf, file);
            return buf;
        }
        return NULL;
    }
    if (__syscall(SYS_access, file, 0) == 0) {
        strcpy(buf, file);
        return buf;
    }
    p = getenv("PATH");
    while (p && *p) {
        int n = 0;
        while (p[n] && p[n] != ':') {
            n++;
        }
        if (n + strlen(file) + 2 < sizeof buf) {
            memcpy(buf, p, n);
            buf[n] = '/';
            strcpy(buf + n + 1, file);
            if (__syscall(SYS_access, buf, 0) == 0) {
                return buf;
            }
        }
        p += n;
        if (*p == ':') {
            p++;
        }
    }
    return NULL;
}

/* Looks for a file in the directories listed in the environment variable varname. */
void _searchenv(const char *file, const char *varname, char *buf)
{
    const char *p = getenv(varname);
    buf[0] = 0;
    if (__syscall(SYS_access, file, 0) == 0) {
        strcpy(buf, file);
        return;
    }
    while (p && *p) {
        int n = 0;
        while (p[n] && p[n] != ':') {
            n++;
        }
        if (n + strlen(file) + 2 < 260) {
            memcpy(buf, p, n);
            buf[n] = '/';
            strcpy(buf + n + 1, file);
            if (__syscall(SYS_access, buf, 0) == 0) {
                return;
            }
        }
        p += n;
        if (*p == ':') {
            p++;
        }
    }
    buf[0] = 0;
}
