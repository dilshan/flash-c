/* Flash C - arena allocator, string interning, diagnostics */
#include "fci.h"

typedef struct Chunk { struct Chunk *next; size_t used, cap; } Chunk;
static Chunk *chunks;

void *xalloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (!chunks || chunks->used + n > chunks->cap) {
        size_t cap = n + sizeof(Chunk) > (1u << 20) ? n + sizeof(Chunk) : (1u << 20);
        Chunk *c = calloc(1, cap);
        if (!c) { fprintf(stderr, "out of memory\n"); exit(2); }
        c->cap = cap; c->used = (sizeof(Chunk) + 15) & ~15u; c->next = chunks; chunks = c;
    }
    void *p = (char *)chunks + chunks->used;
    chunks->used += n;
    return p;
}

char *xstrndup(const char *s, int n)
{
    char *p = xalloc(n + 1);
    memcpy(p, s, n); p[n] = 0;
    return p;
}
char *xstrdup(const char *s) { return xstrndup(s, (int)strlen(s)); }

/* intern table (arena-backed, reset with arena) */
static char **itab; static int icap;
static unsigned hash(const char *s, int n)
{
    unsigned h = 2166136261u;
    while (n--) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}
static int icount;
char *intern(const char *s, int n)
{
    if (!itab) { icap = 4096; itab = calloc(icap, sizeof(char *)); }
    unsigned h = hash(s, n) & (icap - 1);
    while (itab[h]) {
        if ((int)strlen(itab[h]) == n && !memcmp(itab[h], s, n)) return itab[h];
        h = (h + 1) & (icap - 1);
    }
    /* grow at 50% */
    if (++icount * 2 > icap) {
        char **old = itab; int oc = icap;
        icap *= 2; itab = calloc(icap, sizeof(char *));
        for (int i = 0; i < oc; i++) if (old[i]) {
            unsigned k = hash(old[i], (int)strlen(old[i])) & (icap - 1);
            while (itab[k]) k = (k + 1) & (icap - 1);
            itab[k] = old[i];
        }
        free(old);
        h = hash(s, n) & (icap - 1);
        while (itab[h]) h = (h + 1) & (icap - 1);
    }
    return itab[h] = xstrndup(s, n);
}

void arena_reset(void)
{
    while (chunks) { Chunk *n = chunks->next; free(chunks); chunks = n; }
    free(itab); itab = NULL; icap = 0; icount = 0;
}

/* ---------- diagnostics ---------- */
FcContext *CX;
static char *fnames[256]; static int nfiles;
jmp_buf *recover_jb;           /* set by the parser */

int file_register(const char *name)
{
    for (int i = 0; i < nfiles; i++) if (!strcmp(fnames[i], name)) return i;
    if (nfiles >= 256) return 255;
    fnames[nfiles] = xstrdup(name);
    return nfiles++;
}
const char *file_name(int id) { return id >= 0 && id < nfiles ? fnames[id] : "?"; }
static char fbuiltin[256];
void file_set_builtin(int id) { if (id >= 0 && id < 256) fbuiltin[id] = 1; }
int file_is_builtin(int id) { return id >= 0 && id < 256 && fbuiltin[id]; }
void files_reset(void) { nfiles = 0; memset(fbuiltin, 0, sizeof fbuiltin); }

static void emit(int sev, int fileid, int line, const char *fmt, va_list ap)
{
    char msg[512];
    vsnprintf(msg, sizeof msg, fmt, ap);
    if (sev == FC_WARNING) {
        if (CX->opt.warn_level == 0) return;
        CX->warnings++;
    } else CX->errors++;
    if (CX->diag) CX->diag(CX->user, sev, file_name(fileid), line, msg);
}

void fc_warn(int fileid, int line, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); emit(FC_WARNING, fileid, line, fmt, ap); va_end(ap);
}
void fc_error_nj(int fileid, int line, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); emit(FC_ERROR, fileid, line, fmt, ap); va_end(ap);
}
void fc_error(int fileid, int line, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); emit(FC_ERROR, fileid, line, fmt, ap); va_end(ap);
    if (CX->opt.max_errors && CX->errors >= CX->opt.max_errors) {
        if (CX->diag) CX->diag(CX->user, FC_FATAL, file_name(fileid), line, "Too many error or warning messages");
        CX->errors++;
    }
    if (recover_jb) longjmp(*recover_jb, 1);
    exit(2);
}
