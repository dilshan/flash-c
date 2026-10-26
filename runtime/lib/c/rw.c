/* rw - block input and output */
#include <stdio.h>
#include <string.h>
#include <_fcrt.h>
#include <_fcio.h>

size_t fread(void *p, size_t sz, size_t n, FILE *f)
{
    size_t total = sz * n, got = 0;
    char *d = p;
    if (!total) return 0;
    if (!(f->flags & _F_READ)) { f->err = 1; return 0; }
    if (f->unget) { d[got++] = (char)(f->unget - 1); f->unget = 0; }
    while (got < total) {
        size_t avail = f->rlen - f->rpos, want = total - got;
        if (avail == 0) {
            f->rpos = f->rlen = 0;
            if (want >= __BUFSZ && !(f->flags & _F_RAW)) {          /* large request: read straight into the caller's block */
                long r;
                if (f->fd == 0) fflush(stdout);
                if (f->wlen) __fl_flush(f);
                r = __syscall(SYS_read, f->fd, d + got, want);
                if (r <= 0) { f->eof = 1; if (r < 0) f->err = 1; break; }
                got += r;
                continue;
            }
            if (!__fl_fill(f)) break;
            continue;
        }
        if (avail > want) avail = want;
        memcpy(d + got, f->rbuf + f->rpos, avail);
        f->rpos += (int)avail; got += avail;
    }
    return got / sz;
}

size_t fwrite(const void *p, size_t sz, size_t n, FILE *f)
{
    size_t total = sz * n, done = 0;
    const char *s = p;
    if (!total) return 0;
    if (!(f->flags & _F_WRITE)) { f->err = 1; return 0; }
    if (f->rlen > f->rpos) __syscall(SYS_lseek, f->fd, -(long)(f->rlen - f->rpos), 1);
    f->rpos = f->rlen = 0; f->unget = 0;
    if (!__fl_wbuf(f)) {
        size_t k;
        for (k = 0; k < total; k++) if (fputc(s[k], f) == EOF) return k / sz;
        return n;
    }
    if ((f->flags & _F_RAW) || (f->flags & _F_LINE)) {            /* unbuffered and line-buffered streams go through fputc */
        size_t k;
        for (k = 0; k < total; k++) if (fputc((unsigned char)s[k], f) == EOF) return k / sz;
        return n;
    }
    while (done < total) {
        size_t room = __BUFSZ - f->wlen, left = total - done;
        if (f->wlen == 0 && left >= __BUFSZ) {                      /* large block: write it directly */
            long r = __syscall(SYS_write, f->fd, s + done, left);
            if (r <= 0) { f->err = 1; break; }
            done += r;
            continue;
        }
        if (room > left) room = left;
        memcpy(f->wbuf + f->wlen, s + done, room);
        f->wlen += (int)room; done += room;
        if (f->wlen >= __BUFSZ && __fl_flush(f)) break;
    }
    return done / sz;
}
