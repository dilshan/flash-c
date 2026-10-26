#ifndef _FC_STDIO_H
#define _FC_STDIO_H
#ifndef _FC_SIZE_T
#define _FC_SIZE_T
typedef unsigned long size_t;
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 1024
typedef char *va_list;
typedef struct _FILE {
    int fd, flags;
    int rpos, rlen, wlen, eof, err, unget;
    char rbuf[1024];
    char wbuf[1024];
} FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int scanf(const char *fmt, ...);
int fscanf(FILE *f, const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int fputs(const char *s, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int getchar(void);
int fgetc(FILE *f);
int getc(FILE *f);
int ungetc(int c, FILE *f);
char *gets(char *buf);
char *fgets(char *buf, int n, FILE *f);
int fflush(FILE *f);
FILE *fopen(const char *name, const char *mode);
int fclose(FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int remove(const char *name);
int rename(const char *a, const char *b);
void perror(const char *msg);
#endif
