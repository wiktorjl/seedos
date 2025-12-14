 /* tarfs.c - TAR filesystem vnode operations */

#include "vfs.h"
#include "tar.h"
#include "string.h"
#include <stddef.h>


/* Pool of vnodes (no malloc in kernel) */
static struct vnode vnode_pool[MAX_VNODES];

/* Synthetic root directory entry (not in TAR archive) */
static struct tar_file tar_root = {
    .name = "",
    .data = NULL,
    .size = 0,
    .is_dir = 1
};

/* 
* tarfs_read - Read from a TAR file.
*
* The vnode's fs_data points to a struct tar_file.
* Read 'count' bytes starting at 'offset' into 'buf'.
*
* Returns: bytes read, or 0 at EOF
*/
static ssize_t tarfs_read(struct vnode *vn, void *buf, size_t count, size_t offset) {
    // 1. Get tar_file from vn->fs_data
    // 2. Check if offset >= file size (return 0 for EOF)
    // 3. Clamp count if it would read past end
    // 4. memcpy from tar_file->data + offset to buf
    // 5. Return bytes copied

    struct tar_file *tf = (struct tar_file *)vn->fs_data;
    if(offset >= tf->size) {
        return 0;  // EOF
    }
    if(offset + count > tf->size) {
        count = tf->size - offset;
    }
    memcpy(buf, tf->data + offset, count);
    return count;
}

/*
* tarfs_write - Write to TAR file (not supported, read-only)
*/
static ssize_t tarfs_write(struct vnode *vn, const void *buf, size_t count, size_t offset) {
    (void)vn; (void)buf; (void)count; (void)offset;
    return -1;  /* Read-only filesystem */
}

/*
* tarfs_close - Close a TAR vnode.
*
* Decrement refcount. If zero, mark vnode as free.
*/
static int tarfs_close(struct vnode *vn) {

    vn->refcount--;
    if(vn->refcount == 0) {
        vn->ops = NULL;  // Mark as free
    }
    return 0;
}

/* Operations table for TAR files */
static const struct vnode_ops tarfs_ops = {
    .read = tarfs_read,
    .write = tarfs_write,
    .close = tarfs_close,
};

/*
* tarfs_open - Open a file from the TAR archive.
*
* @path: Path to file (e.g., "bin/hello")
*
* Returns: vnode pointer, or NULL if not found
*/
struct vnode *tarfs_open(const char *path) {
    struct tar_file *tf;

    /* Handle root directory specially (empty path or "/") */
    if(path == NULL || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        tf = &tar_root;
    }else {
        tf = tar_find(path);
        if(tf == NULL) {
            return NULL;
        }
    }

    for(int i = 0; i < MAX_VNODES; i++) {
        if(vnode_pool[i].ops == NULL) {
            vnode_pool[i].ops = &tarfs_ops;
            vnode_pool[i].fs_data = (void *)tf;
            vnode_pool[i].refcount = 1;
            vnode_pool[i].type = tf->is_dir ? VNODE_DIR : VNODE_FILE;
            vnode_pool[i].size = tf->size;
            return &vnode_pool[i];
        }
    }
    return NULL;
}
