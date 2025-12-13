#include "vfs.h"
#include "types.h"


void vfs_init(void) {
}

/*
* vfs_alloc_fd - Allocate a file descriptor slot.
*
* Searches for a free slot (where vn == NULL).
* Starts at fd 3 since 0/1/2 are reserved for stdin/stdout/stderr.
*
* Returns: fd number (>= 3), or -1 if table full.
*/
int vfs_alloc_fd(struct fd_table *fdt) {
    /* Start at 3, skip stdin/stdout/stderr */
    for (int i = 3; i < MAX_FDS; i++) {
        if (fdt->fds[i].vn == NULL) {
            return i;
        }
    }
    return -1;  /* No free slots */
}

/*
* vfs_free_fd - Release a file descriptor slot.
*/
void vfs_free_fd(struct fd_table *fdt, int fd) {
    if (fd >= 0 && fd < MAX_FDS) {
        fdt->fds[fd].vn = NULL;
        fdt->fds[fd].position = 0;
        fdt->fds[fd].flags = 0;
    }
}

/*
* vfs_get_fd - Get file descriptor by number.
*
* Returns: Pointer to file_descriptor, or NULL if invalid/unused.
*/
struct file_descriptor *vfs_get_fd(struct fd_table *fdt, int fd) {
    if (fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }
    if (fdt->fds[fd].vn == NULL) {
        return NULL;  /* Slot not in use */
    }
    return &fdt->fds[fd];
}
