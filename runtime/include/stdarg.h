#ifndef _FC_STDARG_H
#define _FC_STDARG_H
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + 8)
#define va_arg(ap, type) (*(type *)(((ap) += 8) - 8))
#define va_end(ap) ((void)0)
#endif
