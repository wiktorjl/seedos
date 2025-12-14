/*
 * fcntl.c - File Control Operations
 */

#include <fcntl.h>
#include <errno.h>
#include "internal/syscall.h"

int open(const char *pathname, int flags, ...) {
    int ret = __syscall2(__NR_open, (long)pathname, flags);
    if(ret < 0) {
        errno = ENOENT;  /* Assume file not found for now */
        return -1;
    }
    return ret;
}
