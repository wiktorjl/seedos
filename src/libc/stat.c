/*
 * stat.c - File Status Functions
 */

#include <sys/stat.h>
#include <errno.h>
#include "internal/syscall.h"

int stat(const char *path, struct stat *buf) {
    int ret = __syscall2(__NR_stat, (long)path, (long)buf);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return ret;
}

int fstat(int fd, struct stat *buf) {
    int ret = __syscall2(__NR_fstat, fd, (long)buf);
    if (ret < 0) {
        errno = EBADF;
        return -1;
    }
    return ret;
}

int lstat(const char *path, struct stat *buf) {
    /* lstat is the same as stat (no symlink support yet) */
    return stat(path, buf);
}
