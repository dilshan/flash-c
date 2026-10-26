#ifndef _FC_STRING_H
#define _FC_STRING_H
#ifndef _FC_SIZE_T
#define _FC_SIZE_T
typedef unsigned long size_t;
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
size_t strlen(const char *s);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int stricmp(const char *a, const char *b);
int strnicmp(const char *a, const char *b, size_t n);
int strcmpi(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
size_t strspn(const char *s, const char *set);
size_t strcspn(const char *s, const char *set);
char *strpbrk(const char *s, const char *set);
char *strtok(char *s, const char *delim);
char *strdup(const char *s);
char *strupr(char *s);
char *strlwr(char *s);
char *strrev(char *s);
#endif
