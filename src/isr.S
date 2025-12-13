/*
 * isr.S - Interrupt Service Routine stubs
 *
 * These stubs save CPU state, call the C handler, restore state, and return.
 * Some exceptions push an error code, some don't - we normalize by pushing
 * a dummy error code (0) for those that don't.
 */

.section .text

/* Common handler - all stubs jump here after pushing int_no and error_code */
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

    /* First argument (rdi) = pointer to interrupt_frame (current stack pointer) */
    movq %rsp, %rdi
    
    /* Call C handler */
    call interrupt_handler

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

isr_common_syscall:

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

    /* First argument (rdi) = pointer to interrupt_frame (current stack pointer) */
    movq %rsp, %rdi

    /* Call C handler */
    call syscall_handler

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


/*
 * Macro for exceptions that DON'T push an error code
 * We push a dummy 0 to keep stack layout consistent
 */
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


.macro ISR_SYSCALL num
.global isr_\num
isr_\num:
    pushq $0            /* Dummy error code */
    pushq $\num         /* Interrupt number */
    jmp isr_common_syscall
.endm

/*
 * Exception handlers (0-31)
 * See Intel manual for which ones push error codes
 */
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

/* Hardware IRQs (32-47) - none push error codes */
ISR_NOERR 32   /* IRQ0: Timer */
ISR_NOERR 33   /* IRQ1: Keyboard */
ISR_NOERR 34   /* IRQ2: Cascade */
ISR_NOERR 35   /* IRQ3: COM2 */
ISR_NOERR 36   /* IRQ4: COM1 */
ISR_NOERR 37   /* IRQ5: LPT2 */
ISR_NOERR 38   /* IRQ6: Floppy */
ISR_NOERR 39   /* IRQ7: LPT1 / Spurious */
ISR_NOERR 40   /* IRQ8: RTC */
ISR_NOERR 41   /* IRQ9: Free */
ISR_NOERR 42   /* IRQ10: Free */
ISR_NOERR 43   /* IRQ11: Free */
ISR_NOERR 44   /* IRQ12: PS/2 Mouse */
ISR_NOERR 45   /* IRQ13: FPU */
ISR_NOERR 46   /* IRQ14: Primary ATA */
ISR_NOERR 47   /* IRQ15: Secondary ATA */
ISR_SYSCALL 128  /* SYSCALL */

/* Mark stack as non-executable */
.section .note.GNU-stack, "", @progbits
