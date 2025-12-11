/*
 * idt.c - Interrupt Descriptor Table setup and handling
 */

#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "gdt.h"
#include "console.h"
#include <stddef.h>
#include "syscall.h"


/* The IDT - 256 entries */
static struct idt_entry idt[256];

/* The IDTR value to load */
static struct idtr idtr;

/* External references to assembly stubs */
extern void isr_0(void);
extern void isr_1(void);
extern void isr_2(void);
extern void isr_3(void);
extern void isr_4(void);
extern void isr_5(void);
extern void isr_6(void);
extern void isr_7(void);
extern void isr_8(void);
extern void isr_9(void);
extern void isr_10(void);
extern void isr_11(void);
extern void isr_12(void);
extern void isr_13(void);
extern void isr_14(void);
extern void isr_15(void);
extern void isr_16(void);
extern void isr_17(void);
extern void isr_18(void);
extern void isr_19(void);
extern void isr_20(void);
extern void isr_21(void);
extern void isr_22(void);
extern void isr_23(void);
extern void isr_24(void);
extern void isr_25(void);
extern void isr_26(void);
extern void isr_27(void);
extern void isr_28(void);
extern void isr_29(void);
extern void isr_30(void);
extern void isr_31(void);
extern void isr_32(void);
extern void isr_33(void);
extern void isr_34(void);
extern void isr_35(void);
extern void isr_36(void);
extern void isr_37(void);
extern void isr_38(void);
extern void isr_39(void);
extern void isr_40(void);
extern void isr_41(void);
extern void isr_42(void);
extern void isr_43(void);
extern void isr_44(void);
extern void isr_45(void);
extern void isr_46(void);
extern void isr_47(void);
extern void isr_128(void);

/* Array of handler addresses for convenience */
static void (*isr_stubs[48])(void) = {
    isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
    isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
    isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
    isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    isr_32, isr_33, isr_34, isr_35, isr_36, isr_37, isr_38, isr_39,
    isr_40, isr_41, isr_42, isr_43, isr_44, isr_45, isr_46, isr_47,
};

/* Exception names for pretty printing */
static const char *exception_names[] = {
    "Divide Error",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FP Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD FP Exception",
    "Virtualization Exception",
    "Control Protection Exception",
};

/*
 * Set an IDT entry
 */
static void idt_set_entry(int n, void (*handler)(void), uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    
    idt[n].offset_low  = addr & 0xFFFF;
    idt[n].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[n].selector    = GDT_KERNEL_CODE;
    idt[n].ist         = 0;     /* Don't use IST */
    idt[n].type_attr   = type_attr;
    idt[n].reserved    = 0;
}

/*
 * Initialize and load the IDT
 */
void idt_init(void) {
    /* Set up exception handlers (0-31) and IRQ handlers (32-47) */
    for (int i = 0; i < 48; i++) {
        idt_set_entry(i, isr_stubs[i], IDT_GATE_INTERRUPT);
    }
    idt_set_entry(128, isr_128, IDT_GATE_USER);  /* Syscall gate */

    /* Set up IDTR */
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    /* Load IDT */
    asm volatile ("lidt %0" : : "m"(idtr));
}

/*
 * Common interrupt handler - called from assembly stub
 */
void interrupt_handler(struct interrupt_frame *frame) {
    
    if (frame->int_no == 3) {
        /* Breakpoint - just continue */
        return;
    }
    
    if (frame->int_no < 32) {
        /* Exception */
        puts("\n");
        puts("========================================\n");
        puts("  KERNEL PANIC: ");
        if (frame->int_no < 22) {
            puts(exception_names[frame->int_no]);
        } else {
            puts("Unknown Exception");
        }
        puts("\n");
        puts("========================================\n\n");

        puts("  Interrupt:  ");
        put_dec(frame->int_no);
        puts("\n");

        puts("  Error code: ");
        put_hex(frame->error_code);
        puts("\n\n");

        puts("  RIP: ");
        put_hex(frame->rip);
        puts("\n");

        puts("  RSP: ");
        put_hex(frame->rsp);
        puts("\n");

        puts("  RBP: ");
        put_hex(frame->rbp);
        puts("\n");

        puts("  RFLAGS: ");
        put_hex(frame->rflags);
        puts("\n\n");

        puts("  RAX: ");
        put_hex(frame->rax);
        puts("  RBX: ");
        put_hex(frame->rbx);
        puts("\n");

        puts("  RCX: ");
        put_hex(frame->rcx);
        puts("  RDX: ");
        put_hex(frame->rdx);
        puts("\n");

        puts("  RSI: ");
        put_hex(frame->rsi);
        puts("  RDI: ");
        put_hex(frame->rdi);
        puts("\n");

        /* For page faults, CR2 contains the faulting address */
        if (frame->int_no == 14) {
            uint64_t cr2;
            asm volatile ("movq %%cr2, %0" : "=r"(cr2));
            puts("\n  Fault address (CR2): ");
            put_hex(cr2);
            puts("\n");
        }

        /* Halt after exception */
        puts("\n========================================\n");
        puts("  System halted. Please reboot.\n");
        puts("========================================\n");
        while (1) {
            asm volatile ("cli; hlt");
        }
    } else if (frame->int_no >= 32 && frame->int_no < 48) {
        /* Hardware IRQ */
        uint8_t irq = frame->int_no - 32;
        
        if (irq == 1) {
            /* Keyboard interrupt */
            keyboard_handler();
        }
        
        /* Send End-of-Interrupt to PIC */
        pic_send_eoi(irq);
    } else {
        /* Other interrupt */
        puts("Interrupt ");
        put_dec(frame->int_no);
        puts("\n");
    }
}
