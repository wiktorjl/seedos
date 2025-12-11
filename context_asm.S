/*
 * context_asm.S - Low-level context switching assembly
 */

.section .note.GNU-stack, "", @progbits

.section .data
.global saved_kernel_rsp
saved_kernel_rsp:
    .quad 0

.section .text
.code64

/*
 * void context_switch_to_user(struct user_context *ctx)
 *
 * Arguments:
 *   %rdi = pointer to struct user_context { pml4, entry, stack }
 *
 * This function:
 *   1. Saves kernel state (return address + RSP)
 *   2. Sets TSS.RSP0
 *   3. Switches to user address space
 *   4. Enters userspace via iretq
 *
 * When sys_exit calls context_return_to_kernel(), execution
 * resumes and this function returns normally.
 */
.global context_switch_to_user
context_switch_to_user:
    /*
     * Save callee-saved registers per System V AMD64 ABI.
     * We need these to survive the function calls and iretq.
     */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Save ctx pointer in callee-saved register */
    movq %rdi, %r12

    /*
     * Save RSP. When context_return_to_kernel() restores this,
     * we'll pop the callee-saved registers and return normally.
     */
    movq %rsp, saved_kernel_rsp(%rip)

    /*
     * Set TSS.RSP0 to current RSP.
     * This is the stack the CPU will use when an interrupt
     * occurs in ring 3.
     */
    movq %rsp, %rdi
    call tss_set_kernel_stack

    /*
     * Switch to user address space.
     * ctx->pml4 is at offset 0.
     */
    movq 0(%r12), %rdi
    call vmm_switch_address_space

    /*
     * Build iretq frame on stack.
     * ctx->entry is at offset 8, ctx->stack is at offset 16.
     */
    movq 8(%r12), %rcx      /* entry point */
    movq 16(%r12), %r8      /* user stack */

    pushq $0x23             /* SS = GDT_USER_DATA | 3 */
    pushq %r8               /* RSP = user stack */
    pushq $0x202            /* RFLAGS with IF=1 */
    pushq $0x1b             /* CS = GDT_USER_CODE | 3 */
    pushq %rcx              /* RIP = entry point */

    /* Clear registers for clean entry to userspace */
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    xorq %rbp, %rbp

    /* Jump to ring 3! */
    iretq

    /*
     * context_return_to_kernel() jumps here by restoring RSP
     * and we continue below...
     */
.global context_resume_from_user
context_resume_from_user:
    /* Restore callee-saved registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    /* Return to caller of context_switch_to_user() */
    ret


/*
 * void context_return_to_kernel(void)
 *
 * Returns from userspace to kernel by restoring saved_kernel_rsp.
 * This effectively "returns" from context_switch_to_user().
 *
 * Called by sys_exit after switching back to kernel address space.
 */
.global context_return_to_kernel
context_return_to_kernel:
    /* Restore kernel stack */
    movq saved_kernel_rsp(%rip), %rsp

    /* Re-enable interrupts (disabled by int 0x80) */
    sti

    /* Jump to resume point (right after iretq in context_switch_to_user) */
    jmp context_resume_from_user
