#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "memory.h"  /* PATH_MAX */

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

/*
 * vfs_resolve_path - Normalize a path for filesystem lookup.
 *
 * @path:     User-provided path (absolute or relative)
 * @cwd:      Current working directory (for relative paths), or NULL for root
 * @out:      Output buffer for resolved path
 * @out_size: Size of output buffer
 *
 * Handles:
 *   - Strips "./" prefix
 *   - "." resolves to current directory
 *   - Absolute paths: strips leading "/" for tarfs
 *   - Relative paths: prepends cwd
 *
 * Output is in tarfs format (no leading slash).
 *
 * Returns: 0 on success, -1 on error (path too long).
 */
int vfs_resolve_path(const char *path, const char *cwd, char *out, size_t out_size);

/*
 * vfs_resolve_executable - Resolve path to an executable.
 *
 * @path:     User-provided path or program name
 * @cwd:      Current working directory (for relative paths)
 * @out:      Output buffer for resolved path
 * @out_size: Size of output buffer
 *
 * If path has no slashes (bare command name like "ls"), prepends "bin/".
 * Otherwise resolves using vfs_resolve_path.
 *
 * Returns: 0 on success, -1 on error.
 */
int vfs_resolve_executable(const char *path, const char *cwd, char *out, size_t out_size);

/*
 * vfs_lookup - Look up a file by path.
 *
 * @path: Resolved path (without leading slash)
 *
 * Returns: vnode on success, NULL if not found.
 *
 * This abstracts the underlying filesystem (currently tarfs).
 */
struct vnode *vfs_lookup(const char *path);

/*
 * vfs_lookup_executable - Look up an executable file and get its data.
 *
 * @path:     Resolved path (without leading slash)
 * @data_out: Output pointer for file data
 * @size_out: Output pointer for file size
 *
 * Returns: 0 on success, -1 if not found.
 */
int vfs_lookup_executable(const char *path, const void **data_out, size_t *size_out);

#endif /* VFS_H */