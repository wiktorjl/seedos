/*
 * fcntl.c - File Control Operations
 */

#include <fcntl.h>
#include "internal/syscall.h"

int open(const char *pathname, int flags, ...) {
    return __syscall2(__NR_open, (long)pathname, flags);
}
