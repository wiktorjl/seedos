#include "vfs.h"
#include "types.h"
#include "tar.h"
#include "tarfs.h"
#include "string.h"


void vfs_init(void) {
}

/*
 * vfs_resolve_path - Normalize a path for filesystem lookup.
 */
int vfs_resolve_path(const char *path, const char *cwd, char *out, size_t out_size) {
    if (path == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    /* Strip leading "/" - tarfs uses paths without leading slash */
    if (path[0] == '/') {
        path++;
    }

    /* Strip "./" prefix */
    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    }

    /* Copy the normalized path */
    size_t len = strlen(path);
    if (len >= out_size) {
        return -1;  /* Path too long */
    }

    strcpy(out, path);

    /* TODO: Handle relative paths with cwd prepending */
    (void)cwd;

    return 0;
}

/*
 * vfs_resolve_executable - Resolve path to an executable.
 *
 * If path has no slashes, prepends "bin/" to search in /bin.
 */
int vfs_resolve_executable(const char *path, const char *cwd, char *out, size_t out_size) {
    if (path == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    /* Strip leading "/" and "./" */
    if (path[0] == '/') {
        path++;
    }
    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    }

    /* Check if path contains a slash (i.e., is a path, not just a name) */
    int has_slash = 0;
    for (const char *c = path; *c; c++) {
        if (*c == '/') {
            has_slash = 1;
            break;
        }
    }

    if (!has_slash) {
        /* No slash - prepend "bin/" */
        if (strlen(path) + 5 >= out_size) {  /* 5 = strlen("bin/") + 1 */
            return -1;
        }
        strcpy(out, "bin/");
        strcat(out, path);
    } else {
        /* Has slash - use as-is */
        if (strlen(path) >= out_size) {
            return -1;
        }
        strcpy(out, path);
    }

    (void)cwd;  /* TODO: Handle relative paths */
    return 0;
}

/*
 * vfs_lookup - Look up a file by path (abstracts underlying filesystem).
 */
struct vnode *vfs_lookup(const char *path) {
    return tarfs_open(path);
}

/*
 * vfs_lookup_executable - Look up an executable and return its data.
 */
int vfs_lookup_executable(const char *path, const void **data_out, size_t *size_out) {
    struct tar_file *file = tar_find(path);
    if (file == NULL) {
        return -1;
    }

    *data_out = file->data;
    *size_out = file->size;
    return 0;
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
