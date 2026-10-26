/*
 * File:    dos.h
 * Purpose: DOS-style date, time, delay and file-attribute services.
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#ifndef _FC_DOS_H
#define _FC_DOS_H
#include <_defs.h>
struct date {
    int da_year;
    char da_day;
    char da_mon;
};
struct time {
    unsigned char ti_min;
    unsigned char ti_hour;
    unsigned char ti_hund;
    unsigned char ti_sec;
};
struct dfree {
    unsigned df_avail;
    unsigned df_total;
    unsigned df_bsec;
    unsigned df_sclus;
};
void delay(unsigned milliseconds);
unsigned sleep(unsigned seconds);
void sound(unsigned frequency);
void nosound(void);
void getdate(struct date *d);
void gettime(struct time *t);
void setdate(struct date *d);
void settime(struct time *t);
long dostounix(struct date *d, struct time *t);
void unixtodos(long utime, struct date *d, struct time *t);
void getdfree(unsigned char drive, struct dfree *table);
int getpid(void);
int unlink(const char *path);
extern int _osmajor;
extern int _osminor;
#endif
