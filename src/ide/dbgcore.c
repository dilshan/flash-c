/*
 * File:    dbgcore.c
 * Purpose: Flash C - source-level debugger core: process control, breakpoints,
 *          stepping, frames
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include "dbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/stat.h>

DbgBP dbg_bps[DBG_MAXBP];
int dbg_nbps;
int dbg_radix;
char dbg_msg[200];
int (*dbg_screen_hook)(int mode);
static int hook_mode = 1;                 /* 1 the program runs freely, 2 quietly inside a step */

static pid_t pid;
static int on_user;                       /* the program screen is displayed */
static int state = DS_NONE;
static int master = -1, memfd = -1;
static int exit_code, stop_sig, pending_sig;
static char *outbuf; static int outlen, outcap;

/* ------------------------------------------------------------------ debug info access */
static const FcDebug *DI;
static char **absfiles; static int nabs; static const FcDebug *abs_for;

static const FcDebug *info(void)
{
    const FcDebug *d = fc_debug_info();
    if (d != DI) { DI = d; }
    return DI;
}

int dbg_info_ok(void) { return info() != NULL && info()->nfuncs > 0; }

static const char *abs_file(int fi)
{
    const FcDebug *d = info();
    if (!d || fi < 0 || fi >= d->nfiles) return "";
    if (abs_for != d) {
        for (int i = 0; i < nabs; i++) free(absfiles[i]);
        free(absfiles); absfiles = calloc(d->nfiles + 1, sizeof *absfiles); nabs = d->nfiles; abs_for = d;
    }
    if (!absfiles[fi]) { char *r = realpath(d->files[fi], NULL); absfiles[fi] = r ? r : strdup(d->files[fi]); }
    return absfiles[fi];
}

static int file_index(const char *path)
{
    const FcDebug *d = info();
    if (!d) return -1;
    char *r = realpath(path, NULL);
    const char *want = r ? r : path;
    int found = -1;
    for (int i = 0; i < d->nfiles && found < 0; i++) if (!strcmp(abs_file(i), want)) found = i;
    free(r);
    return found;
}

static const FcDFunc *func_at(unsigned long pc)
{
    const FcDebug *d = info();
    if (!d) return NULL;
    for (int i = 0; i < d->nfuncs; i++) if (pc >= d->funcs[i].addr && pc < d->funcs[i].end) return &d->funcs[i];
    return NULL;
}

/* index of the last line entry at or before pc inside its function, -1 if none */
static int line_idx(unsigned long pc)
{
    const FcDebug *d = info();
    const FcDFunc *f = func_at(pc);
    if (!d || !f) return -1;
    int lo = 0, hi = d->nlines - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (d->lines[mid].addr <= pc) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (best >= 0 && d->lines[best].addr < f->addr) best = -1;
    return best;
}

static int is_line_start(unsigned long pc)
{
    const FcDebug *d = info();
    if (!d) return 0;
    int lo = 0, hi = d->nlines - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (d->lines[mid].addr == pc) return 1;
        if (d->lines[mid].addr < pc) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

static unsigned long first_line_addr(const FcDFunc *f)
{
    const FcDebug *d = info();
    for (int i = 0; i < d->nlines; i++) if (d->lines[i].addr >= f->addr && d->lines[i].addr < f->end) return d->lines[i].addr;
    return f->addr;
}

/* address of the first instruction of a source line; for a line without code the next line that has code */
static unsigned long line_addr(int fi, int line, int exact, int *actual)
{
    const FcDebug *d = info();
    unsigned long best = 0; int bestline = 0;
    for (int i = 0; i < d->nlines; i++) {
        if (d->lines[i].file != fi) continue;
        int l = d->lines[i].line;
        if (l == line) { if (actual) *actual = l; return d->lines[i].addr; }
        if (!exact && l > line && (!bestline || l < bestline)) { bestline = l; best = d->lines[i].addr; }
    }
    if (bestline && actual) *actual = bestline;
    return best;
}

int dbg_has_code(const char *file, int line)
{
    int fi = file_index(file);
    return fi >= 0 && line_addr(fi, line, 1, NULL) != 0;
}

int dbg_find_function(const char *name, char *file, int n, int *line)
{
    const FcDebug *d = info();
    if (!d) return -1;
    for (int i = 0; i < d->nfuncs; i++) if (!strcmp(d->funcs[i].name, name)) {
        char *r = realpath(d->funcs[i].file, NULL);
        snprintf(file, n, "%s", r ? r : d->funcs[i].file); free(r);
        *line = d->funcs[i].line0;
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ memory and registers */
static int rdmem(unsigned long addr, void *buf, size_t n)
{
    if (memfd < 0) return -1;
    return pread(memfd, buf, n, (off_t)addr) == (ssize_t)n ? 0 : -1;
}
static int wrmem(unsigned long addr, const void *buf, size_t n)
{
    if (memfd < 0) return -1;
    return pwrite(memfd, buf, n, (off_t)addr) == (ssize_t)n ? 0 : -1;
}
int dbg_read_memory(unsigned long addr, void *buf, size_t n) { return rdmem(addr, buf, n); }
int dbg_write_memory(unsigned long addr, const void *buf, size_t n) { return wrmem(addr, buf, n); }

static struct user_regs_struct regs;
static int get_regs(void) { return ptrace(PTRACE_GETREGS, pid, 0, &regs) == 0 ? 0 : -1; }
static int set_regs(void) { return ptrace(PTRACE_SETREGS, pid, 0, &regs) == 0 ? 0 : -1; }
unsigned long dbg_reg_rbp(void) { return regs.rbp; }

/* ------------------------------------------------------------------ output captured from the pty */
void (*dbg_output_hook)(const char *data, int len);

static void drain_master(int to_stdout)
{
    char b[1024];
    for (;;) {
        struct pollfd p = { master, POLLIN, 0 };
        if (master < 0 || poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN)) break;
        ssize_t n = read(master, b, sizeof b);
        if (n <= 0) break;
        if (dbg_output_hook) dbg_output_hook(b, (int)n);
        if (to_stdout) { ssize_t w = write(1, b, n); (void)w; }
        else {
            if (outlen + n > outcap) { outcap = (outlen + (int)n) * 2 + 256; outbuf = realloc(outbuf, outcap); }
            memcpy(outbuf + outlen, b, n); outlen += (int)n;
        }
    }
}

const char *dbg_pending_output(int *len) { drain_master(0); if (len) *len = outlen; return outbuf ? outbuf : ""; }
void dbg_clear_output(void) { outlen = 0; }

/* ------------------------------------------------------------------ process control */
static void close_session(void)
{
    if (master >= 0) close(master);
    if (memfd >= 0) close(memfd);
    master = memfd = -1;
}

void dbg_kill(void)
{
    if (state == DS_STOPPED && pid > 0) {
        kill(pid, SIGKILL);
        int st; waitpid(pid, &st, __WALL);
    }
    close_session();
    pid = 0; state = DS_NONE; pending_sig = 0;
    outlen = 0;
}

int dbg_state(void) { return state; }
int dbg_exit_code(void) { return exit_code; }
int dbg_signal(void) { return stop_sig; }

static int struct_nframes_dirty = 1;

static struct termios saved_tio; static int have_tio;
static void show_user(int mode)
{
    if (on_user) return;
    int shown = dbg_screen_hook ? dbg_screen_hook(mode) : 1;
    if (!shown) return;
    on_user = 1;
    have_tio = tcgetattr(0, &saved_tio) == 0;
    if (have_tio) {
        struct termios raw = saved_tio;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG); raw.c_iflag &= ~(ICRNL | IXON); raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
}
static void hide_user(void)
{
    if (!on_user) return;
    drain_master(1);
    on_user = 0;
    if (have_tio) tcsetattr(0, TCSANOW, &saved_tio);
    have_tio = 0;
    if (dbg_screen_hook) dbg_screen_hook(0);
}

/* wait for the child to stop; returns 0 when stopped by SIGTRAP, DR_* for other events */
static int wait_stop(int nohang_pump)
{
    int st, idle = 0;
    for (;;) {
        pid_t r = waitpid(pid, &st, __WALL | (nohang_pump ? WNOHANG : 0));
        if (r == 0) {
            /* the program is running: shuttle bytes between the terminal and the pty */
            if (!on_user) { struct pollfd q = { master, POLLIN, 0 }; poll(&q, 1, 10); drain_master(0); if (++idle >= 30) show_user(3); continue; }
            struct pollfd p[2] = { { master, POLLIN, 0 }, { 0, POLLIN, 0 } };
            if (poll(p, 2, 15) > 0) {
                char b[1024];
                if (p[0].revents & POLLIN) { ssize_t n = read(master, b, sizeof b); if (n > 0) { if (dbg_output_hook) dbg_output_hook(b, (int)n); ssize_t w = write(1, b, n); (void)w; } }
                if (p[1].revents & POLLIN) { ssize_t n = read(0, b, sizeof b); if (n > 0 && memchr(b, 3, n)) kill(pid, SIGINT); else if (n > 0) { ssize_t w = write(master, b, n); (void)w; } }
            }
            continue;
        }
        if (r < 0) { if (errno == EINTR) continue; state = DS_EXITED; exit_code = 0; return DR_EXIT; }
        break;
    }
    struct_nframes_dirty = 1;
    if (WIFEXITED(st)) { state = DS_EXITED; exit_code = WEXITSTATUS(st); drain_master(on_user); return DR_EXIT; }
    if (WIFSIGNALED(st)) { state = DS_EXITED; exit_code = -WTERMSIG(st); drain_master(on_user); return DR_EXIT; }
    if (WIFSTOPPED(st)) {
        int sig = WSTOPSIG(st);
        if (sig == SIGTRAP) return 0;
        stop_sig = sig; pending_sig = sig;
        return DR_SIGNAL;
    }
    return DR_ERROR;
}

static int single_step(void)
{
    int sig = pending_sig; pending_sig = 0;
    if (ptrace(PTRACE_SINGLESTEP, pid, 0, sig) < 0) return DR_ERROR;
    int r = wait_stop(0);
    return r;
}

/* temporary breakpoints for the duration of one resume */
typedef struct Ins { unsigned long addr; unsigned char orig; int on; } Ins;
static Ins ins[DBG_MAXBP + 4]; static int nins;

static void ins_add(unsigned long addr)
{
    for (int i = 0; i < nins; i++) if (ins[i].addr == addr) return;
    if (nins >= DBG_MAXBP + 4) return;
    unsigned char c = 0xCC;
    if (rdmem(addr, &ins[nins].orig, 1)) return;
    ins[nins].addr = addr;
    if (wrmem(addr, &c, 1)) return;
    nins++;
}
static void ins_remove_all(void)
{
    for (int i = 0; i < nins; i++) wrmem(ins[i].addr, &ins[i].orig, 1);
    nins = 0;
}

static unsigned long bp_addr_of(const DbgBP *b)
{
    int fi = file_index(b->file);
    if (fi < 0) return 0;
    return line_addr(fi, b->line, 1, NULL);
}

static void insert_user_bps(void)
{
    for (int i = 0; i < dbg_nbps; i++) if (!dbg_bps[i].disabled) { unsigned long a = bp_addr_of(&dbg_bps[i]); if (a) ins_add(a); }
}

static int user_bp_at(unsigned long pc)
{
    for (int i = 0; i < dbg_nbps; i++) if (!dbg_bps[i].disabled && bp_addr_of(&dbg_bps[i]) == pc) return i;
    return -1;
}

int dbg_eval_cond(const char *expr, long long *v);

/* should the user breakpoint at pc stop the program now? (condition and pass count) */
static int bp_triggers(unsigned long pc)
{
    int hit = 0;
    for (int i = 0; i < dbg_nbps; i++) {
        DbgBP *b = &dbg_bps[i];
        if (b->disabled || bp_addr_of(b) != pc) continue;
        if (b->cond[0]) { long long v; if (dbg_eval_cond(b->cond, &v) == 0 && !v) continue; }
        b->hits++;
        if (b->hits < b->pass) continue;
        hit = 1;
    }
    return hit;
}

/* resume until a breakpoint (user or temporary) or an event. tmp = temporary address or 0.
 * Returns 0 when stopped at a trap (pc is set), DR_* otherwise. */
static int resume(unsigned long tmp, unsigned long want_rsp, int user_bps, int *user_hit)
{
    if (user_hit) *user_hit = 0;
    for (;;) {
        if (get_regs()) return DR_ERROR;
        unsigned long pc = regs.rip;
        /* leave the current instruction first so a breakpoint on it does not trap again at once */
        int r = single_step();
        if (r) return r;
        if (get_regs()) return DR_ERROR;
        if (regs.rip == tmp && (!want_rsp || regs.rsp == want_rsp)) return 0;
        if (user_bps && user_bp_at(regs.rip) >= 0 && bp_triggers(regs.rip)) { if (user_hit) *user_hit = 1; return 0; }
        (void)pc;
        show_user(hook_mode);
        if (tmp) ins_add(tmp);
        if (user_bps) insert_user_bps();
        int sig = pending_sig; pending_sig = 0;
        if (ptrace(PTRACE_CONT, pid, 0, sig) < 0) { ins_remove_all(); hide_user(); return DR_ERROR; }
        r = wait_stop(1);
        if (state == DS_STOPPED) ins_remove_all(); else nins = 0;
        hide_user();
        if (r) return r;
        if (get_regs()) return DR_ERROR;
        regs.rip -= 1;                                   /* the int3 has been executed */
        set_regs();
        struct_nframes_dirty = 1;
        if (regs.rip == tmp && (!want_rsp || regs.rsp == want_rsp)) return 0;
        if (user_bps && user_bp_at(regs.rip) >= 0) {
            if (bp_triggers(regs.rip)) { if (user_hit) *user_hit = 1; return 0; }
        }
        /* not ours (a deeper recursion level, or a condition that is false): go on */
    }
}

static int finish_result(int r, int user_hit, int ok_code)
{
    if (r == 0) return user_hit ? DR_BREAK : ok_code;
    return r;
}

int dbg_start(const char *exe, const char *args)
{
    dbg_kill();
    if (!dbg_info_ok()) { snprintf(dbg_msg, sizeof dbg_msg, "No debug information; compile with Options|Debugger|Source debugging"); return -1; }
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) || unlockpt(master)) { snprintf(dbg_msg, sizeof dbg_msg, "Cannot open a pseudo terminal"); close_session(); return -1; }
    char *sname = ptsname(master);
    char slave[200]; snprintf(slave, sizeof slave, "%s", sname ? sname : "");
    /* arguments: split on blanks, double quotes group */
    char *av[64]; int ac = 0;
    av[ac++] = (char *)exe;
    char *abuf = strdup(args ? args : "");
    for (char *p = abuf; *p && ac < 62;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') { av[ac++] = ++p; while (*p && *p != '"') p++; if (*p) *p++ = 0; }
        else { av[ac++] = p; while (*p && *p != ' ' && *p != '\t') p++; if (*p) *p++ = 0; }
    }
    av[ac] = NULL;
    struct winsize ws; if (ioctl(0, TIOCGWINSZ, &ws) == 0) ioctl(master, TIOCSWINSZ, &ws);
    pid = fork();
    if (pid < 0) { free(abuf); close_session(); snprintf(dbg_msg, sizeof dbg_msg, "Cannot start the program"); return -1; }
    if (pid == 0) {
        setsid();
        int s = open(slave, O_RDWR);
        if (s < 0) _exit(126);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0); dup2(s, 1); dup2(s, 2);
        if (s > 2) close(s);
        close(master);
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execv(exe, av);
        _exit(127);
    }
    free(abuf);
    int st;
    if (waitpid(pid, &st, __WALL) < 0 || !WIFSTOPPED(st)) { close_session(); pid = 0; snprintf(dbg_msg, sizeof dbg_msg, "Cannot start the program"); return -1; }
    ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_EXITKILL);
    char mp[64]; snprintf(mp, sizeof mp, "/proc/%d/mem", (int)pid);
    memfd = open(mp, O_RDWR);
    state = DS_STOPPED; pending_sig = 0; stop_sig = 0; nins = 0; outlen = 0;
    for (int i = 0; i < dbg_nbps; i++) dbg_bps[i].hits = 0;
    /* run to the first statement of main */
    const FcDFunc *m = NULL;
    for (int i = 0; i < info()->nfuncs; i++) if (!strcmp(info()->funcs[i].name, "main")) m = &info()->funcs[i];
    if (!m) { dbg_kill(); snprintf(dbg_msg, sizeof dbg_msg, "No main function"); return -1; }
    if (get_regs()) { dbg_kill(); return -1; }
    int uh;
    hook_mode = 2;
    int r = resume(first_line_addr(m), 0, 0, &uh);
    hook_mode = 1;
    if (r) { snprintf(dbg_msg, sizeof dbg_msg, "Program stopped during start-up"); if (state != DS_EXITED) dbg_kill(); return -1; }
    return 0;
}

int dbg_go(void)
{
    if (state != DS_STOPPED) return DR_ERROR;
    int uh;
    int r = resume(0, 0, 1, &uh);
    return finish_result(r, uh, DR_STEP);
}

int dbg_goto_line(const char *file, int line)
{
    if (state != DS_STOPPED) return DR_ERROR;
    int fi = file_index(file);
    if (fi < 0) { snprintf(dbg_msg, sizeof dbg_msg, "Not a line of the program"); return DR_ERROR; }
    int act = 0;
    unsigned long a = line_addr(fi, line, 0, &act);
    if (!a) { snprintf(dbg_msg, sizeof dbg_msg, "No code at or after this line"); return DR_ERROR; }
    int uh;
    int r = resume(a, 0, 1, &uh);
    return finish_result(r, uh, DR_GOTO);
}

int dbg_run_to_return(void)
{
    if (state != DS_STOPPED || get_regs()) return DR_ERROR;
    unsigned long ret = 0;
    if (rdmem(regs.rbp + 8, &ret, 8)) return DR_ERROR;
    int uh;
    int r = resume(ret, regs.rbp + 16, 1, &uh);
    return finish_result(r, uh, DR_STEP);
}

static int step_impl(int into);
int dbg_step(int into)
{
    hook_mode = 2;
    int r = step_impl(into);
    hook_mode = 1;
    return r;
}

static int step_impl(int into)
{
    if (state != DS_STOPPED || get_regs()) return DR_ERROR;
    const FcDFunc *f0 = func_at(regs.rip);
    if (!f0) return dbg_go();
    int li = line_idx(regs.rip);
    int start_line = li >= 0 ? info()->lines[li].line : 0;
    for (;;) {
        unsigned long pc = regs.rip, rsp = regs.rsp;
        unsigned char op[2] = { 0, 0 };
        rdmem(pc, op, 2);
        int is_call = op[0] == 0xE8 || (op[0] == 0xFF && (op[1] & 0x38) == 0x10);
        int is_ret = op[0] == 0xC3;
        int r = single_step();
        if (r) return r;
        if (get_regs()) return DR_ERROR;
        unsigned long npc = regs.rip;
        if (user_bp_at(npc) >= 0 && is_line_start(npc) && bp_triggers(npc)) return DR_BREAK;
        if (is_call) {
            const FcDFunc *nf = func_at(npc);
            if (nf && into) {
                int uh;
                r = resume(first_line_addr(nf), 0, 1, &uh);
                return finish_result(r, uh, DR_STEP);
            }
            unsigned long ret = 0;
            rdmem(regs.rsp, &ret, 8);
            int uh;
            r = resume(ret, rsp, 1, &uh);                /* rsp after the return equals rsp before the call */
            if (r || uh) return finish_result(r, uh, DR_STEP);
            continue;
        }
        if (is_ret) {
            if (func_at(npc)) return DR_STEP;            /* back in the caller, in the middle of its line */
            int uh;
            r = resume(0, 0, 1, &uh);                    /* main returned: let the program finish */
            return finish_result(r, uh, DR_STEP);
        }
        if (!func_at(npc)) {                             /* jumped into code without line information */
            int uh;
            r = resume(0, 0, 1, &uh);
            return finish_result(r, uh, DR_STEP);
        }
        if (is_line_start(npc)) {
            int ni = line_idx(npc);
            if (ni >= 0 && (info()->lines[ni].line != start_line || npc < pc || func_at(npc) != f0)) return DR_STEP;
        }
    }
}

/* ------------------------------------------------------------------ breakpoints */
int dbg_bp_find(const char *file, int line)
{
    char *r = realpath(file, NULL);
    const char *f = r ? r : file;
    int found = -1;
    for (int i = 0; i < dbg_nbps; i++) if (dbg_bps[i].line == line && !strcmp(dbg_bps[i].file, f)) { found = i; break; }
    free(r);
    return found;
}

int dbg_line_has_bp(const char *file, int line)
{
    int i = dbg_bp_find(file, line);
    return i < 0 ? 0 : dbg_bps[i].disabled ? 2 : 1;
}

int dbg_bp_add(const char *file, int line, const char *cond, int pass)
{
    if (dbg_nbps >= DBG_MAXBP) { snprintf(dbg_msg, sizeof dbg_msg, "Too many breakpoints"); return -1; }
    char *r = realpath(file, NULL);
    DbgBP *b = &dbg_bps[dbg_nbps];
    memset(b, 0, sizeof *b);
    snprintf(b->file, sizeof b->file, "%s", r ? r : file); free(r);
    b->line = line; b->pass = pass > 0 ? pass : 0;
    if (cond) snprintf(b->cond, sizeof b->cond, "%s", cond);
    return dbg_nbps++;
}

int dbg_bp_toggle(const char *file, int line)
{
    int i = dbg_bp_find(file, line);
    if (i >= 0) { dbg_bp_delete(i); return 0; }
    if (info() && !dbg_has_code(file, line)) { snprintf(dbg_msg, sizeof dbg_msg, "No code was generated for this line"); return -1; }
    return dbg_bp_add(file, line, NULL, 0) < 0 ? -1 : 1;
}

void dbg_bp_delete(int i)
{
    if (i < 0 || i >= dbg_nbps) return;
    memmove(&dbg_bps[i], &dbg_bps[i + 1], (dbg_nbps - i - 1) * sizeof(DbgBP));
    dbg_nbps--;
}

/* ------------------------------------------------------------------ position and frames */
static DbgFrame frames[64]; static int nframes_cached = -1, selframe;

static void build_frames(void)
{
    nframes_cached = 0; selframe = 0;
    if (state != DS_STOPPED || get_regs()) return;
    unsigned long pc = regs.rip, fp = regs.rbp;
    for (int depth = 0; depth < 64; depth++) {
        const FcDFunc *f = func_at(pc);
        if (!f) break;
        DbgFrame *fr = &frames[nframes_cached++];
        fr->pc = pc; fr->fp = fp; fr->fn = f;
        unsigned long lookup = depth ? pc - 1 : pc;
        int li = line_idx(lookup);
        fr->line = li >= 0 ? info()->lines[li].line : f->line0;
        snprintf(fr->file, sizeof fr->file, "%s", li >= 0 ? abs_file(info()->lines[li].file) : f->file);
        /* at the very first instructions of a function rbp is still the caller's */
        unsigned long ret = 0, nfp = 0;
        if (depth == 0 && pc == f->addr) { if (rdmem(regs.rsp, &ret, 8)) break; nfp = fp; fr->fp = regs.rsp - 8; }
        else { if (rdmem(fp + 8, &ret, 8) || rdmem(fp, &nfp, 8)) break; }
        pc = ret; fp = nfp;
        if (!fp && depth) break;
    }
}

static void ensure_frames(void) { if (struct_nframes_dirty || nframes_cached < 0) { build_frames(); struct_nframes_dirty = 0; } }
int dbg_nframes(void) { ensure_frames(); return nframes_cached; }
const DbgFrame *dbg_frame(int i) { ensure_frames(); return i >= 0 && i < nframes_cached ? &frames[i] : NULL; }
void dbg_select_frame(int i) { ensure_frames(); if (i >= 0 && i < nframes_cached) selframe = i; }
int dbg_selected_frame(void) { ensure_frames(); return selframe; }

int dbg_where(char *file, int n, int *line)
{
    ensure_frames();
    if (state != DS_STOPPED || selframe >= nframes_cached) return -1;
    snprintf(file, n, "%s", frames[selframe].file);
    *line = frames[selframe].line;
    return 0;
}

const FcDFunc *dbg_cur_func(void) { ensure_frames(); return selframe < nframes_cached ? frames[selframe].fn : NULL; }
int dbg_cur_line(void) { ensure_frames(); return selframe < nframes_cached ? frames[selframe].line : 0; }
unsigned long dbg_cur_fp(void) { ensure_frames(); return selframe < nframes_cached ? frames[selframe].fp : 0; }
const FcDebug *dbg_info(void) { return info(); }

int dbg_get_regs_copy(struct user_regs_struct *r) { if (state != DS_STOPPED || get_regs()) return -1; *r = regs; return 0; }
