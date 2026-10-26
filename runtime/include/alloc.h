#ifndef _FC_ALLOC_H
#define _FC_ALLOC_H
#include <stdlib.h>
#define _HEAPEMPTY 1
#define _HEAPOK 2
#define _HEAPEND 5
#define _FREEENTRY 3
#define _USEDENTRY 4
struct heapinfo {
    void *ptr;
    unsigned long size;
    int in_use;
};
int brk(void *addr);
void *sbrk(long incr);
unsigned long coreleft(void);
int heapcheck(void);
int heapcheckfree(unsigned int fill);
int heapchecknode(void *node);
int heapfillfree(unsigned int fill);
int heapwalk(struct heapinfo *hi);
#endif
