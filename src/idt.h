#ifndef IDT_H
#define IDT_H

#include "types.h"

#define IDT_SIZE 256  /* Full IDT: 0-31 exceptions, 32-255 IRQs */

#define IDT_GATE_INTERRUPT 0x8E  /* P=1, DPL=0, Type=0xE (interrupt gate) */
#define IDT_GATE_TRAP      0x8F  /* P=1, DPL=0, Type=0xF (trap gate) */
#define IDT_GATE_USER      0xEE  /* P=1, DPL=3, Type=0xE (user-callable interrupt) */

#define GDT_SELECTOR_FROM_LIMINE 0x28  /* Code segment selector provided by Limine */

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

void idt_set_gate(int n, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist);
void idt_install(void);

/*
 * IRQ handler callback type.
 * Called when an IRQ fires. The handler should perform any necessary
 * processing and then return. EOI is handled by the caller.
 */
typedef void (*irq_handler_t)(interrupt_frame_t *frame);

/*
 * idt_register_irq - Register a handler for an IRQ.
 *
 * @irq: The IRQ number (vector number, 32-255)
 * @handler: Function to call when the IRQ fires
 */
void idt_register_irq(int irq, irq_handler_t handler);

#endif /* IDT_H */