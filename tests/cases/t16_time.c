#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
int main(void)
{
    time_t t = 1000000000L; struct tm *g, tm; char b[100];
    g = gmtime(&t);
    printf("%d-%02d-%02d %02d:%02d:%02d wd=%d yd=%d\n", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday, g->tm_hour, g->tm_min, g->tm_sec, g->tm_wday, g->tm_yday);
    printf("%s", asctime(g));
    strftime(b, sizeof b, "%Y/%m/%d %H:%M:%S %a %A %b %B %j %y %p %I", g); printf("%s\n", b);
    memset(&tm, 0, sizeof tm); tm.tm_year = 100; tm.tm_mon = 1; tm.tm_mday = 29; tm.tm_hour = 12;
    t = mktime(&tm); printf("%ld %d\n", t, tm.tm_wday);
    t = 0; g = gmtime(&t); printf("%s", asctime(g));
    t = 951782400L; g = gmtime(&t); printf("%s", asctime(g));
    printf("%d\n", (int)difftime(2000, 1000));
    t = time(NULL); printf("%d\n", t > 1700000000L);
    return 0;
}
