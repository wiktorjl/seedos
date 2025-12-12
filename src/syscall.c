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

/* File descriptor for standard output (stdout) */
#define FD_STDOUT 1

/* Error return value for syscalls */
#define SYSCALL_ERROR ((uint64_t)-1)

/* =============================================================================
 * Individual Syscall Implementations
 * =============================================================================
 */

/*
 * sys_exit - Terminate the current process.
 *
 * @exit_code: The exit code to report (0 = success, non-zero = error).
 *
 * This function does not return. It:
 *   1. Prints the exit code for debugging
 *   2. Switches back to the kernel's address space
 *   3. Returns control to the kernel (via context_return_to_kernel)
 */
static void sys_exit(uint64_t exit_code) {
    puts("\n========================================\n");
    puts("Process exited with code ");
    put_dec(exit_code);
    puts("\n========================================\n");

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

    if (buffer >= 0x0000800000000000ULL || buffer + count >= 0x0000800000000000ULL) {
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
        
        case SYS_GETPID:
            regs->rax = sys_getpid();
            break;
        
        case SYS_UPTIME:
            regs->rax = sys_getuptime();
            break;

        case SYS_SBRK:
            regs->rax = sys_brk(regs->rdi);
            break;

        default:
            puts("Unknown syscall: ");
            put_dec(regs->rax);
            puts("\n");
            regs->rax = SYSCALL_ERROR;
            break;
    }
}
