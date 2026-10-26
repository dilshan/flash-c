/* internal declarations of the stdio modules */
#ifndef __FCIO_H
#define __FCIO_H
#include <stdio.h>
#define __BUFSZ 4096
int __fl_flush(FILE *f);              /* write out the buffered output */
int __fl_wbuf(FILE *f);               /* make sure f has an output buffer; chooses line/full buffering */
int __fl_fill(FILE *f);               /* refill the input buffer; 0 at end of file or on error */
int __fl_isatty(int fd);
#endif
