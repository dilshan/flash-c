#ifndef _FC_BIOS_H
#define _FC_BIOS_H
/* the few BIOS services that make sense on a terminal */
long biostime(int cmd, long newtime);
int bioskey(int cmd);
int biosequip(void);
int biosmemory(void);
#endif
