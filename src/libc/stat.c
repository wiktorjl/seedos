/*
 * stat.c - File Status Functions
 */

#include <sys/stat.h>
#include "internal/syscall.h"

int stat(const char *path, struct stat *buf) {
    return __syscall2(__NR_stat, (long)path, (long)buf);
}

int fstat(int fd, struct stat *buf) {
    return __syscall2(__NR_fstat, fd, (long)buf);
}

int lstat(const char *path, struct stat *buf) {
    /* lstat is the same as stat (no symlink support yet) */
    return __syscall2(__NR_stat, (long)path, (long)buf);
}
