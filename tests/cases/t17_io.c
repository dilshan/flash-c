#include <stdio.h>
#include <io.h>
#include <dir.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
int main(void)
{
    struct ffblk ff; struct stat st; int fd, r, n = 0; char buf[64]; char dr[4], di[70], nm[10], ex[6]; long total = 0;
    mkdir("fc_t17");
    fd = open("fc_t17/a.txt", O_CREAT | O_WRONLY, 0644); write(fd, "hello world", 11); close(fd);
    fd = open("fc_t17/b.txt", O_CREAT | O_WRONLY, 0644); write(fd, "xy", 2); close(fd);
    fd = open("fc_t17/c.dat", O_CREAT | O_WRONLY, 0644); close(fd);
    for (r = findfirst("fc_t17/*.txt", &ff, 0); r == 0; r = findnext(&ff)) { n++; total += ff.ff_fsize; }
    printf("%d files %ld bytes\n", n, total);
    stat("fc_t17/a.txt", &st); printf("%ld %d\n", st.st_size, (st.st_mode & S_IFMT) == S_IFREG);
    fd = open("fc_t17/a.txt", O_RDWR); printf("%ld ", filelength(fd)); lseek(fd, 6L, SEEK_SET);
    read(fd, buf, 5); buf[5] = 0; printf("%s %ld %d\n", buf, tell(fd), eof(fd)); chsize(fd, 3); printf("%ld\n", filelength(fd)); close(fd);
    printf("%d %d\n", access("fc_t17/a.txt", 0), access("fc_t17/zzz", 0));
    printf("%d\n", fnsplit("/usr/lib/foo.txt", dr, di, nm, ex)); printf("[%s][%s][%s]\n", di, nm, ex);
    fnmerge(buf, "", "/a/b/", "file", ".ext"); printf("%s\n", buf);
    r = rename("fc_t17/b.txt", "fc_t17/d.txt"); printf("%d %d\n", r, access("fc_t17/d.txt", 0));
    unlink("fc_t17/a.txt"); unlink("fc_t17/d.txt"); unlink("fc_t17/c.dat");
    printf("%d\n", rmdir("fc_t17"));
    return 0;
}
