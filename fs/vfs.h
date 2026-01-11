/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Virtual File System Interface
 *
 * Provides a unified interface for file operations across different
 * filesystems and device types. Each open file is represented by a
 * vfs_file_t which contains the file's state and operation handlers.
 *
 * File operations are dispatched through function pointers, allowing
 * different backends (ext2, console, pipes, etc.) to implement their
 * own behavior.
 */

#ifndef _VFS_H
#define _VFS_H

#include "types.h"

/*
 * Forward declarations
 */
struct vfs_file;
struct vfs_inode;

/*
 * File types
 */
#define VFS_TYPE_NONE       0
#define VFS_TYPE_REG        1   /* Regular file */
#define VFS_TYPE_DIR        2   /* Directory */
#define VFS_TYPE_CHR        3   /* Character device */
#define VFS_TYPE_BLK        4   /* Block device */
#define VFS_TYPE_FIFO       5   /* Named pipe */
#define VFS_TYPE_SOCK       6   /* Socket */
#define VFS_TYPE_LNK        7   /* Symbolic link */

/*
 * Open flags (Linux compatible)
 */
#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_ACCMODE       0x0003  /* Mask for access mode */

#define O_CREAT         0x0040
#define O_EXCL          0x0080
#define O_NOCTTY        0x0100
#define O_TRUNC         0x0200
#define O_APPEND        0x0400
#define O_NONBLOCK      0x0800
#define O_CLOEXEC       0x80000

/*
 * Seek origins
 */
#define SEEK_SET        0   /* From beginning of file */
#define SEEK_CUR        1   /* From current position */
#define SEEK_END        2   /* From end of file */

/*
 * File operation handlers
 *
 * Each file type (regular file, device, pipe, etc.) implements these
 * operations. NULL means the operation is not supported.
 */
typedef struct file_ops {
    /* Read up to count bytes into buf, return bytes read or negative errno */
    ssize_t (*read)(struct vfs_file *file, void *buf, size_t count);

    /* Write up to count bytes from buf, return bytes written or negative errno */
    ssize_t (*write)(struct vfs_file *file, const void *buf, size_t count);

    /* Seek to offset, return new position or negative errno */
    off_t (*lseek)(struct vfs_file *file, off_t offset, int whence);

    /* Close the file, return 0 or negative errno */
    int (*close)(struct vfs_file *file);

    /* For devices: ioctl operations */
    int (*ioctl)(struct vfs_file *file, unsigned long request, void *arg);
} file_ops_t;

/*
 * Open file structure
 *
 * Represents an open file descriptor. Multiple file descriptors can
 * point to the same vfs_file (after dup() or fork()).
 */
typedef struct vfs_file {
    int type;               /* VFS_TYPE_* */
    int flags;              /* O_* flags from open() */
    uint64_t offset;        /* Current file position */
    int refcount;           /* Number of fd references */

    file_ops_t *ops;        /* File operation handlers */
    void *private;          /* Backend-specific data (inode, device, etc.) */
} vfs_file_t;

/*
 * VFS functions
 */

/**
 * vfs_init - Initialize the VFS subsystem
 */
void vfs_init(void);

/**
 * vfs_file_alloc - Allocate a new vfs_file structure
 *
 * Return: New file structure, or NULL on failure
 */
vfs_file_t *vfs_file_alloc(void);

/**
 * vfs_file_ref - Increment file reference count
 * @file: File to reference
 */
void vfs_file_ref(vfs_file_t *file);

/**
 * vfs_file_unref - Decrement file reference count and free if zero
 * @file: File to unreference
 *
 * Calls file->ops->close() when refcount reaches zero.
 */
void vfs_file_unref(vfs_file_t *file);

/**
 * vfs_read - Read from a file
 * @file: Open file
 * @buf: Buffer to read into
 * @count: Maximum bytes to read
 *
 * Return: Bytes read, 0 on EOF, or negative errno
 */
ssize_t vfs_read(vfs_file_t *file, void *buf, size_t count);

/**
 * vfs_write - Write to a file
 * @file: Open file
 * @buf: Buffer to write from
 * @count: Bytes to write
 *
 * Return: Bytes written or negative errno
 */
ssize_t vfs_write(vfs_file_t *file, const void *buf, size_t count);

/**
 * vfs_lseek - Seek within a file
 * @file: Open file
 * @offset: Offset to seek to
 * @whence: SEEK_SET, SEEK_CUR, or SEEK_END
 *
 * Return: New position or negative errno
 */
off_t vfs_lseek(vfs_file_t *file, off_t offset, int whence);

/**
 * vfs_close - Close a file
 * @file: File to close
 *
 * Decrements refcount; frees when it reaches zero.
 *
 * Return: 0 or negative errno
 */
int vfs_close(vfs_file_t *file);

#endif /* _VFS_H */
