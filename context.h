/*
 * context.h - User/Kernel context switching
 *
 * Handles the transition between kernel mode (ring 0) and user mode (ring 3).
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdint.h>

/*
 * User context - everything needed to enter userspace
 */
struct user_context {
    uint64_t pml4;      /* Physical address of user's PML4 */
    uint64_t entry;     /* Entry point (RIP) */
    uint64_t stack;     /* User stack pointer (RSP) */
};

/*
 * Enter userspace and run until sys_exit.
 *
 * This function:
 * 1. Saves kernel state (so sys_exit can return here)
 * 2. Sets TSS.RSP0 for kernel stack on interrupts
 * 3. Switches to user address space
 * 4. Jumps to userspace via iretq
 *
 * When user calls sys_exit, execution resumes after this call.
 */
void context_switch_to_user(struct user_context *ctx);

/*
 * Return from userspace to kernel.
 * Called by sys_exit after switching back to kernel address space.
 * Re-enables interrupts and returns to where context_switch_to_user() was called.
 */
void context_return_to_kernel(void) __attribute__((noreturn));

/*
 * Global kernel RSP saved by context_switch_to_user().
 * Used by context_return_to_kernel() to restore stack.
 */
extern uint64_t saved_kernel_rsp;

#endif /* CONTEXT_H */
