H = "time.h"
F = [
("asctime","char *asctime(const struct tm *timeptr);","Converts broken-down time to a string.","The format is \"Sun Sep 16 01:03:52 1973\\n\". A static buffer is used.","A pointer to the string.","ctime strftime",None),
("clock","clock_t clock(void);","Returns the processor time used by the program.","Divide by CLOCKS_PER_SEC to get seconds.","The time, or (clock_t)-1 if not available.","time difftime",None),
("ctime","char *ctime(const time_t *timer);","Converts a calendar time to a string.","Same as asctime(localtime(timer)).","A pointer to the string.","asctime localtime",None),
("difftime","double difftime(time_t time1, time_t time0);","Difference of two times in seconds.","","time1 - time0 as a double.","time",None),
("gmtime","struct tm *gmtime(const time_t *timer);","Converts a calendar time to UTC broken-down time.","A static structure is used.","A pointer to the structure.","localtime mktime",None),
("localtime","struct tm *localtime(const time_t *timer);","Converts a calendar time to local broken-down time.","A static structure is used.","A pointer to the structure.","gmtime mktime","#include <stdio.h>\n#include <time.h>\n\nint main(void)\n{\n    time_t now = time(NULL);\n    struct tm *t = localtime(&now);\n    printf(\"%04d-%02d-%02d\\n\", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);\n    return 0;\n}"),
("mktime","time_t mktime(struct tm *timeptr);","Converts local broken-down time to calendar time.","The fields are normalized and tm_wday and tm_yday are set.","The calendar time, or (time_t)-1.","localtime",None),
("strftime","size_t strftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr);","Formats a time as text.","Conversions: %a %A weekday, %b %B month, %c date and time, %d day, %H %I hour, %j day of year, %m month, %M minute, %p AM/PM, %S second, %U %W week, %w weekday number, %x date, %X time, %y %Y year, %Z zone, %% percent sign.","The number of characters stored (not counting the null), or 0 if maxsize is too small.","asctime localtime",None),
("time","time_t time(time_t *timer);","Returns the current calendar time.","The value is the number of seconds since 1 January 1970 UTC. If timer is not NULL the value is also stored there.","The time.","difftime clock",None),
]
TYPES = [("struct tm","Broken-down time: int tm_sec, tm_min, tm_hour, tm_mday, tm_mon (0-11), tm_year (since 1900), tm_wday (0-6, Sunday is 0), tm_yday (0-365), tm_isdst."),("time_t","Arithmetic type for calendar times."),("clock_t","Arithmetic type for processor times; CLOCKS_PER_SEC gives the units per second.")]
