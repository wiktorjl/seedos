/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Syscall Entry/Exit (sysretq version)
 *
 * This is where the CPU jumps when user code executes syscall.
 * We use sysretq for return - simpler and faster than iretq.
 *
 * On entry from syscall instruction:
 *   RCX = user RIP (return address)
 *   R11 = user RFLAGS
 *   RAX = syscall number
 *   RDI, RSI, RDX, R10, R8, R9 = arguments (Linux x86-64 ABI)
 *   RSP = user stack (NOT switched by syscall instruction!)
 *
 * On sysretq:
 *   RIP = RCX
 *   RFLAGS = R11
 *
 * Note: Linux uses R10 for arg4 because RCX is clobbered by syscall.
 */

/*
 * Constants - must match C headers!
 */

/* Per-CPU offsets (from percpu.h) */
#define PERCPU_KERNEL_RSP   0x08
#define PERCPU_USER_RSP     0x10

.section .text

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    /*
     * Step 1: Switch to kernel GS and stack
     */
    swapgs
    movq %rsp, %gs:PERCPU_USER_RSP      /* Save user RSP */
    movq %gs:PERCPU_KERNEL_RSP, %rsp    /* Load kernel RSP */

    /*
     * Step 2: Save registers for sysretq
     */
    pushq %rcx                          /* User RIP */
    pushq %r11                          /* User RFLAGS */

    /*
     * Step 3: Build syscall_frame_t on stack
     *
     * Push in reverse order so struct layout matches.
     * syscall_frame_t: { nr, arg1, arg2, arg3, arg4, arg5, arg6 }
     */
    pushq %r9                           /* arg6 */
    pushq %r8                           /* arg5 */
    pushq %r10                          /* arg4 (Linux uses R10, not RCX) */
    pushq %rdx                          /* arg3 */
    pushq %rsi                          /* arg2 */
    pushq %rdi                          /* arg1 */
    pushq %rax                          /* syscall number */

    /*
     * Step 4: Call syscall_dispatch with pointer to frame
     */
    movq %rsp, %rdi                     /* RDI = pointer to syscall_frame_t */

    sti
    call syscall_dispatch
    cli

    /*
     * Step 5: Clean up syscall frame
     * Return value is in RAX - leave it there for userspace.
     */
    addq $56, %rsp                      /* Pop syscall_frame_t (7 * 8 bytes) */

    /*
     * Step 6: Restore sysret registers and return
     */
    popq %r11                           /* User RFLAGS */
    popq %rcx                           /* User RIP */

    movq %gs:PERCPU_USER_RSP, %rsp      /* Restore user RSP */

    swapgs
    sysretq

.size syscall_entry, . - syscall_entry
