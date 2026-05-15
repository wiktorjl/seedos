/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * x86-64 Interrupt Descriptor Table
 */

#ifndef _IDT_H
#define _IDT_H

#include "types.h"

#define IDT_SIZE 256

#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP      0x8F
#define IDT_GATE_USER      0xEE

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) interrupt_frame_t;

/**
 * idt_set_gate - Configure a single IDT entry
 * @n: vector number (0-255)
 * @handler: ISR stub address
 * @selector: code segment selector
 * @type_attr: gate type and DPL
 * @ist: IST index (0-7)
 */
void idt_set_gate(int n, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist);

/**
 * idt_install - Initialize and load the IDT
 */
void idt_install(void);

typedef void (*irq_handler_t)(interrupt_frame_t *frame);

/**
 * idt_register_irq - Register a handler for an IRQ
 * @irq: vector number (32-255)
 * @handler: function to call when IRQ fires
 */
void idt_register_irq(int irq, irq_handler_t handler);

/*
 * NMI nesting diagnostics, maintained by the dedicated isr_2 stub.
 *
 *   nmi_depth      - 1 while an NMI is being handled, 0 otherwise.
 *   nmi_lost_count - number of nested-NMI events observed (the inner
 *                    NMI's CPU-pushed frame overwrites the outer's at
 *                    IST1 top; the outer's iret is then unrecoverable).
 */
extern uint64_t nmi_depth;
extern uint64_t nmi_lost_count;

#endif /* _IDT_H */
