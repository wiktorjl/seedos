/*
 * idt.h - Interrupt Descriptor Table (IDT)
 *
 * The IDT tells the CPU where to jump when an interrupt occurs.
 * It's an array of 256 "gate descriptors" - each one points to a handler
 * function for a specific interrupt number.
 *
 * Interrupt types:
 *   - Exceptions (0-31): CPU-generated (divide by zero, page fault, etc.)
 *   - IRQs (32-47): Hardware interrupts (timer, keyboard, etc.)
 *   - Software (48-255): Available for syscalls, etc.
 *
 * When interrupt N occurs:
 *   1. CPU looks up IDT[N]
 *   2. Pushes flags, CS, RIP (and error code for some exceptions)
 *   3. Jumps to the handler address in IDT[N]
 *   4. Handler runs with interrupts disabled (for interrupt gates)
 */

#ifndef IDT_H
#define IDT_H

#include "types.h"

/* =============================================================================
 * IDT Data Structures
 * =============================================================================
 */

/*
 * IDT Entry (Gate Descriptor) - 16 bytes in 64-bit mode
 *
 * Each entry describes one interrupt handler:
 *   - Where to jump (64-bit handler address split across 3 fields)
 *   - What code segment to use (always kernel code segment)
 *   - Privilege level required to trigger (DPL in type_attr)
 *   - Whether to use IST for alternate stack
 */
struct idt_entry {
    uint16_t offset_low;      /* Handler address bits 0-15 */
    uint16_t selector;        /* Code segment selector (GDT_KERNEL_CODE) */
    uint8_t  ist;             /* Interrupt Stack Table index (0 = don't use IST) */
    uint8_t  type_attr;       /* Gate type and attributes (present, DPL, type) */
    uint16_t offset_mid;      /* Handler address bits 16-31 */
    uint32_t offset_high;     /* Handler address bits 32-63 */
    uint32_t reserved;        /* Reserved, must be zero */
} __attribute__((packed));

/*
 * IDTR - IDT Register structure for the LIDT instruction
 *
 * This tells the CPU where the IDT is located and how big it is.
 */
struct idtr {
    uint16_t limit;           /* Size of IDT in bytes minus 1 */
    uint64_t base;            /* Virtual address of IDT array */
} __attribute__((packed));

/*
 * Interrupt Frame - CPU and stub saved state
 *
 * When an interrupt occurs, the CPU pushes SS, RSP, RFLAGS, CS, RIP
 * (and error code for some exceptions). Our assembly stub then pushes
 * general-purpose registers so the C handler can access them.
 *
 * This struct matches the stack layout so we can access it from C.
 */
struct interrupt_frame {
    /* Pushed by our assembly stub (isr.S) - in reverse order of push */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;          /* Interrupt number (pushed by stub) */
    uint64_t error_code;      /* Error code (pushed by CPU or dummy by stub) */

    /* Pushed automatically by CPU on interrupt */
    uint64_t rip;             /* Instruction pointer at time of interrupt */
    uint64_t cs;              /* Code segment at time of interrupt */
    uint64_t rflags;          /* CPU flags at time of interrupt */
    uint64_t rsp;             /* Stack pointer (only if privilege change) */
    uint64_t ss;              /* Stack segment (only if privilege change) */
};

/* =============================================================================
 * Gate Type Constants
 *
 * The type_attr byte format:
 *   Bit 7: Present (P) - must be 1 for valid entries
 *   Bits 6-5: DPL - minimum privilege level to trigger (0=kernel, 3=user)
 *   Bit 4: Always 0 for interrupt/trap gates
 *   Bits 3-0: Gate type (0xE=interrupt gate, 0xF=trap gate)
 *
 * Interrupt gates disable interrupts (clear IF), trap gates don't.
 * =============================================================================
 */
#define IDT_GATE_INTERRUPT 0x8E  /* P=1, DPL=0, Type=0xE (interrupt gate) */
#define IDT_GATE_TRAP      0x8F  /* P=1, DPL=0, Type=0xF (trap gate) */
#define IDT_GATE_USER      0xEE  /* P=1, DPL=3, Type=0xE (user-callable interrupt) */

/* =============================================================================
 * Exception Numbers (0-31)
 *
 * These are defined by Intel/AMD and cannot be changed.
 * Some push an error code, some don't (our stubs normalize this).
 * =============================================================================
 */
#define EXCEPTION_DIVIDE_ERROR        0   /* DIV/IDIV by zero */
#define EXCEPTION_DEBUG               1   /* Debug trap */
#define EXCEPTION_NMI                 2   /* Non-maskable interrupt */
#define EXCEPTION_BREAKPOINT          3   /* INT3 instruction */
#define EXCEPTION_OVERFLOW            4   /* INTO instruction */
#define EXCEPTION_BOUND_EXCEEDED      5   /* BOUND instruction */
#define EXCEPTION_INVALID_OPCODE      6   /* Invalid instruction */
#define EXCEPTION_DEVICE_NOT_AVAIL    7   /* FPU not available */
#define EXCEPTION_DOUBLE_FAULT        8   /* Exception during exception (has error code) */
#define EXCEPTION_INVALID_TSS         10  /* Invalid TSS (has error code) */
#define EXCEPTION_SEGMENT_NOT_PRESENT 11  /* Segment not present (has error code) */
#define EXCEPTION_STACK_FAULT         12  /* Stack segment fault (has error code) */
#define EXCEPTION_GENERAL_PROTECTION  13  /* General protection fault (has error code) */
#define EXCEPTION_PAGE_FAULT          14  /* Page fault (has error code, CR2=address) */
#define EXCEPTION_X87_FP_EXCEPTION    16  /* x87 FPU exception */
#define EXCEPTION_ALIGNMENT_CHECK     17  /* Alignment check (has error code) */
#define EXCEPTION_MACHINE_CHECK       18  /* Machine check */
#define EXCEPTION_SIMD_FP_EXCEPTION   19  /* SIMD floating point exception */

/* =============================================================================
 * Hardware IRQ Numbers
 *
 * After remapping the PIC, IRQs 0-15 map to interrupts 32-47.
 * =============================================================================
 */
#define IRQ_BASE       32                  /* IRQ 0 maps to interrupt 32 */
#define IRQ_TIMER      (IRQ_BASE + 0)      /* IRQ 0: PIT timer */
#define IRQ_KEYBOARD   (IRQ_BASE + 1)      /* IRQ 1: PS/2 keyboard */

/* =============================================================================
 * IDT API
 * =============================================================================
 */

/*
 * idt_init - Initialize the Interrupt Descriptor Table.
 *
 * Sets up handlers for:
 *   - Exceptions 0-31 (CPU exceptions)
 *   - IRQs 32-47 (hardware interrupts)
 *   - Syscall 128 (int 0x80 for userspace)
 *
 * Then loads the IDT using the LIDT instruction.
 */
void idt_init(void);

#endif /* IDT_H */
