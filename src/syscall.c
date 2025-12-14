/*
 * syscall.c - System Call Implementations
 *
 * This file contains the C implementations of all system calls.
 * The syscall_handler function dispatches to the appropriate handler
 * based on the syscall number in RAX.
 *
 * Adding a New Syscall:
 *
 *   1. Define the syscall number in syscall.h (e.g., #define SYS_FOO 2)
 *   2. Implement the handler function here (e.g., sys_foo())
 *   3. Add a case in syscall_handler() to dispatch to it
 *
 * Security Considerations:
 *
 *   - All pointers from user space must be validated before use
 *   - Don't trust any values from user registers
 *   - Check buffer bounds to prevent kernel memory corruption
 *   - (This simple implementation doesn't do full validation yet!)
 */

#include "syscall.h"
#include "console.h"
#include "process.h"
#include "sched.h"
#include "vmm.h"
#include "context.h"
#include "pit.h"
#include <stdint.h>
#include "keyboard.h"
#include "vfs.h"
#include "serial.h"
#include "tarfs.h"
#include "tar.h"
#include "string.h"
#include "memory.h"

/* =============================================================================
 * Structures for stat and getdents syscalls
 * =============================================================================
 */

/* File type bits for st_mode */
#define S_IFMT   0170000  /* Type mask */
#define S_IFDIR  0040000  /* Directory */
#define S_IFREG  0100000  /* Regular file */

/* Permission bits (simplified) */
#define S_IRWXU  0700     /* User rwx */
#define S_IRWXG  0070     /* Group rwx */
#define S_IRWXO  0007     /* Other rwx */

struct kernel_stat {
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

/* Directory entry for getdents */
struct kernel_dirent {
    uint64_t d_ino;       /* Inode number */
    uint64_t d_off;       /* Offset to next entry */
    uint16_t d_reclen;    /* Length of this record */
    uint8_t  d_type;      /* File type */
    char     d_name[256]; /* Filename (null-terminated) */
};

/* d_type values */
#define DT_UNKNOWN 0
#define DT_REG     8   /* Regular file */
#define DT_DIR     4   /* Directory */

/* Standard file descriptors */
#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

/* Error return value for syscalls */
#define SYSCALL_ERROR ((uint64_t)-1)

/* =============================================================================
 * Individual Syscall Implementations
 * =============================================================================
 */


static int64_t sys_open(uint64_t path_ptr, uint64_t flags) {
    const char *path = (const char *)path_ptr;

    if (!vmm_validate_user_range((const void *)path_ptr, 1)) {
        return -1;
    }

    /* Resolve path (handles ./, absolute, relative) */
    char full_path[PATH_MAX];
    if (vfs_resolve_path(path, process_get_cwd(), full_path, sizeof(full_path)) != 0) {
        return -1;
    }

    /* Get current process fd_table */
    struct fd_table *fdt = process_get_fd_table();

    // 4. Allocate fd
    int fd = vfs_alloc_fd(fdt);
    if (fd == -1) {
        return -1;  // No free file descriptors
    }

    // 5. Open vnode
    struct vnode *vn = tarfs_open(full_path);

    if (vn == NULL) {
        vfs_free_fd(fdt, fd);
        return -1;  // File not found
    }

    // 6. Set up fd entry
    fdt->fds[fd].vn = vn;
    fdt->fds[fd].position = 0;
    fdt->fds[fd].flags = (int)flags;

    // 7. Return fd
    return fd;
}

/*
* sys_close - Close a file descriptor.
*/
static int64_t sys_close(uint64_t fd) {
    if(fd >= MAX_FDS) {
        return -1;  // Invalid fd
    }

    // 1. Get fd_table
    struct fd_table *fdt = process_get_fd_table();
    // 2. Get file_descriptor with vfs_get_fd()
    struct file_descriptor *file_desc = vfs_get_fd(fdt, (int)fd);
    
    if( file_desc == NULL) {
        return -1;  // fd not in use
    }

    struct vnode *vn = file_desc->vn;
    vn->ops->close(vn);
    vfs_free_fd(fdt, (int)fd);
    return 0;
}


static int64_t sys_lseek(uint64_t fd, int64_t offset, uint64_t whence) {
    // 1. Get fd_table and file_descriptor
    if(fd >= MAX_FDS) {
        return -1;  // Invalid fd
    }

    // 1. Get fd_table
    struct fd_table *fdt = process_get_fd_table();
    // 2. Get file_descriptor with vfs_get_fd()
    struct file_descriptor *file_desc = vfs_get_fd(fdt, (int)fd);

    // 2. Calculate new position based on whence:
    //    SEEK_SET: new_pos = offset
    //    SEEK_CUR: new_pos = position + offset
    //    SEEK_END: new_pos = vnode->size + offset
    if( file_desc == NULL) {
        return -1;  // fd not in use
    }
    int64_t new_pos;
    switch (whence) {
        case 0:  // SEEK_SET
            new_pos = offset;
            break;
        case 1:  // SEEK_CUR
            new_pos = (int64_t)file_desc->position + offset;
            break;
        case 2:  // SEEK_END
            new_pos = (int64_t)file_desc->vn->size + offset;
            break;
        default:
            return -1;  // Invalid whence
    }

    if(new_pos < 0) {
        return -1;  // Invalid position
    }

    file_desc->position = (size_t)new_pos;
    return new_pos;

}


/*
 * sys_exit - Terminate the current process.
 *
 * @exit_code: The exit code to report (0 = success, non-zero = error).
 *
 * This function does not return. It:
 *   1. Switches back to the kernel's address space
 *   2. Saves the exit code and marks process as zombie
 *   3. Wakes up any process waiting for this one
 *   4. Returns control to the kernel (via context_return_to_kernel)
 */
static void sys_exit(uint64_t exit_code) {
    struct process *current = process_get_current();

    /* Switch back to kernel address space before returning */
    vmm_switch_address_space(vmm_get_kernel_pml4());

    /* Save the exit code (both per-process and global for legacy code) */
    current->exit_code = (int)exit_code;
    process_set_exit_code(exit_code);

    /* Remove from scheduler and mark as zombie */
    sched_remove(current);
    current->state = PROC_ZOMBIE;

    /* Wake up any process waiting for us */
    sched_wake_waiters(current->pid);

    /*
     * Return to where context_save_kernel_state() was called.
     * This effectively "returns" from context_switch_to_user().
     */
    context_return_to_kernel();

    /* Never reached - context_return_to_kernel doesn't return */
}

/*
 * sys_write - Write bytes to a file descriptor.
 *
 * @fd:     File descriptor (only FD_STDOUT=1 is supported)
 * @buffer: Pointer to data buffer (user-space address)
 * @count:  Number of bytes to write
 *
 * Returns: Number of bytes actually written, or 0 on error.
 *
 * Security: Validates buffer is in user address space before access.
 */
static uint64_t sys_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    const char *buf = (const char *)buffer;

    /* Writing 0 bytes is a no-op */
    if (count == 0) {
        return 0;
    }

    if (!vmm_validate_user_range((const void *)buffer, count)) {
        puts("Error: Invalid user buffer address\n");
        return 0;
    }

    /* Support stdout and stderr - both go to console */
    if (fd != FD_STDOUT && fd != FD_STDERR) {
        return 0;
    }

    /* Write each byte to the console */
    for (uint64_t i = 0; i < count; i++) {
        putc(buf[i]);
    }

    return count;  /* All bytes written successfully */
}

static uint64_t sys_read(uint64_t fd, uint64_t buffer, uint64_t count) {
    char *buf = (char *)buffer;

    /* Reading 0 bytes is a no-op */
    if (count == 0) {
        return 0;
    }

    if (!vmm_validate_user_range((const void *)buffer, count)) {
        puts("Error: Invalid user buffer address\n");
        return 0;
    }

    if (fd == FD_STDIN) {
        /*
         * Canonical (line-buffered) mode for stdin:
         * - Echo characters as they're typed
         * - Handle backspace locally
         * - Buffer until newline, then return the whole line
         * - Preserve remaining data for subsequent reads
         */
        static char line_buffer[256];
        static size_t line_len = 0;   /* Total chars in completed line */
        static size_t line_read = 0;  /* Chars already returned to caller */
        static size_t input_pos = 0;  /* Position while building current line */

        /* If we have remaining data from previous line, return that first */
        if (line_read < line_len) {
            size_t remaining = line_len - line_read;
            size_t to_copy = remaining < count ? remaining : count;
            memcpy(buf, line_buffer + line_read, to_copy);
            line_read += to_copy;

            /* If line fully consumed, reset for next line */
            if (line_read >= line_len) {
                line_len = 0;
                line_read = 0;
            }
            return to_copy;
        }

        /* Read characters until we get a newline */
        input_pos = 0;
        while (1) {
            /* Wait for a character */
            char c = 0;
            while (c == 0) {
                size_t n = keyboard_read(&c, 1);
                if (n == 0) {
                    /* No input yet - enable interrupts and wait */
                    __asm__ volatile ("sti; hlt; cli");
                    c = 0;
                }
            }

            if (c == '\n' || c == '\r') {
                /* Newline - echo it and complete the line */
                putc('\n');
                line_buffer[input_pos++] = '\n';
                line_len = input_pos;
                line_read = 0;

                /* Return as much as requested */
                size_t to_copy = line_len < count ? line_len : count;
                memcpy(buf, line_buffer, to_copy);
                line_read = to_copy;

                /* If line fully consumed, reset */
                if (line_read >= line_len) {
                    line_len = 0;
                    line_read = 0;
                }
                return to_copy;
            } else if (c == '\b' || c == 127) {
                /* Backspace - remove last character if any */
                if (input_pos > 0) {
                    input_pos--;
                    putc('\b');
                    putc(' ');
                    putc('\b');
                }
            } else if (c >= 32 && c < 127) {
                /* Printable character - add to buffer if room */
                if (input_pos < sizeof(line_buffer) - 1) {
                    line_buffer[input_pos++] = c;
                    putc(c);  /* Echo */
                }
            }
            /* Ignore other control characters */
        }
    } else {
        struct fd_table *fdt = process_get_fd_table();
        struct file_descriptor *file_desc = vfs_get_fd(fdt, (int)fd);
        if( file_desc == NULL) {
            return 0;
        }
        struct vnode *vn = file_desc->vn;
        ssize_t bytes_read = vn->ops->read(vn, buf, (size_t)count, file_desc->position);
        if(bytes_read < 0) {
            return 0;
        }
        file_desc->position += (size_t)bytes_read;
        return (uint64_t)bytes_read;
    }
}

static uint64_t sys_getpid(void) {
    return process_get_pid();
}

static uint64_t sys_getuptime(void) {
    return pit_get_ticks() * 1000;  /* Convert ticks to milliseconds */
}

static uint64_t sys_brk(uint64_t increment) {
    return (uint64_t)process_sbrk((intptr_t)increment);
}

/*
 * sys_stat - Get file status by path.
 */
static int64_t sys_stat(uint64_t path_ptr, uint64_t buf_ptr) {
    const char *path = (const char *)path_ptr;
    struct kernel_stat *buf = (struct kernel_stat *)buf_ptr;

    /* Validate pointers */
    if (!vmm_validate_user_range((const void *)path_ptr, 1)) {
        return -1;
    }
    if (!vmm_validate_user_range((const void *)buf_ptr, sizeof(struct kernel_stat))) {
        return -1;
    }

    /* Resolve path (handles ./, absolute, relative) */
    char full_path[PATH_MAX];
    if (vfs_resolve_path(path, process_get_cwd(), full_path, sizeof(full_path)) != 0) {
        return -1;
    }

    /* Find file in tarfs */
    struct tar_file *tf;
    if (full_path[0] == '\0') {
        /* Root directory - use synthetic entry */
        static struct tar_file root_entry = { .name = "", .is_dir = 1, .size = 0 };
        tf = &root_entry;
    } else {
        tf = tar_find(full_path);
        if (tf == NULL) {
            return -1;  /* File not found */
        }
    }

    /* Fill in stat buffer */
    memset(buf, 0, sizeof(struct kernel_stat));
    buf->st_dev = 1;
    buf->st_ino = (uint64_t)(uintptr_t)tf;  /* Use pointer as inode */
    buf->st_mode = tf->is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_size = tf->size;
    buf->st_blksize = 512;
    buf->st_blocks = (tf->size + 511) / 512;

    return 0;
}

/*
 * sys_fstat - Get file status by file descriptor.
 */
static int64_t sys_fstat(uint64_t fd, uint64_t buf_ptr) {
    struct kernel_stat *buf = (struct kernel_stat *)buf_ptr;

    if (!vmm_validate_user_range((const void *)buf_ptr, sizeof(struct kernel_stat))) {
        return -1;
    }

    if (fd >= MAX_FDS) {
        return -1;
    }

    struct fd_table *fdt = process_get_fd_table();
    struct file_descriptor *file_desc = vfs_get_fd(fdt, (int)fd);

    if (file_desc == NULL) {
        return -1;
    }

    struct vnode *vn = file_desc->vn;
    struct tar_file *tf = (struct tar_file *)vn->fs_data;

    /* Fill in stat buffer */
    memset(buf, 0, sizeof(struct kernel_stat));
    buf->st_dev = 1;
    buf->st_ino = (uint64_t)(uintptr_t)tf;
    buf->st_mode = (vn->type == VNODE_DIR) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_size = vn->size;
    buf->st_blksize = 512;
    buf->st_blocks = (vn->size + 511) / 512;

    return 0;
}

/* Context for getdents callback */
struct getdents_ctx {
    uint8_t *buf;
    size_t buf_size;
    size_t bytes_written;
    int count;
};

/* Callback for tar_list_dir */
static void getdents_callback(const char *name, size_t size, int is_dir, void *ctx_ptr) {
    struct getdents_ctx *ctx = (struct getdents_ctx *)ctx_ptr;

    /* Calculate entry size (fixed-size for simplicity) */
    size_t name_len = strlen(name);
    /* Remove trailing slash from directory names */
    if (name_len > 0 && name[name_len - 1] == '/') {
        name_len--;
    }

    /* Skip empty names, "/" and "./" entries */
    if (name_len == 0 || strcmp(name, "/") == 0 || strcmp(name, "./") == 0) {
        return;
    }

    size_t reclen = sizeof(struct kernel_dirent);

    /* Check if we have space */
    if (ctx->bytes_written + reclen > ctx->buf_size) {
        return;  /* No more space */
    }

    struct kernel_dirent *ent = (struct kernel_dirent *)(ctx->buf + ctx->bytes_written);

    ent->d_ino = ctx->count + 1;
    ent->d_off = ctx->bytes_written + reclen;
    ent->d_reclen = (uint16_t)reclen;
    ent->d_type = is_dir ? DT_DIR : DT_REG;

    /* Copy name without trailing slash */
    if (name_len >= sizeof(ent->d_name)) {
        name_len = sizeof(ent->d_name) - 1;
    }
    memcpy(ent->d_name, name, name_len);
    ent->d_name[name_len] = '\0';

    ctx->bytes_written += reclen;
    ctx->count++;
    (void)size;  /* Unused */
}

/*
 * sys_getdents - Read directory entries.
 *
 * The fd must be an open directory. Returns bytes read.
 * Uses file position to track if we've already returned entries.
 */
static int64_t sys_getdents(uint64_t fd, uint64_t buf_ptr, uint64_t count) {
    if (!vmm_validate_user_range((const void *)buf_ptr, count)) {
        return -1;
    }

    if (fd >= MAX_FDS) {
        return -1;
    }

    struct fd_table *fdt = process_get_fd_table();
    struct file_descriptor *file_desc = vfs_get_fd(fdt, (int)fd);

    if (file_desc == NULL) {
        return -1;
    }

    struct vnode *vn = file_desc->vn;
    if (vn->type != VNODE_DIR) {
        return -1;  /* Not a directory */
    }

    /* Use position to track if we've already read the directory */
    if (file_desc->position != 0) {
        return 0;  /* Already read, return EOF */
    }

    struct tar_file *tf = (struct tar_file *)vn->fs_data;

    /* Get directory path (remove trailing slash for matching) */
    char dir_path[256];
    strncpy(dir_path, tf->name, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    size_t len = strlen(dir_path);
    if (len > 0 && dir_path[len - 1] == '/') {
        dir_path[len - 1] = '\0';
    }

    /* Set up context for callback */
    struct getdents_ctx ctx;
    ctx.buf = (uint8_t *)buf_ptr;
    ctx.buf_size = count;
    ctx.bytes_written = 0;
    ctx.count = 0;

    /* List directory contents */
    tar_list_dir(dir_path, getdents_callback, &ctx);

    /* Mark as read */
    file_desc->position = 1;

    return (int64_t)ctx.bytes_written;
}

/*
 * sys_getcwd - Get current working directory.
 */
static int64_t sys_getcwd(uint64_t buf_ptr, uint64_t size) {
    if (!vmm_validate_user_range((const void *)buf_ptr, size)) {
        return -1;
    }

    const char *cwd = process_get_cwd();
    size_t cwd_len = strlen(cwd) + 1;

    if (cwd_len > size) {
        return -1;  /* Buffer too small */
    }

    memcpy((void *)buf_ptr, cwd, cwd_len);
    return (int64_t)buf_ptr;
}

/*
 * sys_chdir - Change current working directory.
 *
 * Handles:
 *   /path     - absolute paths
 *   path      - relative to cwd
 *   .         - current directory (no-op)
 *   ..        - parent directory
 */
static int64_t sys_chdir(uint64_t path_ptr) {
    const char *path = (const char *)path_ptr;

    if (!vmm_validate_user_range((const void *)path_ptr, 1)) {
        return -1;
    }

    /* Handle root directory */
    if (strcmp(path, "/") == 0) {
        process_set_cwd("/");
        return 0;
    }

    /* Handle . (current directory - no change) */
    if (strcmp(path, ".") == 0) {
        return 0;
    }

    /* Build the new path */
    char new_path[256];
    const char *cwd = process_get_cwd();

    /* Handle .. (parent directory) */
    if (strcmp(path, "..") == 0) {
        /* Copy current cwd */
        strncpy(new_path, cwd, sizeof(new_path) - 1);
        new_path[sizeof(new_path) - 1] = '\0';

        /* Find last slash and truncate (but keep at least "/") */
        size_t len = strlen(new_path);
        if (len > 1) {
            /* Find the last slash */
            char *last_slash = new_path + len - 1;
            while (last_slash > new_path && *last_slash != '/') {
                last_slash--;
            }
            if (last_slash == new_path) {
                /* We're at root, stay at root */
                new_path[1] = '\0';
            } else {
                *last_slash = '\0';
            }
        }
        process_set_cwd(new_path);
        return 0;
    }

    /* Handle absolute vs relative paths */
    if (path[0] == '/') {
        /* Absolute path - use as-is */
        strncpy(new_path, path, sizeof(new_path) - 1);
        new_path[sizeof(new_path) - 1] = '\0';
    } else {
        /* Relative path - combine with cwd */
        if (strcmp(cwd, "/") == 0) {
            new_path[0] = '/';
            strncpy(new_path + 1, path, sizeof(new_path) - 2);
            new_path[sizeof(new_path) - 1] = '\0';
        } else {
            strncpy(new_path, cwd, sizeof(new_path) - 1);
            new_path[sizeof(new_path) - 1] = '\0';
            size_t cwd_len = strlen(new_path);
            if (cwd_len < sizeof(new_path) - 2) {
                new_path[cwd_len] = '/';
                strncpy(new_path + cwd_len + 1, path, sizeof(new_path) - cwd_len - 2);
                new_path[sizeof(new_path) - 1] = '\0';
            }
        }
    }

    /* Convert path to tarfs format (no leading slash) for lookup */
    const char *tar_path = new_path;
    if (tar_path[0] == '/') tar_path++;

    /* Verify the path exists (as a directory) */
    struct tar_file *tf = tar_find(tar_path);
    if (tf == NULL) {
        /* Try with trailing slash for directories */
        char path_with_slash[256];
        size_t len = strlen(tar_path);
        if (len < sizeof(path_with_slash) - 2) {
            strcpy(path_with_slash, tar_path);
            path_with_slash[len] = '/';
            path_with_slash[len + 1] = '\0';
            tf = tar_find(path_with_slash);
        }
    }

    if (tf == NULL) {
        return -1;  /* Path not found */
    }

    /* Must be a directory */
    if (!tf->is_dir) {
        return -1;  /* Not a directory */
    }

    /* Build final cwd path with leading slash */
    char final_cwd[256];
    final_cwd[0] = '/';
    strncpy(final_cwd + 1, tf->name, sizeof(final_cwd) - 2);
    final_cwd[sizeof(final_cwd) - 1] = '\0';

    /* Remove trailing slash */
    size_t len = strlen(final_cwd);
    if (len > 1 && final_cwd[len - 1] == '/') {
        final_cwd[len - 1] = '\0';
    }

    process_set_cwd(final_cwd);
    return 0;
}

/*
 * sys_isatty - Check if file descriptor refers to a terminal.
 */
static int64_t sys_isatty(uint64_t fd) {
    /* stdin, stdout, stderr are terminals */
    if (fd == 0 || fd == 1 || fd == 2) {
        return 1;
    }
    return 0;
}

/*
 * sys_dup - Duplicate a file descriptor.
 *
 * Returns the lowest available fd that is a copy of oldfd.
 */
static int64_t sys_dup(uint64_t oldfd) {
    if (oldfd >= MAX_FDS) {
        return -1;
    }

    struct fd_table *fdt = process_get_fd_table();
    struct file_descriptor *old_desc = vfs_get_fd(fdt, (int)oldfd);

    if (old_desc == NULL) {
        return -1;  /* oldfd not open */
    }

    /* Find lowest available fd */
    int newfd = vfs_alloc_fd(fdt);
    if (newfd < 0) {
        return -1;  /* No free fd */
    }

    /* Copy the file descriptor entry */
    fdt->fds[newfd].vn = old_desc->vn;
    fdt->fds[newfd].position = old_desc->position;
    fdt->fds[newfd].flags = old_desc->flags;

    /* Increment vnode refcount */
    old_desc->vn->refcount++;

    return newfd;
}

/*
 * sys_dup2 - Duplicate a file descriptor to a specific fd number.
 *
 * If newfd is already open, it is closed first.
 * If oldfd == newfd, just return newfd.
 */
static int64_t sys_dup2(uint64_t oldfd, uint64_t newfd) {
    if (oldfd >= MAX_FDS || newfd >= MAX_FDS) {
        return -1;
    }

    /* If oldfd == newfd, just return newfd */
    if (oldfd == newfd) {
        /* But first verify oldfd is valid */
        struct fd_table *fdt = process_get_fd_table();
        struct file_descriptor *old_desc = vfs_get_fd(fdt, (int)oldfd);
        if (old_desc == NULL) {
            return -1;
        }
        return (int64_t)newfd;
    }

    struct fd_table *fdt = process_get_fd_table();
    struct file_descriptor *old_desc = vfs_get_fd(fdt, (int)oldfd);

    if (old_desc == NULL) {
        return -1;  /* oldfd not open */
    }

    /* If newfd is open, close it first */
    struct file_descriptor *new_desc = vfs_get_fd(fdt, (int)newfd);
    if (new_desc != NULL) {
        /* Close newfd */
        new_desc->vn->ops->close(new_desc->vn);
        vfs_free_fd(fdt, (int)newfd);
    }

    /* Copy the file descriptor entry */
    fdt->fds[newfd].vn = old_desc->vn;
    fdt->fds[newfd].position = old_desc->position;
    fdt->fds[newfd].flags = old_desc->flags;

    /* Increment vnode refcount */
    old_desc->vn->refcount++;

    return (int64_t)newfd;
}

/* =============================================================================
 * Spawn Helper Functions
 * =============================================================================
 */

/*
 * spawn_copy_argv - Copy argv from user space to process exec buffers.
 *
 * @p:         Process whose exec_args/exec_argv buffers to use
 * @argv:      User-space argv array
 * @argv_ptr:  Raw pointer for validation
 * @path:      Program path (used as argv[0] if no args provided)
 * @argc_out:  Output: argument count
 *
 * Uses p->exec_args for string storage and p->exec_argv for pointers.
 * These per-process buffers avoid race conditions with concurrent spawns.
 *
 * Returns: 0 on success, -1 on error.
 */
static int spawn_copy_argv(struct process *p, char **argv, uint64_t argv_ptr,
                           const char *path, int *argc_out) {
    int argc = 0;

    if (argv != NULL && vmm_validate_user_range((const void *)argv_ptr, 8)) {
        while (argc < EXEC_MAX_ARGS && argv[argc] != NULL) {
            if (!vmm_validate_user_range(argv[argc], 1)) break;
            strncpy(p->exec_args[argc], argv[argc], EXEC_MAX_ARG_LEN - 1);
            p->exec_args[argc][EXEC_MAX_ARG_LEN - 1] = '\0';
            p->exec_argv[argc] = p->exec_args[argc];
            argc++;
        }
    }
    p->exec_argv[argc] = NULL;

    /* If no args provided, use the program path as argv[0] */
    if (argc == 0) {
        strncpy(p->exec_args[0], path, EXEC_MAX_ARG_LEN - 1);
        p->exec_args[0][EXEC_MAX_ARG_LEN - 1] = '\0';
        p->exec_argv[0] = p->exec_args[0];
        argc = 1;
        p->exec_argv[1] = NULL;
    }

    *argc_out = argc;
    return 0;
}

/*
 * spawn_create_child - Create and load a child process.
 *
 * @path_ptr:  User pointer to program path
 * @cwd:       Current working directory to inherit
 * @child_out: Output: created child process
 *
 * Returns: 0 on success, -1 on error.
 */
static int spawn_create_child(uint64_t path_ptr, const char *cwd,
                              struct process **child_out) {
    const char *path = (const char *)path_ptr;

    /* Validate path pointer */
    if (!vmm_validate_user_range((const void *)path_ptr, 1)) {
        return -1;
    }

    /* Resolve executable path */
    char full_path[PATH_MAX];
    if (vfs_resolve_executable(path, cwd, full_path, sizeof(full_path)) != 0) {
        return -1;
    }

    /* Look up executable via VFS */
    const void *data;
    size_t size;
    if (vfs_lookup_executable(full_path, &data, &size) != 0) {
        return -1;  /* Program not found */
    }

    /* Create child process */
    struct process *child = process_create();
    if (child == NULL) {
        return -1;
    }

    /* Inherit cwd from parent */
    process_set_cwd_for(child, cwd);

    /* Load ELF executable */
    if (process_load_elf(child, data, size) != 0) {
        process_destroy(child);
        return -1;
    }

    *child_out = child;
    return 0;
}

/* =============================================================================
 * Spawn Syscalls
 * =============================================================================
 */

/*
 * sys_spawn - Run a program and wait for it to complete.
 *
 * @path_ptr: Path to the program (e.g., "/bin/hello" or "hello")
 * @argv_ptr: NULL-terminated array of argument strings
 *
 * Returns: Exit code of the program, or -1 on error.
 *
 * This is like system() in C - runs a program and waits for completion.
 */
static int64_t sys_spawn(uint64_t path_ptr, uint64_t argv_ptr) {
    const char *path = (const char *)path_ptr;
    char **argv = (char **)argv_ptr;
    const char *cwd = process_get_cwd();
    struct process *parent = process_get_current();

    /* Create and load child process */
    struct process *child;
    if (spawn_create_child(path_ptr, cwd, &child) != 0) {
        return -1;
    }

    /* Copy argv from user space into parent's exec buffers */
    int argc;
    spawn_copy_argv(parent, argv, argv_ptr, path, &argc);

    /*
     * Save parent's saved_kernel_rsp before nested context switch.
     * This is critical for nested spawns (shell spawns ls, then shell exits).
     * Without this, the global saved_kernel_rsp gets overwritten and
     * returning from shell would use the wrong stack.
     */
    uint64_t parent_saved_rsp = saved_kernel_rsp;

    /* Run child process - this blocks until child calls exit */
    int exit_code = process_run_with_args(child, argc, parent->exec_argv);

    /* Restore parent's saved_kernel_rsp for when parent later exits */
    saved_kernel_rsp = parent_saved_rsp;

    /* Clean up child */
    process_destroy(child);

    /* Restore parent as current process */
    process_set_current(parent);

    /* Switch back to parent's address space */
    vmm_switch_address_space(parent->pml4);

    return exit_code;
}

/*
 * sys_spawn_async - Spawn a program without waiting for it to complete.
 *
 * @path_ptr: Path to the program
 * @argv_ptr: NULL-terminated array of argument strings
 *
 * Returns: Child PID on success, or -1 on error.
 *
 * Unlike sys_spawn, this returns immediately after starting the child.
 * Use sys_waitpid to wait for the child to complete.
 */
static int64_t sys_spawn_async(uint64_t path_ptr, uint64_t argv_ptr) {
    const char *path = (const char *)path_ptr;
    char **argv = (char **)argv_ptr;
    const char *cwd = process_get_cwd();
    struct process *parent = process_get_current();

    /* Create and load child process */
    struct process *child;
    if (spawn_create_child(path_ptr, cwd, &child) != 0) {
        return -1;
    }

    /* Copy argv from user space into parent's exec buffers */
    int argc;
    spawn_copy_argv(parent, argv, argv_ptr, path, &argc);

    /* Set up argc/argv on child's stack */
    child->stack = process_setup_argv(child, argc, parent->exec_argv);
    if (child->stack == 0) {
        process_destroy(child);
        return -1;  /* Too many arguments */
    }

    /* Start the child process (non-blocking) */
    process_start(child);

    return child->pid;
}

/*
 * sys_shutdown - Halt the system.
 *
 * Disables interrupts and halts the CPU.
 * Never returns.
 */
static void sys_shutdown(void) {
    puts("\nSystem is shutting down...\n");
    puts("You may now power off your computer.\n");
    __asm__ volatile("cli; hlt");
    /* Never reached */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/*
 * sys_reboot - Reboot the system.
 *
 * Sends reset command to the keyboard controller.
 * Never returns.
 */
static void sys_reboot(void) {
    puts("\nSystem is rebooting...\n");

    /* Wait for keyboard controller to be ready */
    uint8_t status;
    do {
        __asm__ volatile("inb $0x64, %0" : "=a"(status));
    } while (status & 0x02);

    /* Send reset command to keyboard controller */
    __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));

    /* If that didn't work, triple fault by loading invalid IDT */
    __asm__ volatile(
        "lidt %0\n"
        "int $3\n"
        : : "m"(*(uint64_t *)0)
    );

    /* Should never reach here */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/*
 * sys_waitpid - Wait for a child process to complete.
 *
 * @pid: Process ID to wait for
 *
 * Returns: Exit code of the child, or -1 on error.
 */
static int64_t sys_waitpid(uint64_t pid) {
    struct process *parent = process_get_current();
    struct process *child = process_find_by_pid((int)pid);

    if (child == NULL) {
        return -1;  /* Process not found */
    }

    /* If child already exited, just reap it */
    if (child->state == PROC_ZOMBIE) {
        int exit_code = child->exit_code;
        process_destroy(child);
        return exit_code;
    }

    /* Block parent until child exits */
    sched_block_on_pid(parent, (int)pid);

    /* Wait for child to exit and wake us up */
    while (child->state != PROC_ZOMBIE) {
        /* Enable interrupts and halt - timer will fire and run scheduler */
        __asm__ volatile("sti; hlt; cli");
    }

    /* Child has exited - reap it */
    int exit_code = child->exit_code;
    process_destroy(child);

    return exit_code;
}

/* =============================================================================
 * Syscall Dispatcher
 * =============================================================================
 */

/*
 * syscall_handler - Dispatch system calls based on RAX.
 *
 * @regs: Pointer to saved registers from the INT 0x80 trap.
 *
 * The syscall number is in regs->rax. Arguments are in:
 *   - regs->rdi (arg1)
 *   - regs->rsi (arg2)
 *   - regs->rdx (arg3)
 *   - regs->rcx (arg4)
 *
 * Return value (if any) is placed in regs->rax.
 */
void syscall_handler(struct syscall_registers *regs) {
    /* Debug: print which syscall was invoked */
    // puts("Syscall invoked: ");
    // put_dec(regs->rax);
    // puts("\n");

    switch (regs->rax) {
        case SYS_EXIT:
            sys_exit(regs->rdi);
            /* sys_exit never returns */
            break;

        case SYS_WRITE:
            regs->rax = sys_write(regs->rdi, regs->rsi, regs->rdx);
            break;
        
        case SYS_READ:
            regs->rax = sys_read(regs->rdi, regs->rsi, regs->rdx);
            break;
            
        case SYS_GETPID:
            regs->rax = sys_getpid();
            break;
        
        case SYS_UPTIME:
            regs->rax = sys_getuptime();
            break;

        case SYS_SBRK:
            regs->rax = sys_brk(regs->rdi);
            break;
        case SYS_OPEN:
            regs->rax = sys_open(regs->rdi, regs->rsi);
            break;
        case SYS_CLOSE:
            regs->rax = sys_close(regs->rdi);
            break;
        case SYS_LSEEK:
            regs->rax = sys_lseek(regs->rdi, (int64_t)regs->rsi, regs->rdx);
            break;

        case SYS_STAT:
            regs->rax = sys_stat(regs->rdi, regs->rsi);
            break;

        case SYS_FSTAT:
            regs->rax = sys_fstat(regs->rdi, regs->rsi);
            break;

        case SYS_GETDENTS:
            regs->rax = sys_getdents(regs->rdi, regs->rsi, regs->rdx);
            break;

        case SYS_GETCWD:
            regs->rax = sys_getcwd(regs->rdi, regs->rsi);
            break;

        case SYS_CHDIR:
            regs->rax = sys_chdir(regs->rdi);
            break;

        case SYS_ISATTY:
            regs->rax = sys_isatty(regs->rdi);
            break;

        case SYS_DUP:
            regs->rax = sys_dup(regs->rdi);
            break;

        case SYS_DUP2:
            regs->rax = sys_dup2(regs->rdi, regs->rsi);
            break;

        case SYS_SPAWN:
            regs->rax = sys_spawn(regs->rdi, regs->rsi);
            break;

        case SYS_SPAWN_ASYNC:
            regs->rax = sys_spawn_async(regs->rdi, regs->rsi);
            break;

        case SYS_WAITPID:
            regs->rax = sys_waitpid(regs->rdi);
            break;

        case SYS_SHUTDOWN:
            sys_shutdown();
            /* Never returns */
            break;

        case SYS_REBOOT:
            sys_reboot();
            /* Never returns */
            break;

        default:
            puts("Unknown syscall: ");
            put_dec(regs->rax);
            puts("\n");
            regs->rax = SYSCALL_ERROR;
            break;
    }
}