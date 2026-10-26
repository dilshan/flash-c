#ifndef _FC_VARARGS_H
#define _FC_VARARGS_H
#include <_defs.h>
/* old-style variable argument lists:  f(va_alist) va_dcl { va_list ap; va_start(ap); ... } */
#define va_dcl int va_alist;
#define va_start(ap) ((ap) = (char *)&va_alist)
#define va_arg(ap, type) (*(type *)(((ap) += 8) - 8))
#define va_end(ap) ((void)0)
#endif
