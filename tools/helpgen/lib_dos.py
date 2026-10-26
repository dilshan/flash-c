# File:    lib_dos.py
# Purpose: Help text for the functions of <dos.h>, used by gen.py.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

H = "dos.h"
F = [
("delay","void delay(unsigned milliseconds);","Waits for a number of milliseconds.","Uses the system sleep, so the processor is not kept busy.","Nothing.","sleep sound","#include <dos.h>\n#include <stdio.h>\n\nint main(void)\n{\n    int i;\n    for (i = 3; i > 0; i--) {\n        printf(\"%d\\n\", i);\n        delay(1000);\n    }\n    return 0;\n}"),
("dostounix","long dostounix(struct date *d, struct time *t);","Converts DOS date and time to calendar time.","The values are taken as local time. Gives seconds since 1 January 1970 UTC.","The calendar time.","unixtodos getdate gettime mktime",None),
("getdate","void getdate(struct date *d);","Gets the current system date.","Fills in da_year, da_mon (1-12) and da_day (1-31) of {struct date}.","Nothing.","gettime setdate",None),
("getdfree","void getdfree(unsigned char drive, struct dfree *table);","Gets the free space of the disk.","Drive letters do not exist on this system: the argument is ignored and the file system that holds / is described. The counts are limited to 65535 (the structure is {struct dfree}).","Nothing.","dostounix",None),
("getpid","int getpid(void);","Returns the process id of the program.","","The process id.","system",None),
("gettime","void gettime(struct time *t);","Gets the current system time.","Fills in ti_hour, ti_min, ti_sec and ti_hund (hundredths of a second) of {struct time}.","Nothing.","getdate settime time",None),
("nosound","void nosound(void);","Turns the speaker off.","No speaker is controlled on this system; the function does nothing.","Nothing.","sound",None),
("setdate","void setdate(struct date *d);","Sets the system date.","Changes the system clock, which needs administrator privileges; without them the call has no effect.","Nothing.","getdate settime",None),
("settime","void settime(struct time *t);","Sets the system time.","Changes the system clock, which needs administrator privileges; without them the call has no effect.","Nothing.","gettime setdate",None),
("sleep","unsigned sleep(unsigned seconds);","Waits for a number of seconds.","","0, or the seconds left if the wait was interrupted.","delay",None),
("sound","void sound(unsigned frequency);","Turns the speaker on at a frequency.","There is no speaker control. The function writes a bell character (BEL) to stderr, and the frequency is ignored; nosound does nothing.","Nothing.","nosound delay",None),
("unixtodos","void unixtodos(long utime, struct date *d, struct time *t);","Converts calendar time to DOS date and time.","The result is local time.","Nothing.","dostounix localtime",None),
("unlink","int unlink(const char *path);","Deletes a file.","Same as remove for files. Declared in dos.h and io.h.","0 on success, -1 on error (errno is set).","remove",None),
]
TYPES = [
("struct date","Members: int da_year, char da_day, char da_mon. Used by getdate, setdate and dostounix. Declared in dos.h."),
("struct time","Members: unsigned char ti_min, ti_hour, ti_hund, ti_sec. Used by gettime, settime and dostounix. Declared in dos.h."),
("struct dfree","Members: unsigned df_avail, df_total, df_bsec, df_sclus (free clusters, total clusters, bytes per sector, sectors per cluster). Used by getdfree."),
]
