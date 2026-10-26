#ifndef _FC_SYS_TIMEB_H
#define _FC_SYS_TIMEB_H
#include <time.h>
struct timeb {
    long time;
    short millitm;
    short timezone;
    short dstflag;
};
void ftime(struct timeb *tp);
#endif
