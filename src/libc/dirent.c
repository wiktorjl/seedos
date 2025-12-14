/*
 * dirent.c - Directory Operations
 */

#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "internal/syscall.h"

/* Low-level getdents syscall */
long getdents(int fd, struct dirent *dirp, size_t count) {
    return __syscall3(__NR_getdents, fd, (long)dirp, count);
}

DIR *opendir(const char *name) {
    /* Open the directory */
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    /* Allocate DIR structure */
    DIR *dirp = (DIR *)malloc(sizeof(DIR));
    if (dirp == NULL) {
        close(fd);
        return NULL;
    }

    dirp->fd = fd;
    dirp->buf_pos = 0;
    dirp->buf_len = 0;

    return dirp;
}

struct dirent *readdir(DIR *dirp) {
    if (dirp == NULL) {
        return NULL;
    }

    /* If buffer is empty or exhausted, read more entries */
    if (dirp->buf_pos >= dirp->buf_len) {
        long nread = getdents(dirp->fd, (struct dirent *)dirp->buf, sizeof(dirp->buf));
        if (nread <= 0) {
            return NULL;  /* End of directory or error */
        }
        dirp->buf_len = (size_t)nread;
        dirp->buf_pos = 0;
    }

    /* Get current entry from buffer */
    struct dirent *ent = (struct dirent *)(dirp->buf + dirp->buf_pos);

    /* Advance position for next call */
    dirp->buf_pos += ent->d_reclen;

    /* Copy to the entry buffer in DIR (for safety) */
    memcpy(&dirp->entry, ent, sizeof(struct dirent));

    return &dirp->entry;
}

int closedir(DIR *dirp) {
    if (dirp == NULL) {
        return -1;
    }

    int ret = close(dirp->fd);
    free(dirp);
    return ret;
}
