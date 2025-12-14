/*
 * unistd.c - POSIX System Call Wrappers
 */

#include <unistd.h>
#include "internal/syscall.h"

ssize_t read(int fd, void *buf, size_t count) {
    return __syscall3(__NR_read, fd, (long)buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return __syscall3(__NR_write, fd, (long)buf, count);
}

int close(int fd) {
    return __syscall1(__NR_close, fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    return __syscall3(__NR_lseek, fd, offset, whence);
}

pid_t getpid(void) {
    return __syscall0(__NR_getpid);
}

void _exit(int status) {
    __syscall1(__NR_exit, status);
    __builtin_unreachable();
}

void *sbrk(long increment) {
    return (void *)__syscall1(__NR_sbrk, increment);
}

char *getcwd(char *buf, size_t size) {
    long ret = __syscall2(__NR_getcwd, (long)buf, size);
    return (ret == -1) ? (char *)0 : buf;
}

int chdir(const char *path) {
    return __syscall1(__NR_chdir, (long)path);
}

int isatty(int fd) {
    return __syscall1(__NR_isatty, fd);
}

int dup(int oldfd) {
    return __syscall1(__NR_dup, oldfd);
}

int dup2(int oldfd, int newfd) {
    return __syscall2(__NR_dup2, oldfd, newfd);
}

unsigned long uptime(void) {
    return __syscall0(__NR_uptime);
}
