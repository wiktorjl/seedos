/*
 * idt.h - Interrupt Descriptor Table
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/*
 * IDT entry (Gate Descriptor) - 16 bytes on x86_64
 */
struct idt_entry {
    uint16_t offset_low;      /* Handler address bits 0-15 */
    uint16_t selector;        /* Kernel code segment selector */
    uint8_t  ist;             /* Interrupt Stack Table offset (0 = don't use) */
    uint8_t  type_attr;       /* Type and attributes */
    uint16_t offset_mid;      /* Handler address bits 16-31 */
    uint32_t offset_high;     /* Handler address bits 32-63 */
    uint32_t reserved;        /* Must be zero */
} __attribute__((packed));

/*
 * IDTR - pointer structure for lidt instruction
 */
struct idtr {
    uint16_t limit;           /* Size of IDT - 1 */
    uint64_t base;            /* Address of IDT */
} __attribute__((packed));

/*
 * Interrupt frame - what the CPU pushes before calling handler
 * (plus what our stub pushes)
 */
struct interrupt_frame {
    /* Pushed by our stub */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;          /* Interrupt number */
    uint64_t error_code;      /* Error code (or dummy) */
    
    /* Pushed by CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* Gate type constants */
#define IDT_GATE_INTERRUPT 0x8E  /* Present, DPL=0, Interrupt Gate */
#define IDT_GATE_TRAP      0x8F  /* Present, DPL=0, Trap Gate */
#define IDT_GATE_USER      0xEE  /* Present, DPL=3, Interrupt Gate (callable from userspace) */

/* Exception numbers */
#define INT_DIVIDE_ERROR        0
#define INT_DEBUG               1
#define INT_NMI                 2
#define INT_BREAKPOINT          3
#define INT_OVERFLOW            4
#define INT_BOUND_EXCEEDED      5
#define INT_INVALID_OPCODE      6
#define INT_DEVICE_NOT_AVAIL    7
#define INT_DOUBLE_FAULT        8
#define INT_INVALID_TSS         10
#define INT_SEGMENT_NOT_PRESENT 11
#define INT_STACK_FAULT         12
#define INT_GENERAL_PROTECTION  13
#define INT_PAGE_FAULT          14
#define INT_X87_FP_EXCEPTION    16
#define INT_ALIGNMENT_CHECK     17
#define INT_MACHINE_CHECK       18
#define INT_SIMD_FP_EXCEPTION   19

/* Hardware IRQs (after remapping PIC to 32-47) */
#define IRQ_BASE       32
#define IRQ_TIMER      (IRQ_BASE + 0)
#define IRQ_KEYBOARD   (IRQ_BASE + 1)

/*
 * Initialize the IDT and load it
 */
void idt_init(void);

#endif /* IDT_H */
