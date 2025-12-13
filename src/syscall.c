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

        default:
            puts("Unknown syscall: ");
            put_dec(regs->rax);
            puts("\n");
            regs->rax = SYSCALL_ERROR;
            break;
    }
}