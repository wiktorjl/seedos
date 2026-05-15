/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common ISR stub and per-vector entry points.
 *
 * Each per-vector stub pushes an error code (real or dummy) plus the
 * vector number, then jumps to isr_common. isr_common saves the GPRs,
 * conditionally swaps to the kernel GS base when the trap originated
 * in CPL=3, clears the direction flag per the SysV ABI, and dispatches
 * to the C handler.
 *
 * Stack layout at the point we test CS (after the 15 GPR pushes):
 *   0(%rsp)   r15
 *   ...
 *   112(%rsp) rax
 *   120(%rsp) int_no   (pushed by stub)
 *   128(%rsp) error_code (pushed by CPU or stub)
 *   136(%rsp) rip      (pushed by CPU)
 *   144(%rsp) cs       (pushed by CPU - low 2 bits = CPL)
 */

.section .text

isr_common:

    /* Save all general-purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * If we trapped from user mode (CPL=3 in saved CS), swap to the
     * kernel GS base. Skip when CPL=0 - the kernel GS is already
     * active and a second swapgs would corrupt it.
     */
    testb $3, 144(%rsp)
    jz 1f
    swapgs
1:

    /* SysV ABI requires DF=0 on entry to C; CPU does not clear it for us. */
    cld

    /* First argument (rdi) = pointer to interrupt_frame (current rsp) */
    movq %rsp, %rdi

    /* Call C handler */
    call interrupt_handler

    /* Mirror the entry-time swapgs based on the CS we are returning to. */
    testb $3, 144(%rsp)
    jz 2f
    swapgs
2:

    /* Restore all general-purpose registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* Remove error code and interrupt number from stack */
    addq $16, %rsp

    /* Return from interrupt */
    iretq

.macro ISR_NOERR num
.global isr_\num
isr_\num:
    pushq $0            /* Dummy error code */
    pushq $\num         /* Interrupt number */
    jmp isr_common
.endm

/*
 * Macro for exceptions that DO push an error code
 */
.macro ISR_ERR num
.global isr_\num
isr_\num:
    pushq $\num         /* Interrupt number (error code already on stack) */
    jmp isr_common
.endm


ISR_NOERR 0    /* Divide Error */
ISR_NOERR 1    /* Debug */
ISR_NOERR 2    /* NMI */
ISR_NOERR 3    /* Breakpoint */
ISR_NOERR 4    /* Overflow */
ISR_NOERR 5    /* Bound Range Exceeded */
ISR_NOERR 6    /* Invalid Opcode */
ISR_NOERR 7    /* Device Not Available */
ISR_ERR   8    /* Double Fault */
ISR_NOERR 9    /* Coprocessor Segment Overrun (reserved) */
ISR_ERR   10   /* Invalid TSS */
ISR_ERR   11   /* Segment Not Present */
ISR_ERR   12   /* Stack-Segment Fault */
ISR_ERR   13   /* General Protection Fault */
ISR_ERR   14   /* Page Fault */
ISR_NOERR 15   /* Reserved */
ISR_NOERR 16   /* x87 FP Exception */
ISR_ERR   17   /* Alignment Check */
ISR_NOERR 18   /* Machine Check */
ISR_NOERR 19   /* SIMD FP Exception */
ISR_NOERR 20   /* Virtualization Exception */
ISR_ERR   21   /* Control Protection Exception */
ISR_NOERR 22   /* Reserved */
ISR_NOERR 23   /* Reserved */
ISR_NOERR 24   /* Reserved */
ISR_NOERR 25   /* Reserved */
ISR_NOERR 26   /* Reserved */
ISR_NOERR 27   /* Reserved */
ISR_NOERR 28   /* Reserved */
ISR_NOERR 29   /* Reserved */
ISR_ERR   30   /* Security Exception */
ISR_NOERR 31   /* Reserved */

/* Hardware IRQs (vectors 32-47) - none push error codes */
ISR_NOERR 32   /* IRQ 0: APIC Timer */
ISR_NOERR 33   /* IRQ 1 */
ISR_NOERR 34   /* IRQ 2 */
ISR_NOERR 35   /* IRQ 3 */
ISR_NOERR 36   /* IRQ 4 */
ISR_NOERR 37   /* IRQ 5 */
ISR_NOERR 38   /* IRQ 6 */
ISR_NOERR 39   /* IRQ 7 */
ISR_NOERR 40   /* IRQ 8 */
ISR_NOERR 41   /* IRQ 9 */
ISR_NOERR 42   /* IRQ 10 */
ISR_NOERR 43   /* IRQ 11 */
ISR_NOERR 44   /* IRQ 12 */
ISR_NOERR 45   /* IRQ 13 */
ISR_NOERR 46   /* IRQ 14 */
ISR_NOERR 47   /* IRQ 15 */

/* Spurious interrupt vector */
ISR_NOERR 255  /* Spurious interrupt */
