#ifndef _FC_UTIME_H
#define _FC_UTIME_H
#include <time.h>
struct utimbuf {
    long actime;
    long modtime;
};
int utime(const char *path, struct utimbuf *times);
#endif
