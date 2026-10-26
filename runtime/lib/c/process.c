/* process - running other programs: system, spawn*, exec*, getpid */
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <_fcrt.h>

int getpid(void) { return (int)__syscall(SYS_getpid); }

/* find an executable: names with a slash are used as they are, others are searched along PATH */
static int __find_exe(const char *file, char *buf, int usepath)
{
    const char *p;
    if (!usepath || strchr(file, '/')) {
        if (strlen(file) >= 1000) { errno = ENAMETOOLONG; return -1; }
        strcpy(buf, file);
        if (__syscall(SYS_access, buf, 1) < 0) { errno = ENOENT; return -1; }
        return 0;
    }
    p = getenv("PATH");
    if (!p) p = "/bin:/usr/bin";
    while (*p) {
        int n = 0;
        while (p[n] && p[n] != ':') n++;
        if (n + strlen(file) + 2 < 1000) {
            memcpy(buf, p, n);
            if (n) buf[n++] = '/'; else n = 0;
            strcpy(buf + n, file);
            if (__syscall(SYS_access, buf, 1) == 0) return 0;
        }
        p += n;
        if (*p == ':') p++;
        while (*p == '/' && 0) p++;
    }
    errno = ENOENT;
    return -1;
}

static int __wait_status(int st)
{
    if ((st & 0x7f) == 0) return (st >> 8) & 0xff;
    return 128 + (st & 0x7f);
}

static int __run(int mode, const char *file, char *const argv[], char *const envp[], int usepath)
{
    char path[1024];
    long pid;
    int st = 0;
    if (__find_exe(file, path, usepath) < 0) return -1;
    if (mode == P_OVERLAY) {
        fflush(NULL);
        __sysret(__syscall(SYS_execve, path, argv, envp ? envp : environ));
        return -1;
    }
    fflush(NULL);
    pid = __syscall(SYS_fork);
    if (pid < 0) { __sysret(pid); return -1; }
    if (pid == 0) {
        __syscall(SYS_execve, path, argv, envp ? envp : environ);
        _exit(127);
    }
    if (mode == P_NOWAIT) return (int)pid;
    while (__syscall(SYS_wait4, pid, &st, 0, 0) == -4) ;
    return __wait_status(st);
}

int system(const char *cmd)
{
    char *argv[4];
    long pid;
    int st = 0;
    if (!cmd) return 1;                           /* a command processor is available */
    fflush(NULL);
    pid = __syscall(SYS_fork);
    if (pid < 0) { __sysret(pid); return -1; }
    if (pid == 0) {
        argv[0] = "sh"; argv[1] = "-c"; argv[2] = (char *)cmd; argv[3] = NULL;
        __syscall(SYS_execve, "/bin/sh", argv, environ);
        _exit(127);
    }
    while (__syscall(SYS_wait4, pid, &st, 0, 0) == -4) ;
    return __wait_status(st);
}

/* collect the argument list of the execl/spawnl family; with env != NULL the environment pointer follows */
static int __gather(const char *arg0, va_list ap, char **vec, int max, char ***envp, int with_env)
{
    int n = 0;
    char *a = (char *)arg0;
    while (a && n < max - 1) {
        vec[n++] = a;
        a = va_arg(ap, char *);
    }
    vec[n] = NULL;
    if (with_env) *envp = va_arg(ap, char **);
    return n;
}

int spawnv(int mode, const char *path, char *const argv[]) { return __run(mode, path, argv, NULL, 0); }
int spawnve(int mode, const char *path, char *const argv[], char *const envp[]) { return __run(mode, path, argv, envp, 0); }
int spawnvp(int mode, const char *path, char *const argv[]) { return __run(mode, path, argv, NULL, 1); }
int spawnvpe(int mode, const char *path, char *const argv[], char *const envp[]) { return __run(mode, path, argv, envp, 1); }

int spawnl(int mode, const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 0);
    return __run(mode, path, vec, NULL, 0);
}

int spawnle(int mode, const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 1);
    return __run(mode, path, vec, envp, 0);
}

int spawnlp(int mode, const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 0);
    return __run(mode, path, vec, NULL, 1);
}

int spawnlpe(int mode, const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 1);
    return __run(mode, path, vec, envp, 1);
}

int execv(const char *path, char *const argv[]) { return __run(P_OVERLAY, path, argv, NULL, 0); }
int execve(const char *path, char *const argv[], char *const envp[]) { return __run(P_OVERLAY, path, argv, envp, 0); }
int execvp(const char *path, char *const argv[]) { return __run(P_OVERLAY, path, argv, NULL, 1); }
int execvpe(const char *path, char *const argv[], char *const envp[]) { return __run(P_OVERLAY, path, argv, envp, 1); }

int execl(const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 0);
    return __run(P_OVERLAY, path, vec, NULL, 0);
}

int execle(const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 1);
    return __run(P_OVERLAY, path, vec, envp, 0);
}

int execlp(const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 0);
    return __run(P_OVERLAY, path, vec, NULL, 1);
}

int execlpe(const char *path, const char *arg0, ...)
{
    char *vec[128]; char **envp = NULL; va_list ap;
    va_start(ap, arg0);
    __gather(arg0, ap, vec, 128, &envp, 1);
    return __run(P_OVERLAY, path, vec, envp, 1);
}
