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
#include "vmm.h"
#include "context.h"
#include "pit.h"
#include <stdint.h>
#include "keyboard.h"
#include "vfs.h"
#include "tarfs.h"
#include "tar.h"
#include "string.h"

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

/* File descriptor for standard output (stdout) */
#define FD_STDOUT 1
#define FD_STDIN 0

/* Error return value for syscalls */
#define SYSCALL_ERROR ((uint64_t)-1)

/* =============================================================================
 * Individual Syscall Implementations
 * =============================================================================
 */


static int64_t sys_open(uint64_t path_ptr, uint64_t flags) {
    const char *path = (const char *)path_ptr;

    // 1. Validate user pointer (use vmm_validate_user_string or similar)
    if (!vmm_validate_user_range((const void *)path_ptr, 1)) {
        puts("Error: Invalid user path pointer\n");
        return -1;
    }

    // 2. Get current process fd_table: process_get_fd_table()
    struct fd_table *fdt = process_get_fd_table();
    
    // 3. Allocate fd: vfs_alloc_fd(fdt)
    int fd = vfs_alloc_fd(fdt);
    if (fd == -1) {
        return -1;  // No free file descriptors
    }

    // 4. Open vnode: tarfs_open(path)
    struct vnode *vn = tarfs_open(path);

    if(vn == NULL) {
        vfs_free_fd(fdt, fd);
        return -1;  // File not found
    }

    // 6. Set up fd entry: fdt->fds[fd].vn = vnode, .position = 0, .flags = flags
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
    vn->ops->close(vn);  // 3. Get vnode from file_descriptor
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
 *   2. Saves the exit code in the process structure
 *   3. Returns control to the kernel (via context_return_to_kernel)
 */
static void sys_exit(uint64_t exit_code) {
    /* Switch back to kernel address space before returning */
    vmm_switch_address_space(vmm_get_kernel_pml4());

    /* Save the exit code in the process structure */
    process_set_exit_code(exit_code);

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
 * TODO: Validate that buffer points to valid user memory!
 * Currently we trust the user pointer, which is unsafe.
 */
static uint64_t sys_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    const char *buf = (const char *)buffer;

    if (!vmm_validate_user_range((const void *)buffer, count)) {
        puts("Error: Invalid user buffer address\n");
        return 0;
    }

    /* Only stdout is supported for now */
    if (fd != FD_STDOUT) {
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

    if (!vmm_validate_user_range((const void *)buffer, count)) {
        puts("Error: Invalid user buffer address\n");
        return 0;
    }

    if (fd == FD_STDIN) {
        /* Read available chars (returns actual count, may be 0) */
        size_t bytes_read = keyboard_read(buf, count);
        return bytes_read;
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

    /* Find file in tarfs */
    struct tar_file *tf = tar_find(path);
    if (tf == NULL) {
        return -1;  /* File not found */
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
 */
static int64_t sys_chdir(uint64_t path_ptr) {
    const char *path = (const char *)path_ptr;

    if (!vmm_validate_user_range((const void *)path_ptr, 1)) {
        return -1;
    }

    /* Handle root directory specially */
    if (strcmp(path, "/") == 0) {
        process_set_cwd("/");
        return 0;
    }

    /* Verify the path exists (as a directory) */
    struct tar_file *tf = tar_find(path);
    if (tf == NULL) {
        /* Try with trailing slash for directories */
        char path_with_slash[256];
        size_t len = strlen(path);
        if (len < sizeof(path_with_slash) - 2) {
            strcpy(path_with_slash, path);
            path_with_slash[len] = '/';
            path_with_slash[len + 1] = '\0';
            tf = tar_find(path_with_slash);
        }
    }

    if (tf == NULL) {
        return -1;  /* Path not found */
    }

    /* Update cwd */
    char new_cwd[256];
    new_cwd[0] = '/';
    strncpy(new_cwd + 1, tf->name, sizeof(new_cwd) - 2);
    new_cwd[sizeof(new_cwd) - 1] = '\0';

    /* Remove trailing slash */
    size_t len = strlen(new_cwd);
    if (len > 1 && new_cwd[len - 1] == '/') {
        new_cwd[len - 1] = '\0';
    }

    process_set_cwd(new_cwd);
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

        default:
            puts("Unknown syscall: ");
            put_dec(regs->rax);
            puts("\n");
            regs->rax = SYSCALL_ERROR;
            break;
    }
}