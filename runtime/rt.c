/* Flash C runtime library.
 * Written in Flash C itself and compiled into every program that is built.
 * Talks to the kernel directly with system calls; no external C library is used. */

typedef unsigned long size_t;
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + 8)
#define va_arg(ap, type) (*(type *)(((ap) += 8) - 8))
#define va_end(ap) ((void)0)
#define NULL ((void *)0)
#define EOF (-1)

long __syscall(long n, ...);

/* ------------------------------------------------------------------ */
/* process                                                            */
/* ------------------------------------------------------------------ */
typedef struct _FILE {
    int fd, flags;
    int rpos, rlen, wlen, eof, err, unget;
    char rbuf[1024];
    char wbuf[1024];
} FILE;

#define F_READ 1
#define F_WRITE 2
#define F_LINE 4
#define F_RAW 8
#define F_USED 16

FILE _fc_files[16];
FILE *stdin;
FILE *stdout;
FILE *stderr;

int fflush(FILE *f);
int main();

void _fc_flush_all(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if ((_fc_files[i].flags & F_USED) && _fc_files[i].wlen) fflush(&_fc_files[i]);
}

void exit(int code)
{
    _fc_flush_all();
    __syscall(231, code);
    for (;;) ;
}

void abort(void)
{
    _fc_flush_all();
    __syscall(231, 134);
    for (;;) ;
}

void _fc_start(int argc, char **argv)
{
    stdin = &_fc_files[0]; stdout = &_fc_files[1]; stderr = &_fc_files[2];
    stdin->fd = 0;  stdin->flags = F_READ | F_USED;
    stdout->fd = 1; stdout->flags = F_WRITE | F_LINE | F_USED;
    stderr->fd = 2; stderr->flags = F_WRITE | F_RAW | F_USED;
    exit(main(argc, argv));
}

/* ------------------------------------------------------------------ */
/* strings and memory                                                 */
/* ------------------------------------------------------------------ */
size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

void *memcpy(void *d, const void *s, size_t n)
{
    char *dp = d; const char *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    char *dp = d; const char *sp = s;
    if (dp < sp) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    char *dp = d;
    while (n--) *dp++ = c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++; y++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return NULL;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++) != 0) ;
    return r;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *r = d;
    while (*d) d++;
    while (n-- && *s) *d++ = *s++;
    *d = 0;
    return r;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static int _lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int stricmp(const char *a, const char *b)
{
    while (*a && _lc(*a) == _lc(*b)) { a++; b++; }
    return _lc((unsigned char)*a) - _lc((unsigned char)*b);
}

int strnicmp(const char *a, const char *b, size_t n)
{
    while (n && *a && _lc(*a) == _lc(*b)) { a++; b++; n--; }
    if (!n) return 0;
    return _lc((unsigned char)*a) - _lc((unsigned char)*b);
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (*h == *n && !strncmp(h, n, nl)) return (char *)h;
    return NULL;
}

size_t strspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && strchr(set, s[n])) n++;
    return n;
}

size_t strcspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && !strchr(set, s[n])) n++;
    return n;
}

char *strpbrk(const char *s, const char *set)
{
    for (; *s; s++) if (strchr(set, *s)) return (char *)s;
    return NULL;
}

static char *_tok_save;
char *strtok(char *s, const char *delim)
{
    char *start;
    if (!s) s = _tok_save;
    if (!s) return NULL;
    s += strspn(s, delim);
    if (!*s) { _tok_save = NULL; return NULL; }
    start = s;
    s += strcspn(s, delim);
    if (*s) { *s = 0; _tok_save = s + 1; }
    else _tok_save = NULL;
    return start;
}

char *strupr(char *s)
{
    char *p;
    for (p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    return s;
}

char *strlwr(char *s)
{
    char *p;
    for (p = s; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    return s;
}

char *strrev(char *s)
{
    size_t i = 0, j = strlen(s);
    while (j > i + 1) {
        char t = s[i]; s[i] = s[j - 1]; s[j - 1] = t;
        i++; j--;
    }
    return s;
}

/* ------------------------------------------------------------------ */
/* character classes                                                  */
/* ------------------------------------------------------------------ */
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int ispunct(int c) { return isprint(c) && c != ' ' && !isalnum(c); }
int isascii(int c) { return c >= 0 && c < 128; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* ------------------------------------------------------------------ */
/* memory allocation: a first-fit allocator on top of brk             */
/* ------------------------------------------------------------------ */
struct _blk { size_t size; int used; int pad; struct _blk *next; };
static struct _blk *_heap_head;
static struct _blk *_heap_tail;
static char *_brk_cur;

static void *_sbrk(size_t n)
{
    char *old;
    if (!_brk_cur) _brk_cur = (char *)__syscall(12, 0);
    old = _brk_cur;
    if (__syscall(12, old + n) != (long)(old + n)) return NULL;
    _brk_cur = old + n;
    return old;
}

void *malloc(size_t n)
{
    struct _blk *b;
    if (!n) n = 1;
    n = (n + 15) & ~15UL;
    for (b = _heap_head; b; b = b->next)
        if (!b->used && b->size >= n) {
            if (b->size >= n + 64) {
                struct _blk *r = (struct _blk *)((char *)(b + 1) + n);
                r->size = b->size - n - sizeof(struct _blk);
                r->used = 0; r->next = b->next;
                b->next = r; b->size = n;
                if (_heap_tail == b) _heap_tail = r;
            }
            b->used = 1;
            return b + 1;
        }
    b = (struct _blk *)_sbrk(n + sizeof(struct _blk));
    if (!b) return NULL;
    b->size = n; b->used = 1; b->next = NULL;
    if (_heap_tail) _heap_tail->next = b; else _heap_head = b;
    _heap_tail = b;
    return b + 1;
}

void free(void *p)
{
    struct _blk *b, *n;
    if (!p) return;
    b = (struct _blk *)p - 1;
    b->used = 0;
    n = b->next;
    while (n && !n->used) {
        b->size += n->size + sizeof(struct _blk);
        b->next = n->next;
        if (_heap_tail == n) _heap_tail = b;
        n = b->next;
    }
}

void *calloc(size_t n, size_t sz)
{
    void *p = malloc(n * sz);
    if (p) memset(p, 0, n * sz);
    return p;
}

void *realloc(void *p, size_t n)
{
    struct _blk *b;
    void *q;
    if (!p) return malloc(n);
    b = (struct _blk *)p - 1;
    if (b->size >= n) return p;
    q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    free(p);
    return q;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* stdio                                                              */
/* ------------------------------------------------------------------ */
static int _wr(int fd, const char *p, long n)
{
    long done = 0, r;
    while (done < n) {
        r = __syscall(1, fd, p + done, n - done);
        if (r <= 0) return -1;
        done += r;
    }
    return 0;
}

int fflush(FILE *f)
{
    int r = 0;
    if (!f) { _fc_flush_all(); return 0; }
    if (f->wlen) {
        if (_wr(f->fd, f->wbuf, f->wlen)) { f->err = 1; r = -1; }
        f->wlen = 0;
    }
    return r;
}

int fputc(int c, FILE *f)
{
    if (!(f->flags & F_WRITE)) { f->err = 1; return EOF; }
    if (f->rlen) { __syscall(8, f->fd, f->rpos - f->rlen, 1); f->rpos = f->rlen = 0; }
    f->wbuf[f->wlen++] = c;
    if (f->wlen >= 1024 || (f->flags & F_RAW) || ((f->flags & F_LINE) && c == '\n')) fflush(f);
    return (unsigned char)c;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f)
{
    while (*s) fputc(*s++, f);
    return 0;
}

int puts(const char *s)
{
    fputs(s, stdout);
    fputc('\n', stdout);
    return 0;
}

static int _fill(FILE *f)
{
    long r;
    if (f->eof) return 0;
    if (f == stdin) fflush(stdout);
    if (f->wlen) fflush(f);
    r = __syscall(0, f->fd, f->rbuf, 1024);
    if (r <= 0) { f->eof = 1; if (r < 0) f->err = 1; return 0; }
    f->rpos = 0; f->rlen = r;
    return 1;
}

int fgetc(FILE *f)
{
    if (f->unget) { int c = f->unget - 1; f->unget = 0; return c; }
    if (f->rpos >= f->rlen) {
        f->rpos = f->rlen = 0;
        if (!_fill(f)) return EOF;
    }
    return (unsigned char)f->rbuf[f->rpos++];
}

int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f)
{
    if (c == EOF || f->unget) return EOF;
    f->unget = (unsigned char)c + 1;
    return c;
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

int feof(FILE *f) { return f->eof && f->rpos >= f->rlen; }
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) { f->err = 0; f->eof = 0; }

FILE *fopen(const char *name, const char *mode)
{
    int i, fl = 0, rd = 0, wr = 0, fd;
    FILE *f = NULL;
    for (i = 3; i < 16; i++) if (!(_fc_files[i].flags & F_USED)) { f = &_fc_files[i]; break; }
    if (!f) return NULL;
    if (mode[0] == 'r') { rd = 1; fl = 0; }
    else if (mode[0] == 'w') { wr = 1; fl = 0x241; }
    else if (mode[0] == 'a') { wr = 1; fl = 0x441; }
    else return NULL;
    for (i = 1; mode[i]; i++)
        if (mode[i] == '+') {
            rd = wr = 1;
            fl = (mode[0] == 'r') ? 2 : (mode[0] == 'w' ? 0x242 : 0x442);
        }
    if (rd && !wr) fl = 0;
    fd = __syscall(2, name, fl, 420);
    if (fd < 0) return NULL;
    memset(f, 0, sizeof(FILE));
    f->fd = fd;
    f->flags = F_USED | (rd ? F_READ : 0) | (wr ? F_WRITE : 0);
    return f;
}

int fclose(FILE *f)
{
    int r = fflush(f);
    if (f->fd > 2) __syscall(3, f->fd);
    f->flags = 0;
    return r;
}

size_t fread(void *p, size_t sz, size_t n, FILE *f)
{
    size_t i, total = sz * n, k;
    char *d = p;
    for (k = 0; k < total; k++) {
        int c = fgetc(f);
        if (c == EOF) break;
        d[k] = c;
    }
    return sz ? k / sz : 0;
}

size_t fwrite(const void *p, size_t sz, size_t n, FILE *f)
{
    size_t k, total = sz * n;
    const char *s = p;
    for (k = 0; k < total; k++) fputc(s[k], f);
    return n;
}

int fseek(FILE *f, long off, int whence)
{
    fflush(f);
    if (whence == 1 && f->rlen) off -= (f->rlen - f->rpos);
    f->rpos = f->rlen = 0; f->eof = 0; f->unget = 0;
    return __syscall(8, f->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f)
{
    long p = __syscall(8, f->fd, 0, 1);
    if (p < 0) return -1;
    return p - (f->rlen - f->rpos) + f->wlen;
}

void rewind(FILE *f) { fseek(f, 0, 0); f->err = 0; }
int remove(const char *name) { return __syscall(87, name) < 0 ? -1 : 0; }
int rename(const char *a, const char *b) { return __syscall(82, a, b) < 0 ? -1 : 0; }

/* ------------------------------------------------------------------ */
/* floating point support                                             */
/* ------------------------------------------------------------------ */
double __sqrt(double x);

static long _bits(double x) { return *(long *)&x; }
static double _frombits(long b) { return *(double *)&b; }
static double _zero(void) { return 0.0; }
static double _nan(void) { double z = _zero(); return z / z; }
static double _inf(void) { double z = _zero(); return 1.0 / z; }
int isnan(double x) { return x != x; }
int isinf(double x) { return x - x != x - x && x == x; }

double fabs(double x) { return _frombits(_bits(x) & 0x7FFFFFFFFFFFFFFFL); }
double sqrt(double x) { return x < 0 ? _nan() : __sqrt(x); }

double floor(double x)
{
    double t;
    if (x != x || fabs(x) >= 4503599627370496.0) return x;
    t = (double)(long)x;
    if (t > x) t = t - 1.0;
    return t;
}
double ceil(double x)
{
    double t;
    if (x != x || fabs(x) >= 4503599627370496.0) return x;
    t = (double)(long)x;
    if (t < x) t = t + 1.0;
    return t;
}
double trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double ldexp(double x, int n)
{
    while (n > 1000) { x = x * 1.0715086071862673e301; n -= 1000; }
    while (n < -1000) { x = x * 9.332636185032189e-302; n += 1000; }
    return x * _frombits((long)(n + 1023) << 52);
}
double frexp(double x, int *e)
{
    long b = _bits(x); int ex = (int)((b >> 52) & 0x7FF);
    if (x == 0 || x != x || ex == 0x7FF) { *e = 0; return x; }
    if (ex == 0) { x = x * 18014398509481984.0; b = _bits(x); ex = (int)((b >> 52) & 0x7FF) - 54; }
    *e = ex - 1022;
    return _frombits((b & 0x800FFFFFFFFFFFFFL) | 0x3FE0000000000000L);
}
double modf(double x, double *ip) { double t = trunc(x); *ip = t; return x - t; }
double fmod(double x, double y)
{
    double q;
    if (y == 0 || x != x || y != y) return _nan();
    q = trunc(x / y);
    return x - q * y;
}

double exp(double x)
{
    double n, r, term, sum; int k, i;
    if (x != x) return x;
    if (x > 709.78) return _inf();
    if (x < -745.2) return 0.0;
    n = floor(x * 1.4426950408889634 + 0.5);
    r = (x - n * 0.693145751953125) - n * 1.4286068203094173e-06;
    sum = 1.0; term = 1.0;
    for (i = 1; i < 20; i++) { term = term * r / i; sum = sum + term; }
    k = (int)n;
    return ldexp(sum, k);
}
double log(double x)
{
    int e; double m, s, s2, sum, t; int i;
    if (x != x || x < 0) return _nan();
    if (x == 0) return -_inf();
    m = frexp(x, &e);
    if (m < 0.7071067811865476) { m = m * 2.0; e--; }
    s = (m - 1.0) / (m + 1.0); s2 = s * s;
    sum = 0.0; t = s;
    for (i = 1; i < 40; i += 2) { sum = sum + t / i; t = t * s2; }
    return 2.0 * sum + e * 0.6931471805599453;
}
double log10(double x) { return log(x) / 2.302585092994046; }
double log2(double x) { return log(x) / 0.6931471805599453; }
double pow(double x, double y)
{
    double r = 1.0; long n; int neg = 0;
    if (y == 0) return 1.0;
    if (x == 0) return y > 0 ? 0.0 : _inf();
    if (y == floor(y) && fabs(y) < 1073741824.0) {
        n = (long)y; if (n < 0) { neg = 1; n = -n; }
        while (n) { if (n & 1) r = r * x; x = x * x; n >>= 1; }
        return neg ? 1.0 / r : r;
    }
    if (x < 0) return _nan();
    return exp(y * log(x));
}

static void _sincos_reduce(double x, double *r, int *q)
{
    double k = floor(x * 0.6366197723675814 + 0.5);
    *r = (x - k * 1.5707963267948966) - k * 6.123233995736766e-17;
    *q = (int)(k - 4.0 * floor(k / 4.0));
}
static double _sin_poly(double r)
{
    double r2 = r * r, t = r, sum = r; int i;
    for (i = 1; i < 13; i++) { t = -t * r2 / ((2 * i) * (2 * i + 1)); sum = sum + t; }
    return sum;
}
static double _cos_poly(double r)
{
    double r2 = r * r, t = 1.0, sum = 1.0; int i;
    for (i = 1; i < 13; i++) { t = -t * r2 / ((2 * i - 1) * (2 * i)); sum = sum + t; }
    return sum;
}
double sin(double x)
{
    double r; int q;
    if (x != x) return x;
    _sincos_reduce(x, &r, &q);
    if (q == 0) return _sin_poly(r);
    if (q == 1) return _cos_poly(r);
    if (q == 2) return -_sin_poly(r);
    return -_cos_poly(r);
}
double cos(double x)
{
    double r; int q;
    if (x != x) return x;
    _sincos_reduce(x, &r, &q);
    if (q == 0) return _cos_poly(r);
    if (q == 1) return -_sin_poly(r);
    if (q == 2) return -_cos_poly(r);
    return _sin_poly(r);
}
double tan(double x) { return sin(x) / cos(x); }
double atan(double x)
{
    double r2, t, sum; int i, neg = 0, inv = 0;
    if (x != x) return x;
    if (x < 0) { neg = 1; x = -x; }
    if (x > 1.0) { inv = 1; x = 1.0 / x; }
    x = x / (1.0 + sqrt(1.0 + x * x));
    x = x / (1.0 + sqrt(1.0 + x * x));
    r2 = x * x; t = x; sum = 0.0;
    for (i = 1; i < 40; i += 2) { sum = sum + t / i; t = -t * r2; }
    sum = sum * 4.0;
    if (inv) sum = 1.5707963267948966 - sum;
    return neg ? -sum : sum;
}
double atan2(double y, double x)
{
    if (x > 0) return atan(y / x);
    if (x < 0) return y >= 0 ? atan(y / x) + 3.141592653589793 : atan(y / x) - 3.141592653589793;
    if (y > 0) return 1.5707963267948966;
    if (y < 0) return -1.5707963267948966;
    return 0.0;
}
double asin(double x) { if (x > 1 || x < -1) return _nan(); return atan2(x, sqrt(1.0 - x * x)); }
double acos(double x) { if (x > 1 || x < -1) return _nan(); return 1.5707963267948966 - asin(x); }
double sinh(double x) { double e = exp(x); return (e - 1.0 / e) / 2.0; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) / 2.0; }
double tanh(double x) { double e = exp(2.0 * x); return x > 20 ? 1.0 : (x < -20 ? -1.0 : (e - 1.0) / (e + 1.0)); }
double cbrt(double x) { return x < 0 ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0); }
double hypot(double x, double y) { return sqrt(x * x + y * y); }
double fmax(double a, double b) { return a > b ? a : b; }
double fmin(double a, double b) { return a < b ? a : b; }

/* ----- exact decimal expansion of |x| and the %f %e %g converters ----- */
static int _dec_expand(double x, char *buf, int *ipart)
{
    long bits = _bits(x);
    unsigned long m = (unsigned long)bits & 0xFFFFFFFFFFFFFUL;
    int e = (int)((bits >> 52) & 0x7FF), n = 0, i, k;
    if (e == 0) e = 1; else m = m | (1UL << 52);
    e = e - 1075;
    if (e >= 0) {
        unsigned long limb[40]; int nl = 0; unsigned long carry, t;
        while (m) { limb[nl++] = m % 1000000000UL; m = m / 1000000000UL; }
        if (!nl) limb[nl++] = 0;
        for (i = 0; i < e; i++) {
            carry = 0;
            for (k = 0; k < nl; k++) { t = limb[k] * 2 + carry; limb[k] = t % 1000000000UL; carry = t / 1000000000UL; }
            if (carry) limb[nl++] = carry;
        }
        { char tmp[12]; int j;
          for (k = nl - 1; k >= 0; k--) {
              t = limb[k];
              for (j = 8; j >= 0; j--) { tmp[j] = '0' + t % 10; t = t / 10; }
              for (j = 0; j < 9; j++) {
                  if (n == 0 && tmp[j] == '0' && !(k == 0 && j == 8)) continue;
                  buf[n++] = tmp[j];
              }
          }
        }
        *ipart = n;
        return n;
    } else {
        unsigned long ip, frac, a[40]; int nl, pos, d, b; unsigned long carry, t;
        k = -e;
        if (k >= 64) { ip = 0; frac = m; }
        else { ip = m >> k; frac = m & ((1UL << k) - 1); }
        { char tmp[24]; int j = 0;
          if (!ip) tmp[j++] = '0';
          while (ip) { tmp[j++] = '0' + ip % 10; ip = ip / 10; }
          while (j) buf[n++] = tmp[--j];
        }
        *ipart = n;
        nl = 0;
        a[0] = frac & 0xFFFFFFFFUL; a[1] = frac >> 32; nl = 2;
        for (;;) {
            int nz = 0;
            for (i = 0; i < nl; i++) if (a[i]) nz = 1;
            if (!nz) break;
            carry = 0;
            for (i = 0; i < nl; i++) { t = a[i] * 10 + carry; a[i] = t & 0xFFFFFFFFUL; carry = t >> 32; }
            if (carry) a[nl++] = carry;
            d = 0;
            for (b = 0; b < 4; b++) {
                pos = k + b;
                if ((pos >> 5) < nl && ((a[pos >> 5] >> (pos & 31)) & 1)) { d = d | (1 << b); a[pos >> 5] = a[pos >> 5] & ~(1UL << (pos & 31)); }
            }
            buf[n++] = '0' + d;
        }
        return n;
    }
}

/* round the digit string to 'keep' digits (round half to even on the exact value); returns 1 on carry out */
static int _round_digits(char *d, int n, int keep)
{
    int up = 0, i;
    if (keep >= n) return 0;
    if (keep < 0) return 0;
    if (d[keep] > '5') up = 1;
    else if (d[keep] == '5') {
        int nz = 0;
        for (i = keep + 1; i < n; i++) if (d[i] != '0') nz = 1;
        up = nz ? 1 : (keep > 0 && ((d[keep - 1] - '0') & 1));
    }
    if (up) {
        i = keep - 1;
        while (i >= 0 && d[i] == '9') { d[i] = '0'; i--; }
        if (i >= 0) d[i]++;
        else return 1;
    }
    return 0;
}

static char _fdig[1700];

/* converts |x| according to conv ('f', 'e' or 'g'); returns the length written to out */
static int _fmt_double(char *out, double x, int conv, int prec, int alt, int upper)
{
    int n, ip, o = 0, i, f, expo, p, keep, carry, usef;
    if (x != x) { strcpy(out, upper ? "NAN" : "nan"); return 3; }
    if (isinf(x)) { strcpy(out, upper ? "INF" : "inf"); return 3; }
    if (prec < 0) prec = 6;
    if (prec > 340) prec = 340;
    n = _dec_expand(x, _fdig, &ip);
    f = 0;
    while (f < n && _fdig[f] == '0') f++;
    if (f == n) { _fdig[0] = '0'; n = 1; ip = 1; f = 0; expo = 0; }
    else expo = ip - 1 - f;
    usef = conv == 'f';
    if (conv == 'g') {
        p = prec == 0 ? 1 : prec;
        if (f < n) {
            char t2[1700]; memcpy(t2, _fdig, n);
            carry = _round_digits(t2, n, f + p);
            if (carry) { expo = expo + 1; }
        }
        if (expo < -4 || expo >= p) { conv = 'e'; prec = p - 1; }
        else { usef = 1; prec = p - 1 - expo; }
    }
    if (usef) {
        keep = ip + prec;
        carry = _round_digits(_fdig, n, keep);
        if (carry) { memmove(_fdig + 1, _fdig, n); _fdig[0] = '1'; n++; ip++; }
        for (i = 0; i < ip; i++) out[o++] = i < n ? _fdig[i] : '0';
        if (prec > 0 || alt) out[o++] = '.';
        for (i = 0; i < prec; i++) out[o++] = (ip + i) < n ? _fdig[ip + i] : '0';
    } else {
        char *d = _fdig + f; int dn = n - f;
        if (f == n - 0 && _fdig[0] == '0' && n == 1) { d = _fdig; dn = 1; }
        carry = _round_digits(d, dn, prec + 1);
        if (carry) { memmove(d + 1, d, dn); d[0] = '1'; dn++; expo = expo + 1; }
        out[o++] = dn > 0 ? d[0] : '0';
        if (prec > 0 || alt) out[o++] = '.';
        for (i = 1; i <= prec; i++) out[o++] = i < dn ? d[i] : '0';
        out[o++] = upper ? 'E' : 'e';
        if (expo < 0) { out[o++] = '-'; expo = -expo; } else out[o++] = '+';
        if (expo >= 100) { out[o++] = '0' + expo / 100; expo = expo % 100; }
        out[o++] = '0' + expo / 10; out[o++] = '0' + expo % 10;
    }
    if (conv == 'g' || (conv == 'e' && 0)) { }
    out[o] = 0;
    return o;
}

static int _strip_zeros(char *s, int len)
{
    int i, hasdot = 0, epos = len;
    for (i = 0; i < len; i++) { if (s[i] == '.') hasdot = 1; if (s[i] == 'e' || s[i] == 'E') { epos = i; break; } }
    if (!hasdot) return len;
    i = epos;
    while (i > 0 && s[i - 1] == '0') i--;
    if (i > 0 && s[i - 1] == '.') i--;
    memmove(s + i, s + epos, len - epos + 1);
    return i + (len - epos);
}

/* string to double: decimal only, correctly rounded for up to 15 significant digits and moderate exponents */
static double _strtod(const char *s, char **end)
{
    double v; unsigned long m = 0; int e10 = 0, nd = 0, neg = 0, seen = 0, ex = 0, eneg = 0, i;
    const char *p = s;
    while (isspace(*p)) p++;
    if (*p == '-' || *p == '+') { neg = *p == '-'; p++; }
    while (*p >= '0' && *p <= '9') { seen = 1; if (nd < 19) { m = m * 10 + (*p - '0'); if (m) nd++; } else e10++; p++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { seen = 1; if (nd < 19) { m = m * 10 + (*p - '0'); if (m) nd++; e10--; } p++; }
    }
    if (!seen) { if (end) *end = (char *)s; return 0.0; }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        if (*q == '-' || *q == '+') { eneg = *q == '-'; q++; }
        if (*q >= '0' && *q <= '9') { while (*q >= '0' && *q <= '9') { ex = ex * 10 + (*q - '0'); if (ex > 9999) ex = 9999; q++; } p = q; e10 = e10 + (eneg ? -ex : ex); }
    }
    if (end) *end = (char *)p;
    v = (double)m;
    if (e10 < 0) {
        int n = -e10; double pw = 1.0;
        while (n > 22) { v = v / 1e22; n -= 22; }
        for (i = 0; i < n; i++) pw = pw * 10.0;
        v = v / pw;
    } else if (e10 > 0) {
        int n = e10; double pw = 1.0;
        while (n > 22) { v = v * 1e22; n -= 22; }
        for (i = 0; i < n; i++) pw = pw * 10.0;
        v = v * pw;
    }
    return neg ? -v : v;
}
double strtod(const char *s, char **end) { return _strtod(s, end); }
double atof(const char *s) { return _strtod(s, NULL); }

/* ----- formatted output ----- */
struct _sink { char *buf; long cap; long n; FILE *fp; };

static void _put(struct _sink *s, int c)
{
    if (s->fp) fputc(c, s->fp);
    else {
        if (s->n + 1 < s->cap) s->buf[s->n] = c;
        s->n++;
    }
}

static void _pad(struct _sink *s, int c, int n)
{
    while (n-- > 0) _put(s, c);
}

static int _vfmt(struct _sink *s, const char *f, va_list ap)
{
    char tmp[72];
    const char *digits;
    while (*f) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1, lng = 0;
        int c, base, neg, len, ndig, plen;
        unsigned long u;
        char *p;
        char prefix[4];
        if (*f != '%') { _put(s, *f++); continue; }
        f++;
        for (;;) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1;
            else if (*f == '#') alt = 1;
            else break;
            f++;
        }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
        }
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'L') { if (*f == 'l' || *f == 'z') lng++; f++; }
        c = *f;
        if (!c) break;
        f++;
        digits = "0123456789abcdef";
        switch (c) {
        case '%': _put(s, '%'); break;
        case 'c': {
            int ch = va_arg(ap, int);
            if (!left) _pad(s, ' ', width - 1);
            _put(s, ch);
            if (left) _pad(s, ' ', width - 1);
            break;
        }
        case 's': {
            char *str = va_arg(ap, char *);
            if (!str) str = "(null)";
            len = 0;
            while (str[len] && (prec < 0 || len < prec)) len++;
            if (!left) _pad(s, ' ', width - len);
            for (ndig = 0; ndig < len; ndig++) _put(s, str[ndig]);
            if (left) _pad(s, ' ', width - len);
            break;
        }
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            neg = 0; plen = 0; base = 10;
            if (c == 'x' || c == 'X' || c == 'p') base = 16;
            if (c == 'o') base = 8;
            if (c == 'X') digits = "0123456789ABCDEF";
            if (c == 'p') { u = (unsigned long)va_arg(ap, void *); alt = 1; }
            else if (c == 'd' || c == 'i') {
                long v;
                if (lng) v = va_arg(ap, long); else v = va_arg(ap, int);
                if (v < 0) { neg = 1; u = -v; } else u = v;
            } else {
                if (lng) u = va_arg(ap, unsigned long); else u = va_arg(ap, unsigned int);
            }
            p = tmp + 72; ndig = 0;
            if (u == 0 && prec == 0) { }
            else do { *--p = digits[u % base]; u /= base; ndig++; } while (u);
            if (neg) prefix[plen++] = '-';
            else if (plus && (c == 'd' || c == 'i')) prefix[plen++] = '+';
            else if (space && (c == 'd' || c == 'i')) prefix[plen++] = ' ';
            if (alt && base == 16 && (ndig || c == 'p')) { prefix[plen++] = '0'; prefix[plen++] = (c == 'X') ? 'X' : 'x'; }
            if (alt && base == 8 && (!ndig || *p != '0')) { *--p = '0'; ndig++; }
            len = ndig; if (prec > len) len = prec;
            if (prec >= 0) zero = 0;
            if (!left && !zero) _pad(s, ' ', width - len - plen);
            { int i; for (i = 0; i < plen; i++) _put(s, prefix[i]); }
            if (!left && zero) _pad(s, '0', width - len - plen);
            _pad(s, '0', len - ndig);
            { int i; for (i = 0; i < ndig; i++) _put(s, p[i]); }
            if (left) _pad(s, ' ', width - len - plen);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(ap, double);
            char nb[1800]; int neg = 0, len, pl = 0, lc = tolower(c);
            if (_bits(v) < 0) { neg = 1; v = -v; }
            len = _fmt_double(nb, v, lc, prec, alt, c == 'E' || c == 'G' || c == 'F');
            if (lc == 'g' && !alt) len = _strip_zeros(nb, len);
            if (neg) prefix[pl++] = '-';
            else if (plus) prefix[pl++] = '+';
            else if (space) prefix[pl++] = ' ';
            if (v != v || isinf(v)) zero = 0;
            if (!left && !zero) _pad(s, ' ', width - len - pl);
            { int i; for (i = 0; i < pl; i++) _put(s, prefix[i]); }
            if (!left && zero) _pad(s, '0', width - len - pl);
            { int i; for (i = 0; i < len; i++) _put(s, nb[i]); }
            if (left) _pad(s, ' ', width - len - pl);
            break;
        }
        default:
            _put(s, '%'); _put(s, c);
        }
    }
    return 0;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct _sink s;
    s.buf = NULL; s.cap = 0; s.n = 0; s.fp = f;
    _vfmt(&s, fmt, ap);
    return 0;
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct _sink s;
    s.buf = buf; s.cap = n; s.n = 0; s.fp = NULL;
    _vfmt(&s, fmt, ap);
    if (n) buf[s.n < (long)n ? s.n : (long)n - 1] = 0;
    return s.n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, 1L << 30, fmt, ap); }

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vfprintf(f, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vsnprintf(buf, 1L << 30, fmt, ap);
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return vsnprintf(buf, n, fmt, ap);
}

void perror(const char *msg)
{
    if (msg && *msg) { fputs(msg, stderr); fputs(": ", stderr); }
    fputs("error\n", stderr);
}

/* ----- formatted input ----- */
struct _src { const char *s; FILE *fp; int pos; int pushed; };

static int _sgetc(struct _src *r)
{
    if (r->pushed != -2) { int c = r->pushed; r->pushed = -2; return c; }
    if (r->fp) return fgetc(r->fp);
    if (!r->s[r->pos]) return EOF;
    return (unsigned char)r->s[r->pos++];
}

static void _sunget(struct _src *r, int c) { r->pushed = c; }

static int _vscan1(struct _src *r, const char *f, va_list ap)
{
    int count = 0, c;
    r->pushed = -2;
    while (*f) {
        int lng = 0, suppress = 0, width = 0;
        if (isspace(*f)) {
            do { c = _sgetc(r); } while (c != EOF && isspace(c));
            _sunget(r, c);
            f++;
            continue;
        }
        if (*f != '%') {
            c = _sgetc(r);
            if (c != *f) { _sunget(r, c); return count ? count : (c == EOF ? EOF : 0); }
            f++;
            continue;
        }
        f++;
        if (*f == '*') { suppress = 1; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l' || *f == 'h') { if (*f == 'l') lng++; f++; }
        if (*f != 'c' && *f != '[' && *f != '%') {
            do { c = _sgetc(r); } while (c != EOF && isspace(c));
            _sunget(r, c);
        }
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'o': {
            long v = 0; int neg = 0, base = 10, n = 0;
            if (*f == 'x') base = 16;
            if (*f == 'o') base = 8;
            c = _sgetc(r);
            if (c == '-' || c == '+') { neg = c == '-'; c = _sgetc(r); }
            if (base == 16 && c == '0') {
                int c2 = _sgetc(r);
                if (c2 == 'x' || c2 == 'X') c = _sgetc(r); else { _sunget(r, c2); }
            }
            while (c != EOF && (!width || n < width)) {
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else break;
                if (d >= base) break;
                v = v * base + d; n++;
                c = _sgetc(r);
            }
            _sunget(r, c);
            if (!n) return count ? count : (c == EOF ? EOF : 0);
            if (neg) v = -v;
            if (!suppress) {
                if (lng) *va_arg(ap, long *) = v; else *va_arg(ap, int *) = v;
                count++;
            }
            break;
        }
        case 'f': case 'e': case 'g': case 'E': case 'G': {
            char fb[64]; int n = 0; double v;
            c = _sgetc(r);
            while (c != EOF && n < 62 && (isdigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E') && (!width || n < width)) { fb[n++] = c; c = _sgetc(r); }
            _sunget(r, c);
            fb[n] = 0;
            if (!n) return count ? count : (c == EOF ? EOF : 0);
            v = _strtod(fb, NULL);
            if (!suppress) {
                if (lng) *va_arg(ap, double *) = v; else *va_arg(ap, float *) = (float)v;
                count++;
            }
            break;
        }
        case 's': {
            char *d = suppress ? NULL : va_arg(ap, char *);
            int n = 0;
            c = _sgetc(r);
            if (c == EOF) return count ? count : EOF;
            while (c != EOF && !isspace(c) && (!width || n < width)) { if (d) d[n] = c; n++; c = _sgetc(r); }
            _sunget(r, c);
            if (d) { d[n] = 0; count++; }
            break;
        }
        case 'c': {
            char *d = suppress ? NULL : va_arg(ap, char *);
            c = _sgetc(r);
            if (c == EOF) return count ? count : EOF;
            if (d) { *d = c; count++; }
            break;
        }
        case '%':
            c = _sgetc(r);
            if (c != '%') { _sunget(r, c); return count; }
            break;
        default:
            return count;
        }
        f++;
    }
    return count;
}

static int _vscan(struct _src *r, const char *f, va_list ap)
{
    int n = _vscan1(r, f, ap);
    if (r->fp && r->pushed != -2 && r->pushed != EOF) ungetc(r->pushed, r->fp);   /* give back the look-ahead character */
    return n;
}

int scanf(const char *fmt, ...)
{
    struct _src r; va_list ap;
    r.s = NULL; r.fp = stdin; r.pos = 0;
    va_start(ap, fmt);
    return _vscan(&r, fmt, ap);
}

int fscanf(FILE *f, const char *fmt, ...)
{
    struct _src r; va_list ap;
    r.s = NULL; r.fp = f; r.pos = 0;
    va_start(ap, fmt);
    return _vscan(&r, fmt, ap);
}

int sscanf(const char *str, const char *fmt, ...)
{
    struct _src r; va_list ap;
    r.s = str; r.fp = NULL; r.pos = 0;
    va_start(ap, fmt);
    return _vscan(&r, fmt, ap);
}

/* ------------------------------------------------------------------ */
/* stdlib                                                             */
/* ------------------------------------------------------------------ */
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

long strtol(const char *s, char **end, int base)
{
    long v = 0; int neg = 0, d;
    while (isspace(*s)) s++;
    if (*s == '-' || *s == '+') { neg = *s == '-'; s++; }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtol(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

static unsigned long _rand_seed = 1;
void srand(unsigned int s) { _rand_seed = s; }
int rand(void)
{
    _rand_seed = (_rand_seed * 1103515245UL + 12345UL) & 0xffffffffUL;
    return (int)((_rand_seed >> 16) & 0x7fff);
}

char *ltoa(long v, char *buf, int base)
{
    char tmp[72]; int i = 0, j = 0, neg = 0;
    unsigned long u;
    if (v < 0 && base == 10) { neg = 1; u = -v; } else u = v;
    if (!u) tmp[i++] = '0';
    while (u) { int d = u % base; tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10; u /= base; }
    if (neg) buf[j++] = '-';
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

char *itoa(int v, char *buf, int base) { return ltoa(v, buf, base); }

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    char *a = base, *tmp;
    size_t i, j, k;
    if (n < 2) return;
    tmp = malloc(sz);
    for (i = 1; i < n; i++) {
        memcpy(tmp, a + i * sz, sz);
        j = i;
        while (j > 0 && cmp(a + (j - 1) * sz, tmp) > 0) {
            for (k = 0; k < sz; k++) a[j * sz + k] = a[(j - 1) * sz + k];
            j--;
        }
        memcpy(a + j * sz, tmp, sz);
    }
    free(tmp);
}

void *bsearch(const void *key, const void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const char *p = (const char *)base + mid * sz;
        int r = cmp(key, p);
        if (r == 0) return (void *)p;
        if (r < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* console and timing (conio.h / dos.h flavoured helpers)             */
/* ------------------------------------------------------------------ */
static char _tio_save[64];
static int _tio_raw;

static void _raw_on(int echo)
{
    char t[64]; int i;
    fflush(stdout);
    __syscall(16, 0, 0x5401, _tio_save);
    for (i = 0; i < 60; i++) t[i] = _tio_save[i];
    t[12] = t[12] & ~(2 | (echo ? 0 : 8));
    t[23] = 1; t[22] = 0;      /* VMIN = 1, VTIME = 0 */
    __syscall(16, 0, 0x5402, t);
    _tio_raw = 1;
}

static void _raw_off(void)
{
    if (_tio_raw) __syscall(16, 0, 0x5402, _tio_save);
    _tio_raw = 0;
}

int getch(void)
{
    char c = 0; long r;
    _raw_on(0);
    r = __syscall(0, 0, &c, 1);
    _raw_off();
    return r == 1 ? (unsigned char)c : EOF;
}

int getche(void)
{
    char c = 0; long r;
    _raw_on(1);
    r = __syscall(0, 0, &c, 1);
    _raw_off();
    return r == 1 ? (unsigned char)c : EOF;
}

int putch(int c) { fputc(c, stdout); fflush(stdout); return c; }
int cputs(const char *s) { fputs(s, stdout); fflush(stdout); return 0; }
void clrscr(void) { fputs("\033[2J\033[H", stdout); fflush(stdout); }
void clreol(void) { fputs("\033[K", stdout); fflush(stdout); }
void gotoxy(int x, int y) { printf("\033[%d;%dH", y, x); fflush(stdout); }

void delay(unsigned int ms)
{
    long ts[2];
    fflush(stdout);
    ts[0] = ms / 1000;
    ts[1] = (long)(ms % 1000) * 1000000L;
    __syscall(35, ts, 0);
}

unsigned int sleep(unsigned int sec) { delay(sec * 1000); return 0; }

int strcmpi(const char *a, const char *b) { return stricmp(a, b); }
