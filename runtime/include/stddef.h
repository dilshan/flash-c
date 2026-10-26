#ifndef _FC_STDDEF_H
#define _FC_STDDEF_H
#include <_defs.h>
#define offsetof(type, member) ((size_t)&(((type *)0)->member))
#endif
