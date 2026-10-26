/* getc - character and line input */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <_fcrt.h>
#include <_fcio.h>

int fgetc(FILE *f)
{
    if (!(f->flags & _F_READ)) { f->err = 1; errno = EBADF; return EOF; }
    if (f->unget) { int c = f->unget - 1; f->unget = 0; return c; }
    if (f->rpos >= f->rlen) {
        f->rpos = f->rlen = 0;
        if (!__fl_fill(f)) return EOF;
    }
    return (unsigned char)f->rbuf[f->rpos++];
}

int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }
int fgetchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f)
{
    if (c == EOF || f->unget || !(f->flags & _F_READ)) return EOF;
    f->unget = (unsigned char)c + 1;
    f->eof = 0;
    return (unsigned char)c;
}

char *fgets(char *buf, int n, FILE *f)
{
    int i = 0, c;
    if (n <= 0) return NULL;
    while (i < n - 1) {
        c = fgetc(f);
        if (c == EOF) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    if (!i) return NULL;
    buf[i] = 0;
    return buf;
}

char *gets(char *buf)
{
    int i = 0, c;
    for (;;) {
        c = fgetc(stdin);
        if (c == EOF || c == '\n') break;
        buf[i++] = c;
    }
    if (c == EOF && !i) return NULL;
    buf[i] = 0;
    return buf;
}

int getw(FILE *f)
{
    int w;
    return fread(&w, sizeof w, 1, f) == 1 ? w : EOF;
}
