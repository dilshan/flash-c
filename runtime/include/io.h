#ifndef _FC_IO_H
#define _FC_IO_H
#include <_defs.h>
#include <fcntl.h>
#include <share.h>
#include <locking.h>
struct ftime {
    unsigned ft_tsec, ft_min, ft_hour, ft_day, ft_month, ft_year;   /* ft_year counts from 1980; ft_tsec is in 2 second units */
};
int access(const char *path, int amode);
int chmod(const char *path, int amode);
int _chmod(const char *path, int func, ...);
int chsize(int fd, long size);
int close(int fd);
int _close(int fd);
int creat(const char *path, int mode);
int _creat(const char *path, int attrib);
int creatnew(const char *path, int mode);
int creattemp(char *path, int attrib);
int dup(int fd);
int dup2(int fd, int fd2);
int eof(int fd);
long filelength(int fd);
int getftime(int fd, struct ftime *ft);
int ioctl(int fd, int cmd, ...);
int isatty(int fd);
int lock(int fd, long offset, long length);
int locking(int fd, int mode, long length);
long lseek(int fd, long offset, int whence);
char *mktemp(char *templ);
int open(const char *path, int oflag, ...);
int _open(const char *path, int oflag);
int read(int fd, void *buf, unsigned len);
int _read(int fd, void *buf, unsigned len);
int remove(const char *path);
int rename(const char *from, const char *to);
int setftime(int fd, struct ftime *ft);
int setmode(int fd, int mode);
int sopen(const char *path, int oflag, int shflag, ...);
long tell(int fd);
int umask(int mask);
int unlink(const char *path);
int unlock(int fd, long offset, long length);
int write(int fd, const void *buf, unsigned len);
int _write(int fd, const void *buf, unsigned len);
#endif
