/*
 * File:    time.c
 * Purpose: time - calendar time, time zones, and the DOS-flavoured date/time
 *          calls The time zone comes from the TZ variable (POSIX form such as
 *          EST5EDT or CET-1CEST, US daylight-saving rules); without TZ the local
 *          time is UTC.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <sys/timeb.h>
#include <_fcrt.h>

int daylight;
long timezone;
char *tzname[2] = {"UTC", "UTC"};
static char __stdname[8] = "UTC", __dstname[8] = "UTC";
static int __tz_done;

static const char *__wday[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                               "Thursday", "Friday", "Saturday"};
static const char *__mon[] = {"January",   "February", "March",    "April",
                              "May",       "June",     "July",     "August",
                              "September", "October",  "November", "December"};

/* days since 1970-01-01 of a civil date */
static long __days(long y, int m, int d)
{
    long era, yoe, doy, doe;
    if (m <= 2) {
        y--;
    }
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Converts a day count since 1970-01-01 to year, month and day. */
static void __civil(long z, long *y, int *m, int *d)
{
    long era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (*m <= 2) {
        (*y)++;
    }
}

/* Returns true if y is a leap year. */
static int __leap(long y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* Reads the time zone from the TZ variable. */
void tzset(void)
{
    const char *tz = getenv("TZ");
    int i = 0, sign = 1, h = 0, m = 0;
    __tz_done = 1;
    daylight = 0;
    timezone = 0;
    strcpy(__stdname, "UTC");
    strcpy(__dstname, "UTC");
    tzname[0] = __stdname;
    tzname[1] = __dstname;
    if (!tz || !*tz) {
        return;
    }
    if (*tz == ':') {
        tz++;
    }
    {
        int n = 0;
        while (((tz[i] | 32) >= 'a' && (tz[i] | 32) <= 'z') && n < 7) {
            __stdname[n++] = tz[i++];
        }
        __stdname[n] = 0;
        if (n < 3) {
            strcpy(__stdname, "UTC");
            return;
        }
    }
    if (tz[i] == '-') {
        sign = -1;
        i++;
    } else if (tz[i] == '+') {
        i++;
    }
    while (tz[i] >= '0' && tz[i] <= '9') {
        h = h * 10 + (tz[i++] - '0');
    }
    if (tz[i] == ':') {
        i++;
        while (tz[i] >= '0' && tz[i] <= '9') {
            m = m * 10 + (tz[i++] - '0');
        }
    }
    timezone = sign * (h * 3600L + m * 60L);
    {
        int n = 0;
        while (((tz[i] | 32) >= 'a' && (tz[i] | 32) <= 'z') && n < 7) {
            __dstname[n++] = tz[i++];
        }
        __dstname[n] = 0;
        if (n >= 3) {
            daylight = 1;
        } else {
            strcpy(__dstname, __stdname);
        }
    }
}

/* day of month of the n-th (1..5, 5 = last) Sunday of a month */
static int __sunday(long y, int m, int n)
{
    long d1 = __days(y, m, 1);
    int wd = (int)((d1 + 4) % 7); /* 0 = Sunday */
    int first = 1 + (7 - wd) % 7;
    int dim = m == 2 ? (__leap(y) ? 29 : 28) : (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
    int d = first + (n - 1) * 7;
    while (d > dim) {
        d -= 7;
    }
    return d;
}

/* is daylight-saving time in effect at the UTC instant t? (first Sunday of April to last Sunday of
 * October) */
static int __isdst(long t)
{
    long y, std = t - timezone, start, end;
    int m, d;
    if (!daylight) {
        return 0;
    }
    __civil(std >= 0 ? std / 86400 : (std - 86399) / 86400, &y, &m, &d);
    start = __days(y, 4, __sunday(y, 4, 1)) * 86400L + 2 * 3600L;
    end = __days(y, 10, __sunday(y, 10, 5)) * 86400L + 1 * 3600L;
    return std >= start && std < end;
}

/* Converts a time_t to broken-down UTC time in *r. */
static struct tm *__gm(long t, struct tm *r)
{
    long days = t >= 0 ? t / 86400 : (t - 86399) / 86400, y;
    long rem = t - days * 86400;
    int m, d;
    __civil(days, &y, &m, &d);
    r->tm_year = (int)(y - 1900);
    r->tm_mon = m - 1;
    r->tm_mday = d;
    r->tm_hour = (int)(rem / 3600);
    r->tm_min = (int)(rem % 3600 / 60);
    r->tm_sec = (int)(rem % 60);
    r->tm_wday = (int)((days + 4) % 7);
    if (r->tm_wday < 0) {
        r->tm_wday += 7;
    }
    r->tm_yday = (int)(days - __days(y, 1, 1));
    r->tm_isdst = 0;
    return r;
}

static struct tm __tmbuf;

/* Converts a calendar time to UTC broken-down time. Returns a pointer to the structure. */
struct tm *gmtime(const time_t *t)
{
    return __gm(*t, &__tmbuf);
}

/* Converts a calendar time to local broken-down time. Returns a pointer to the structure. */
struct tm *localtime(const time_t *t)
{
    long off, v = *t;
    int dst;
    if (!__tz_done) {
        tzset();
    }
    dst = __isdst(v);
    off = timezone - (dst ? 3600 : 0);
    __gm(v - off, &__tmbuf);
    __tmbuf.tm_isdst = dst;
    return &__tmbuf;
}

/* Converts local broken-down time to calendar time. Returns the calendar time, or (time_t)-1. */
time_t mktime(struct tm *tm)
{
    long y = tm->tm_year + 1900L, t;
    int mon = tm->tm_mon;
    if (!__tz_done) {
        tzset();
    }
    y += mon / 12;
    mon %= 12;
    if (mon < 0) {
        mon += 12;
        y--;
    }
    t = __days(y, mon + 1, 1) + (tm->tm_mday - 1);
    t = t * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
    t += timezone;
    if (tm->tm_isdst > 0 || (tm->tm_isdst < 0 && __isdst(t - 3600))) {
        t -= 3600;
    }
    if (tm->tm_isdst == 0 && daylight && __isdst(t)) {
    }
    {
        time_t tt = t;
        *tm = *localtime(&tt);
    }
    return t;
}

/* Converts broken-down time to a string. Returns a pointer to the string. */
char *asctime(const struct tm *t)
{
    static char buf[40];
    int y = t->tm_year + 1900;
    char *p = buf;
    const char *w = __wday[(unsigned)t->tm_wday % 7], *m = __mon[(unsigned)t->tm_mon % 12];
    *p++ = w[0];
    *p++ = w[1];
    *p++ = w[2];
    *p++ = ' ';
    *p++ = m[0];
    *p++ = m[1];
    *p++ = m[2];
    *p++ = ' ';
    *p++ = t->tm_mday >= 10 ? '0' + t->tm_mday / 10 : ' ';
    *p++ = '0' + t->tm_mday % 10;
    *p++ = ' ';
    *p++ = '0' + t->tm_hour / 10;
    *p++ = '0' + t->tm_hour % 10;
    *p++ = ':';
    *p++ = '0' + t->tm_min / 10;
    *p++ = '0' + t->tm_min % 10;
    *p++ = ':';
    *p++ = '0' + t->tm_sec / 10;
    *p++ = '0' + t->tm_sec % 10;
    *p++ = ' ';
    *p++ = '0' + y / 1000 % 10;
    *p++ = '0' + y / 100 % 10;
    *p++ = '0' + y / 10 % 10;
    *p++ = '0' + y % 10;
    *p++ = '\n';
    *p = 0;
    return buf;
}

/* Converts a calendar time to a string. Returns a pointer to the string. */
char *ctime(const time_t *t)
{
    return asctime(localtime(t));
}

/* Difference of two times in seconds. Returns time1 - time0 as a double. */
double difftime(time_t t1, time_t t0)
{
    return (double)(t1 - t0);
}

/* Returns the current calendar time. */
time_t time(time_t *t)
{
    long r = __syscall(SYS_time, 0);
    if (t) {
        *t = r;
    }
    return r;
}

/*
 * Returns the processor time used by the program.
 */
clock_t clock(void)
{
    long ts[2];
    if (__syscall(SYS_clock_gettime, 2, ts) < 0) {
        return -1; /* CLOCK_PROCESS_CPUTIME_ID */
    }
    return ts[0] * 1000 + ts[1] / 1000000;
}

/* Sets the system time; needs privileges. */
int stime(time_t *t)
{
    long tv[2];
    tv[0] = *t;
    tv[1] = 0;
    return __sysret(__syscall(SYS_settimeofday, tv, 0)) < 0 ? -1 : 0;
}

/* Writes v as two decimal digits at p. */
static void __two(char *p, int v)
{
    p[0] = '0' + v / 10 % 10;
    p[1] = '0' + v % 10;
}

/* Formats a broken-down time according to a strftime format into p (up to end). */
static char *__fmt(char *p, char *end, const char *f, const struct tm *t)
{
    for (; *f && p < end; f++) {
        char tmp[40];
        const char *s = tmp;
        int n;
        if (*f != '%') {
            *p++ = *f;
            continue;
        }
        f++;
        if (*f == 'E' || *f == 'O') {
            f++;
        }
        tmp[0] = 0;
        switch (*f) {
        case 'a':
            s = __wday[(unsigned)t->tm_wday % 7];
            strncpy(tmp, s, 3);
            tmp[3] = 0;
            s = tmp;
            break;
        case 'A':
            s = __wday[(unsigned)t->tm_wday % 7];
            break;
        case 'b':
        case 'h':
            s = __mon[(unsigned)t->tm_mon % 12];
            strncpy(tmp, s, 3);
            tmp[3] = 0;
            s = tmp;
            break;
        case 'B':
            s = __mon[(unsigned)t->tm_mon % 12];
            break;
        case 'c': {
            char *q = tmp;
            q = __fmt(q, tmp + 38, "%a %b %e %H:%M:%S %Y", t);
            *q = 0;
            break;
        }
        case 'C':
            __two(tmp, (t->tm_year + 1900) / 100);
            tmp[2] = 0;
            break;
        case 'd':
            __two(tmp, t->tm_mday);
            tmp[2] = 0;
            break;
        case 'D': {
            char *q = __fmt(tmp, tmp + 38, "%m/%d/%y", t);
            *q = 0;
            break;
        }
        case 'e':
            __two(tmp, t->tm_mday);
            if (tmp[0] == '0') {
                tmp[0] = ' ';
            }
            tmp[2] = 0;
            break;
        case 'F': {
            char *q = __fmt(tmp, tmp + 38, "%Y-%m-%d", t);
            *q = 0;
            break;
        }
        case 'H':
            __two(tmp, t->tm_hour);
            tmp[2] = 0;
            break;
        case 'I':
            __two(tmp, t->tm_hour % 12 ? t->tm_hour % 12 : 12);
            tmp[2] = 0;
            break;
        case 'j':
            tmp[0] = '0' + (t->tm_yday + 1) / 100;
            __two(tmp + 1, (t->tm_yday + 1) % 100);
            tmp[3] = 0;
            break;
        case 'm':
            __two(tmp, t->tm_mon + 1);
            tmp[2] = 0;
            break;
        case 'M':
            __two(tmp, t->tm_min);
            tmp[2] = 0;
            break;
        case 'n':
            tmp[0] = '\n';
            tmp[1] = 0;
            break;
        case 'p':
            s = t->tm_hour >= 12 ? "PM" : "AM";
            break;
        case 'r': {
            char *q = __fmt(tmp, tmp + 38, "%I:%M:%S %p", t);
            *q = 0;
            break;
        }
        case 'R': {
            char *q = __fmt(tmp, tmp + 38, "%H:%M", t);
            *q = 0;
            break;
        }
        case 'S':
            __two(tmp, t->tm_sec);
            tmp[2] = 0;
            break;
        case 't':
            tmp[0] = '\t';
            tmp[1] = 0;
            break;
        case 'T':
        case 'X': {
            char *q = __fmt(tmp, tmp + 38, "%H:%M:%S", t);
            *q = 0;
            break;
        }
        case 'u':
            tmp[0] = '0' + (t->tm_wday ? t->tm_wday : 7);
            tmp[1] = 0;
            break;
        case 'U':
            __two(tmp, (t->tm_yday + 7 - t->tm_wday) / 7);
            tmp[2] = 0;
            break;
        case 'w':
            tmp[0] = '0' + t->tm_wday;
            tmp[1] = 0;
            break;
        case 'W':
            __two(tmp, (t->tm_yday + 7 - (t->tm_wday + 6) % 7) / 7);
            tmp[2] = 0;
            break;
        case 'x': {
            char *q = __fmt(tmp, tmp + 38, "%m/%d/%y", t);
            *q = 0;
            break;
        }
        case 'y':
            __two(tmp, (t->tm_year + 1900) % 100);
            tmp[2] = 0;
            break;
        case 'Y': {
            int y = t->tm_year + 1900;
            tmp[0] = '0' + y / 1000 % 10;
            tmp[1] = '0' + y / 100 % 10;
            tmp[2] = '0' + y / 10 % 10;
            tmp[3] = '0' + y % 10;
            tmp[4] = 0;
            break;
        }
        case 'Z':
            s = t->tm_isdst > 0 ? __dstname : __stdname;
            break;
        case 'z': {
            long off = -(timezone - (t->tm_isdst > 0 ? 3600 : 0));
            tmp[0] = off < 0 ? '-' : '+';
            if (off < 0) {
                off = -off;
            }
            __two(tmp + 1, (int)(off / 3600));
            __two(tmp + 3, (int)(off % 3600 / 60));
            tmp[5] = 0;
            break;
        }
        case '%':
            tmp[0] = '%';
            tmp[1] = 0;
            break;
        default:
            tmp[0] = '%';
            tmp[1] = *f;
            tmp[2] = 0;
            if (!*f) {
                f--;
            }
            break;
        }
        n = (int)strlen(s);
        if (p + n > end) {
            return end + 1;
        }
        memcpy(p, s, n);
        p += n;
    }
    return p;
}

/*
 * Formats a time as text. Returns the number of characters stored (not counting the null), or
 * 0 if maxsize is too small.
 */
size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *t)
{
    char *p;
    if (!max) {
        return 0;
    }
    if (!__tz_done) {
        tzset();
    }
    p = __fmt(buf, buf + max - 1, fmt, t);
    if (p > buf + max - 1) {
        buf[0] = 0;
        return 0;
    }
    *p = 0;
    return (size_t)(p - buf);
}

/* Writes the date as mm/dd/yy into buf. */
char *_strdate(char *buf)
{
    time_t t = time(0);
    struct tm *tm = localtime(&t);
    __two(buf, tm->tm_mon + 1);
    buf[2] = '/';
    __two(buf + 3, tm->tm_mday);
    buf[5] = '/';
    __two(buf + 6, tm->tm_year % 100);
    buf[8] = 0;
    return buf;
}

/* Writes the time as hh:mm:ss into buf. */
char *_strtime(char *buf)
{
    time_t t = time(0);
    struct tm *tm = localtime(&t);
    __two(buf, tm->tm_hour);
    buf[2] = ':';
    __two(buf + 3, tm->tm_min);
    buf[5] = ':';
    __two(buf + 6, tm->tm_sec);
    buf[8] = 0;
    return buf;
}

/* Fills *tp with the current time in seconds and milliseconds. */
void ftime(struct timeb *tp)
{
    long ts[2];
    time_t t;
    __syscall(SYS_clock_gettime, 0, ts);
    t = ts[0];
    if (!__tz_done) {
        tzset();
    }
    tp->time = ts[0];
    tp->millitm = (short)(ts[1] / 1000000);
    tp->timezone = (short)(timezone / 60);
    tp->dstflag = (short)__isdst(t);
}

/* Gets the current system date. */
void getdate(struct date *d)
{
    time_t t = time(0);
    struct tm *tm = localtime(&t);
    d->da_year = tm->tm_year + 1900;
    d->da_mon = tm->tm_mon + 1;
    d->da_day = tm->tm_mday;
}

/* Gets the current system time. */
void gettime(struct time *tt)
{
    long ts[2];
    time_t t;
    struct tm *tm;
    __syscall(SYS_clock_gettime, 0, ts);
    t = ts[0];
    tm = localtime(&t);
    tt->ti_hour = tm->tm_hour;
    tt->ti_min = tm->tm_min;
    tt->ti_sec = tm->tm_sec;
    tt->ti_hund = (unsigned char)(ts[1] / 10000000);
}

/* Sets the system date. */
void setdate(struct date *d)
{
    time_t t = time(0);
    struct tm tm = *localtime(&t);
    tm.tm_year = d->da_year - 1900;
    tm.tm_mon = d->da_mon - 1;
    tm.tm_mday = d->da_day;
    tm.tm_isdst = -1;
    t = mktime(&tm);
    stime(&t);
}

/* Sets the system time. */
void settime(struct time *tt)
{
    time_t t = time(0);
    struct tm tm = *localtime(&t);
    tm.tm_hour = tt->ti_hour;
    tm.tm_min = tt->ti_min;
    tm.tm_sec = tt->ti_sec;
    tm.tm_isdst = -1;
    t = mktime(&tm);
    stime(&t);
}

/* Converts DOS date and time to calendar time. Returns the calendar time. */
long dostounix(struct date *d, struct time *tt)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = d->da_year - 1900;
    tm.tm_mon = d->da_mon - 1;
    tm.tm_mday = d->da_day;
    tm.tm_hour = tt->ti_hour;
    tm.tm_min = tt->ti_min;
    tm.tm_sec = tt->ti_sec;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/* Converts calendar time to DOS date and time. */
void unixtodos(long utime, struct date *d, struct time *tt)
{
    time_t t = utime;
    struct tm *tm = localtime(&t);
    d->da_year = tm->tm_year + 1900;
    d->da_mon = tm->tm_mon + 1;
    d->da_day = tm->tm_mday;
    tt->ti_hour = tm->tm_hour;
    tt->ti_min = tm->tm_min;
    tt->ti_sec = tm->tm_sec;
    tt->ti_hund = 0;
}
