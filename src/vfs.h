#ifndef VFS_H
#define VFS_H

#include "types.h"

#define MAX_FDS 16
#define MAX_VNODES 64

#define VNODE_FILE  1
#define VNODE_DIR   2

#define O_RDONLY    0

struct vnode_ops;

struct vnode {
    const struct vnode_ops *ops;   /* Operations for this vnode */
    void *fs_data;           /* Filesystem-specific data */
    int refcount;            /* Reference count */
    int type;                /* VNODE_FILE or VNODE_DIR */
    size_t size;             /* Size in bytes */
};

struct vnode_ops {
    ssize_t (*read)(struct vnode *vn, void *buf, size_t count, size_t offset);
    ssize_t (*write)(struct vnode *vn, const void *buf, size_t count, size_t offset);
    int (*close)(struct vnode *vn);
};

struct file_descriptor {
    struct vnode *vn;        /* Pointer to the vnode */
    size_t position;         /* Current position in the file */
    int flags;               /* Open flags (e.g., O_RDONLY) */
};

struct fd_table {
    struct file_descriptor fds[MAX_FDS];
};

void vfs_init(void);
int vfs_alloc_fd(struct fd_table *fd_table);
void vfs_free_fd(struct fd_table *fd_table, int fd);
struct file_descriptor *vfs_get_fd(struct fd_table *fd_table, int fd);

#endif /* VFS_H */