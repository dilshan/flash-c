/*
 * File:    heap.c
 * Purpose: heap - dynamic memory: a first-fit allocator on top of brk
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdlib.h>
#include <string.h>
#include <alloc.h>
#include <errno.h>
#include <_fcrt.h>

struct __blk { size_t size; int used; int magic; struct __blk *next; };
#define __BLK_MAGIC 0x46434C4B
static struct __blk *__heap_head;
static struct __blk *__heap_tail;
static char *__brk_cur;

void *sbrk(long incr)
{
    char *old;
    if (!__brk_cur) __brk_cur = (char *)__syscall(SYS_brk, 0);
    old = __brk_cur;
    if (!incr) return old;
    if (__syscall(SYS_brk, old + incr) != (long)(old + incr)) return (void *)-1;
    __brk_cur = old + incr;
    return old;
}

int brk(void *addr)
{
    if (__syscall(SYS_brk, addr) != (long)addr) { errno = ENOMEM; return -1; }
    __brk_cur = addr;
    return 0;
}

void *malloc(size_t n)
{
    struct __blk *b;
    if (!n) n = 1;
    n = (n + 15) & ~15UL;
    for (b = __heap_head; b; b = b->next)
        if (!b->used && b->size >= n) {
            if (b->size >= n + 64) {
                struct __blk *r = (struct __blk *)((char *)(b + 1) + n);
                r->size = b->size - n - sizeof(struct __blk);
                r->used = 0; r->magic = __BLK_MAGIC; r->next = b->next;
                b->next = r; b->size = n;
                if (__heap_tail == b) __heap_tail = r;
            }
            b->used = 1;
            return b + 1;
        }
    b = (struct __blk *)sbrk(n + sizeof(struct __blk));
    if ((long)b == -1) { errno = ENOMEM; return NULL; }
    b->size = n; b->used = 1; b->magic = __BLK_MAGIC; b->next = NULL;
    if (__heap_tail) __heap_tail->next = b; else __heap_head = b;
    __heap_tail = b;
    return b + 1;
}

void free(void *p)
{
    struct __blk *b, *n;
    if (!p) return;
    b = (struct __blk *)p - 1;
    if (b->magic != __BLK_MAGIC) return;
    b->used = 0;
    n = b->next;
    while (n && !n->used) {
        b->size += n->size + sizeof(struct __blk);
        b->next = n->next;
        if (__heap_tail == n) __heap_tail = b;
        n = b->next;
    }
}

void *calloc(size_t n, size_t sz)
{
    void *p;
    if (sz && n > (~0UL) / sz) { errno = ENOMEM; return NULL; }
    p = malloc(n * sz);
    if (p) memset(p, 0, n * sz);
    return p;
}

void *realloc(void *p, size_t n)
{
    struct __blk *b;
    void *q;
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    b = (struct __blk *)p - 1;
    if (b->size >= n) return p;
    q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    free(p);
    return q;
}

/* free memory the system could still give to the heap, in bytes */
unsigned long coreleft(void)
{
    long si[14];                               /* struct sysinfo: uptime, loads[3], totalram, freeram, ... mem_unit */
    unsigned long free_bytes = 0;
    struct __blk *b;
    if (__syscall(99, si) == 0) free_bytes = (unsigned long)si[5] * (unsigned long)(((int *)si)[26] ? ((int *)si)[26] : 1);
    for (b = __heap_head; b; b = b->next) if (!b->used) free_bytes += b->size;
    return free_bytes;
}

int heapcheck(void)
{
    struct __blk *b;
    if (!__heap_head) return _HEAPEMPTY;
    for (b = __heap_head; b; b = b->next) if (b->magic != __BLK_MAGIC) return -1;
    return _HEAPOK;
}

int heapchecknode(void *node)
{
    struct __blk *b = (struct __blk *)node - 1;
    if (!node || b->magic != __BLK_MAGIC) return -1;
    return b->used ? _USEDENTRY : _FREEENTRY;
}

int heapfillfree(unsigned int fill)
{
    struct __blk *b;
    int r = heapcheck();
    if (r < 0) return r;
    for (b = __heap_head; b; b = b->next) if (!b->used) memset(b + 1, fill, b->size);
    return _HEAPOK;
}

int heapcheckfree(unsigned int fill)
{
    struct __blk *b;
    int r = heapcheck();
    if (r < 0) return r;
    for (b = __heap_head; b; b = b->next)
        if (!b->used) {
            unsigned char *q = (unsigned char *)(b + 1); size_t i;
            for (i = 0; i < b->size; i++) if (q[i] != (unsigned char)fill) return -2;
        }
    return _HEAPOK;
}

int heapwalk(struct heapinfo *hi)
{
    struct __blk *b;
    if (!__heap_head) return _HEAPEMPTY;
    if (!hi->ptr) b = __heap_head;
    else {
        b = (struct __blk *)hi->ptr - 1;
        if (b->magic != __BLK_MAGIC) return -1;
        b = b->next;
        if (!b) return _HEAPEND;
    }
    hi->ptr = b + 1; hi->size = b->size; hi->in_use = b->used;
    return _HEAPOK;
}
