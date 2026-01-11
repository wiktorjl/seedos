// SPDX-License-Identifier: GPL-2.0-only
/*
 * Virtual File System Implementation
 *
 * Provides unified file operations dispatched through function pointers.
 */

#include "vfs.h"
#include "heap.h"
#include "log.h"
#include "syscall_table.h"  /* For errno codes */

/**
 * vfs_init - Initialize the VFS subsystem
 */
void vfs_init(void)
{
    log_debug("VFS: Subsystem initialized");
}

/**
 * vfs_file_alloc - Allocate a new vfs_file structure
 */
vfs_file_t *vfs_file_alloc(void)
{
    vfs_file_t *file = kmalloc(sizeof(vfs_file_t));
    if (!file) {
        return NULL;
    }

    file->type = VFS_TYPE_NONE;
    file->flags = 0;
    file->offset = 0;
    file->refcount = 1;
    file->ops = NULL;
    file->private = NULL;

    return file;
}

/**
 * vfs_file_ref - Increment file reference count
 */
void vfs_file_ref(vfs_file_t *file)
{
    if (file) {
        file->refcount++;
    }
}

/**
 * vfs_file_unref - Decrement file reference count and free if zero
 */
void vfs_file_unref(vfs_file_t *file)
{
    if (!file) {
        return;
    }

    file->refcount--;
    if (file->refcount <= 0) {
        /* Call close handler if present */
        if (file->ops && file->ops->close) {
            file->ops->close(file);
        }
        kfree(file);
    }
}

/**
 * vfs_read - Read from a file
 */
ssize_t vfs_read(vfs_file_t *file, void *buf, size_t count)
{
    if (!file) {
        return -EBADF;
    }

    /* Check file is open for reading */
    if ((file->flags & O_ACCMODE) == O_WRONLY) {
        return -EBADF;
    }

    /* Dispatch to backend */
    if (!file->ops || !file->ops->read) {
        return -EINVAL;
    }

    return file->ops->read(file, buf, count);
}

/**
 * vfs_write - Write to a file
 */
ssize_t vfs_write(vfs_file_t *file, const void *buf, size_t count)
{
    if (!file) {
        return -EBADF;
    }

    /* Check file is open for writing */
    if ((file->flags & O_ACCMODE) == O_RDONLY) {
        return -EBADF;
    }

    /* Dispatch to backend */
    if (!file->ops || !file->ops->write) {
        return -EINVAL;
    }

    return file->ops->write(file, buf, count);
}

/**
 * vfs_lseek - Seek within a file
 */
off_t vfs_lseek(vfs_file_t *file, off_t offset, int whence)
{
    if (!file) {
        return -EBADF;
    }

    /* Dispatch to backend if it has custom seek */
    if (file->ops && file->ops->lseek) {
        return file->ops->lseek(file, offset, whence);
    }

    /* Default implementation for regular files */
    off_t new_offset;
    switch (whence) {
    case SEEK_SET:
        new_offset = offset;
        break;
    case SEEK_CUR:
        new_offset = (off_t)file->offset + offset;
        break;
    case SEEK_END:
        /* Need file size - not available without inode */
        return -ESPIPE;  /* Not seekable */
    default:
        return -EINVAL;
    }

    if (new_offset < 0) {
        return -EINVAL;
    }

    file->offset = (uint64_t)new_offset;
    return new_offset;
}

/**
 * vfs_close - Close a file
 */
int vfs_close(vfs_file_t *file)
{
    if (!file) {
        return -EBADF;
    }

    vfs_file_unref(file);
    return 0;
}
