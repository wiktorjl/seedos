/*
 * dirent.h - Directory Entries
 */

#ifndef _DIRENT_H
#define _DIRENT_H

#include <stddef.h>
#include <stdint.h>

/* Directory entry types */
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK    10
#define DT_SOCK   12

/* Directory entry structure (matches kernel's kernel_dirent) */
struct dirent {
    uint64_t d_ino;       /* Inode number */
    uint64_t d_off;       /* Offset to next entry */
    uint16_t d_reclen;    /* Length of this record */
    uint8_t  d_type;      /* File type */
    char     d_name[256]; /* Filename (null-terminated) */
};

/* Directory stream (opaque handle) */
typedef struct {
    int fd;                 /* File descriptor for the directory */
    struct dirent entry;    /* Current entry buffer */
    char buf[16384];        /* Buffer for getdents (fits ~58 entries) */
    size_t buf_pos;         /* Current position in buffer */
    size_t buf_len;         /* Bytes in buffer */
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

/* Low-level syscall (used internally) */
long getdents(int fd, struct dirent *dirp, size_t count);

#endif /* _DIRENT_H */
