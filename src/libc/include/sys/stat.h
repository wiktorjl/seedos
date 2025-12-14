/*
 * sys/stat.h - File Status
 */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <stddef.h>
#include <stdint.h>

/* File type bits for st_mode */
#define S_IFMT   0170000  /* Type mask */
#define S_IFDIR  0040000  /* Directory */
#define S_IFREG  0100000  /* Regular file */
#define S_IFLNK  0120000  /* Symbolic link */

/* File type test macros */
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

/* Permission bits */
#define S_IRWXU  0700   /* User rwx */
#define S_IRUSR  0400   /* User read */
#define S_IWUSR  0200   /* User write */
#define S_IXUSR  0100   /* User execute */
#define S_IRWXG  0070   /* Group rwx */
#define S_IRGRP  0040   /* Group read */
#define S_IWGRP  0020   /* Group write */
#define S_IXGRP  0010   /* Group execute */
#define S_IRWXO  0007   /* Other rwx */
#define S_IROTH  0004   /* Other read */
#define S_IWOTH  0002   /* Other write */
#define S_IXOTH  0001   /* Other execute */

struct stat {
    uint64_t st_dev;      /* Device ID */
    uint64_t st_ino;      /* Inode number */
    uint32_t st_mode;     /* File type and permissions */
    uint32_t st_nlink;    /* Number of hard links */
    uint32_t st_uid;      /* User ID */
    uint32_t st_gid;      /* Group ID */
    uint64_t st_rdev;     /* Device ID (if special file) */
    uint64_t st_size;     /* Size in bytes */
    uint64_t st_blksize;  /* Block size for I/O */
    uint64_t st_blocks;   /* Number of 512-byte blocks */
    uint64_t st_atime;    /* Access time */
    uint64_t st_mtime;    /* Modification time */
    uint64_t st_ctime;    /* Status change time */
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);

#endif /* _SYS_STAT_H */
