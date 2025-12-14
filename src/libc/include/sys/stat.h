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
#define S_IFCHR  0020000  /* Character device */
#define S_IFBLK  0060000  /* Block device */
#define S_IFREG  0100000  /* Regular file */
#define S_IFIFO  0010000  /* FIFO (named pipe) */
#define S_IFLNK  0120000  /* Symbolic link */
#define S_IFSOCK 0140000  /* Socket */

/* File type test macros */
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

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

/* Aliases */
#define S_IEXEC  S_IXUSR  /* Execute by owner */
#define S_IREAD  S_IRUSR  /* Read by owner */
#define S_IWRITE S_IWUSR  /* Write by owner */

/* Special permission bits */
#define S_ISUID  04000  /* Set user ID on execution */
#define S_ISGID  02000  /* Set group ID on execution */
#define S_ISVTX  01000  /* Sticky bit */

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

/* Type for mode */
typedef unsigned int mode_t;

/* File creation (stubs - filesystem is read-only) */
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int mkdir(const char *path, mode_t mode);
int mknod(const char *path, mode_t mode, unsigned int dev);
mode_t umask(mode_t mask);

/* Ownership (stubs) */
int chown(const char *path, unsigned int owner, unsigned int group);
int fchown(int fd, unsigned int owner, unsigned int group);

#endif /* _SYS_STAT_H */
