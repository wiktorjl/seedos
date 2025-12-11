#include "syscall.h"

/*
 * syscall.c - System call handler
 *
 * Currently supports:
 *   - SYS_EXIT: terminate the current process
 *   - SYS_WRITE: write to console
 */
#include "console.h"
#include "vmm.h"
#include "context.h"

void sys_exit(uint64_t exit_code) {
    puts("\n========================================\n");
    puts("Process exited with code ");
    put_dec(exit_code);
    puts("\n========================================\n");

    /* Switch back to kernel address space */
    vmm_switch_address_space(vmm_get_kernel_pml4());

    /* Return to kernel (where context_save_kernel_state was called) */
    context_return_to_kernel();
}

uint64_t sys_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    const char *buf = (const char *)buffer;

    if(fd != 1) {
        return 0;  // Only support stdout (fd=1)
    }

    for (uint64_t i = 0; i < count; i++) {
        putc(buf[i]);
    }
    return count;
}

void syscall_handler(struct registers *regs) {
    puts("Syscall invoked: ");
    put_dec(regs->rax);
    puts("\n");

    // regs->rax = syscall number
    switch(regs->rax) {
        case SYS_EXIT:
            sys_exit(regs->rdi);
            return;
        case SYS_WRITE:
            // regs->rdi = arg1 (fd for write, exit code for exit)
            // regs->rsi = arg2 (buffer for write)
            // regs->rdx = arg3 (length for write)
            regs->rax = sys_write(regs->rdi, regs->rsi, regs->rdx);
            return;
        default:
            puts("Unknown syscall: ");
            put_dec(regs->rax);
            puts("\n");
            regs->rax = -1;  // Return error
            break;
    }

}


