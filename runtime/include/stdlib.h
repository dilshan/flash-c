#ifndef _FC_STDLIB_H
#define _FC_STDLIB_H
#ifndef _FC_SIZE_T
#define _FC_SIZE_T
typedef unsigned long size_t;
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int code);
void abort(void);
int abs(int v);
long labs(long v);
double atof(const char *s);
double strtod(const char *s, char **end);
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int rand(void);
void srand(unsigned int seed);
char *itoa(int v, char *buf, int base);
char *ltoa(long v, char *buf, int base);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
#endif
