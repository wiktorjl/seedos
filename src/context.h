/*
 * context.h - User/Kernel Context Switching
 *
 * This module handles transitions between kernel mode (ring 0) and
 * user mode (ring 3). It's the foundation for running user programs.
 *
 * Context Switch Overview:
 *
 *   Kernel → User (context_switch_to_user):
 *
 *   1. Save kernel state (RSP, RBP, callee-saved registers)
 *   2. Set TSS.RSP0 so interrupts have a kernel stack
 *   3. Switch to user's address space (CR3 = user PML4)
 *   4. Build an IRETQ frame on the kernel stack
 *   5. Execute IRETQ to jump to user code at ring 3
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │  Kernel Stack (before IRETQ)                                │
 *   │                                                             │
 *   │  ┌───────────────┐  ← RSP                                  │
 *   │  │ SS (user)     │  GDT_USER_DATA | 3                      │
 *   │  │ RSP (user)    │  User stack pointer                     │
 *   │  │ RFLAGS        │  Flags with IF=1 (interrupts enabled)   │
 *   │  │ CS (user)     │  GDT_USER_CODE | 3                      │
 *   │  │ RIP (user)    │  Entry point of user program            │
 *   │  └───────────────┘                                         │
 *   │                                                             │
 *   │  IRETQ pops these and jumps to user RIP at ring 3          │
 *   └─────────────────────────────────────────────────────────────┘
 *
 *   User → Kernel (context_return_to_kernel):
 *
 *   Called by sys_exit() to return from userspace:
 *   1. Switch back to kernel address space
 *   2. Restore saved kernel RSP
 *   3. Restore callee-saved registers
 *   4. Return to where context_switch_to_user() was called
 *
 * Key Insight:
 *
 *   context_switch_to_user() "blocks" until sys_exit() is called.
 *   From the caller's perspective, it's like a function that runs
 *   the user program and returns when the program exits.
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "types.h"

/* Note: USER_STACK_SIZE is now defined in process.h */

/* =============================================================================
 * User Context Structure
 *
 * Contains everything needed to enter a user program.
 * =============================================================================
 */
struct user_context {
    uint64_t pml4;      /* Physical address of user's PML4 (page tables) */
    uint64_t entry;     /* Entry point address (initial RIP) */
    uint64_t stack;     /* User stack pointer (initial RSP) */
};

/* =============================================================================
 * Context Switch API
 * =============================================================================
 */

/*
 * context_switch_to_user - Enter user mode and run until sys_exit.
 *
 * @ctx: Pointer to user_context with PML4, entry point, and stack.
 *
 * This function:
 *   1. Saves kernel state so we can return later
 *   2. Configures TSS.RSP0 for interrupt handling
 *   3. Switches to user's address space
 *   4. Jumps to user code via IRETQ
 *
 * When the user program calls sys_exit(), execution continues after
 * this function returns (as if it were a normal function call).
 *
 * This is a "coroutine-like" pattern: the kernel suspends while
 * user code runs, and resumes when user code exits.
 */
void context_switch_to_user(struct user_context *ctx);

/*
 * context_return_to_kernel - Return from user mode to kernel.
 *
 * Called by sys_exit() after switching back to kernel address space.
 * Restores the kernel stack and registers saved by context_switch_to_user(),
 * effectively "returning" from that function.
 *
 * This function does not return (noreturn attribute) because it
 * transfers control back to context_switch_to_user's caller.
 */
void context_return_to_kernel(void) __attribute__((noreturn));

/* =============================================================================
 * Saved Kernel State
 *
 * This global is set by context_switch_to_user() and read by
 * context_return_to_kernel(). It's defined in context_asm.S.
 * =============================================================================
 */

/*
 * saved_kernel_rsp - Kernel stack pointer saved during context switch.
 *
 * When we enter user mode, we save RSP here. When returning from
 * user mode, we restore RSP from here to continue kernel execution.
 */
extern uint64_t saved_kernel_rsp;

#endif /* CONTEXT_H */
