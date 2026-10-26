/*
 * File:    time.h
 * Purpose: Calendar time, time zones and the DOS-flavoured date/time calls.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_TIME_H
#define _FC_TIME_H
#include <_defs.h>
#ifndef _FC_TIME_T
#define _FC_TIME_T
typedef long time_t;
#endif
#ifndef _FC_CLOCK_T
#define _FC_CLOCK_T
typedef long clock_t;
#endif
#define CLOCKS_PER_SEC 1000
#define CLK_TCK 1000
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};
extern int daylight;
extern long timezone;
extern char *tzname[2];
char *asctime(const struct tm *t);
clock_t clock(void);
char *ctime(const time_t *t);
double difftime(time_t t1, time_t t0);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
time_t mktime(struct tm *t);
size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *t);
time_t time(time_t *t);
int stime(time_t *t);
void tzset(void);
char *_strdate(char *buf);
char *_strtime(char *buf);
#endif
