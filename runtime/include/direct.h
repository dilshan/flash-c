#ifndef _FC_DIRECT_H
#define _FC_DIRECT_H
#include <dir.h>
int _chdrive(int drive);
char *_getdcwd(int drive, char *buf, int n);
int _getdrive(void);
#endif
