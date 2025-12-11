/*
 * idt.c - Interrupt Descriptor Table setup and handling
 *
 * This file sets up the IDT and provides the main interrupt handler.
 *
 * Interrupt handling flow:
 *   1. Interrupt occurs (exception, IRQ, or software INT)
 *   2. CPU looks up handler in IDT and jumps to it
 *   3. Our assembly stub (isr.S) saves registers and calls interrupt_handler()
 *   4. interrupt_handler() determines the type and handles it
 *   5. Assembly stub restores registers and returns via IRETQ
 *
 * For hardware IRQs, we must send EOI (End of Interrupt) to the PIC,
 * otherwise no more IRQs of that type will be delivered.
 */

#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "gdt.h"
#include "console.h"
#include <stddef.h>
#include "syscall.h"

/* Number of IDT entries (256 possible interrupt vectors) */
#define IDT_ENTRIES 256

/* Syscall interrupt number (Linux-compatible) */
#define SYSCALL_VECTOR 128

/* =============================================================================
 * IDT Global State
 * =============================================================================
 */

/* The IDT array - 256 entries, one for each possible interrupt vector */
static struct idt_entry idt[IDT_ENTRIES];

/* The IDTR value - pointer and limit loaded into CPU via LIDT */
static struct idtr idtr;

/* =============================================================================
 * Assembly Stub References
 *
 * Each interrupt vector has an assembly stub in isr.S that:
 *   1. Pushes a dummy error code (if CPU didn't push one)
 *   2. Pushes the interrupt number
 *   3. Saves all general-purpose registers
 *   4. Calls interrupt_handler()
 *   5. Restores registers and does IRETQ
 * =============================================================================
 */
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
extern void isr_128(void);  /* Syscall handler */

/* Number of exception/IRQ handlers (0-31 exceptions + 32-47 IRQs) */
#define NUM_ISR_STUBS 48

/* Array of handler addresses for easy iteration during setup */
static void (*isr_stubs[NUM_ISR_STUBS])(void) = {
    isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
    isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
    isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
    isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    isr_32, isr_33, isr_34, isr_35, isr_36, isr_37, isr_38, isr_39,
    isr_40, isr_41, isr_42, isr_43, isr_44, isr_45, isr_46, isr_47,
};

/* Human-readable names for CPU exceptions (for panic messages) */
static const char *exception_names[] = {
    "Divide Error",                /* 0 */
    "Debug",                       /* 1 */
    "NMI",                         /* 2 */
    "Breakpoint",                  /* 3 */
    "Overflow",                    /* 4 */
    "Bound Range Exceeded",        /* 5 */
    "Invalid Opcode",              /* 6 */
    "Device Not Available",        /* 7 */
    "Double Fault",                /* 8 */
    "Coprocessor Segment Overrun", /* 9 */
    "Invalid TSS",                 /* 10 */
    "Segment Not Present",         /* 11 */
    "Stack-Segment Fault",         /* 12 */
    "General Protection Fault",    /* 13 */
    "Page Fault",                  /* 14 */
    "Reserved",                    /* 15 */
    "x87 FP Exception",            /* 16 */
    "Alignment Check",             /* 17 */
    "Machine Check",               /* 18 */
    "SIMD FP Exception",           /* 19 */
    "Virtualization Exception",    /* 20 */
    "Control Protection Exception",/* 21 */
};
#define NUM_EXCEPTION_NAMES (sizeof(exception_names) / sizeof(exception_names[0]))

/* =============================================================================
 * IDT Setup Functions
 * =============================================================================
 */

/*
 * idt_set_entry - Configure a single IDT entry.
 *
 * @n:         Interrupt vector number (0-255)
 * @handler:   Address of the assembly stub to call
 * @type_attr: Gate type and attributes (see IDT_GATE_* constants)
 *
 * The 64-bit handler address is split across three fields because
 * the IDT entry format was designed for backward compatibility with
 * 32-bit protected mode.
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
 * idt_init - Initialize and load the IDT.
 *
 * Sets up all interrupt handlers and loads the IDT into the CPU.
 * After this, the CPU will use our handlers for all interrupts.
 */
void idt_init(void) {
    /* Set up exception handlers (0-31) and IRQ handlers (32-47) */
    for (int i = 0; i < NUM_ISR_STUBS; i++) {
        idt_set_entry(i, isr_stubs[i], IDT_GATE_INTERRUPT);
    }

    /* Set up syscall handler at INT 0x80 (Linux-compatible) */
    /* DPL=3 so userspace can trigger it with INT 0x80 instruction */
    idt_set_entry(SYSCALL_VECTOR, isr_128, IDT_GATE_USER);

    /* Set up IDTR with address and size of our IDT */
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    /* Load IDT into CPU via LIDT instruction */
    asm volatile ("lidt %0" : : "m"(idtr));
}

/* =============================================================================
 * Main Interrupt Handler
 *
 * This is the C entry point for all interrupts. The assembly stub saves
 * registers and calls this function with a pointer to the interrupt frame.
 * =============================================================================
 */

/*
 * interrupt_handler - Handle all interrupts (exceptions, IRQs, syscalls).
 *
 * @frame: Pointer to saved CPU state on the stack
 *
 * This function dispatches based on interrupt number:
 *   0-31:  CPU exceptions (fault, trap, abort)
 *   32-47: Hardware IRQs
 *   128:   Syscall (handled in isr.S, not here)
 */
void interrupt_handler(struct interrupt_frame *frame) {

    /* Breakpoint exception (INT3) - just continue for debugging */
    if (frame->int_no == EXCEPTION_BREAKPOINT) {
        return;
    }

    /* CPU Exception (vectors 0-31) */
    if (frame->int_no < 32) {
        /* Fatal exception - print diagnostic info and halt */
        puts("\n");
        puts("========================================\n");
        puts("  KERNEL PANIC: ");
        if (frame->int_no < NUM_EXCEPTION_NAMES) {
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

        /*
         * For page faults, CR2 contains the virtual address that caused
         * the fault. This is essential for implementing demand paging.
         */
        if (frame->int_no == EXCEPTION_PAGE_FAULT) {
            uint64_t faulting_address;
            asm volatile ("movq %%cr2, %0" : "=r"(faulting_address));
            puts("\n  Fault address (CR2): ");
            put_hex(faulting_address);
            puts("\n");
        }

        /* Halt the system - unrecoverable error */
        puts("\n========================================\n");
        puts("  System halted. Please reboot.\n");
        puts("========================================\n");
        while (1) {
            asm volatile ("cli; hlt");  /* Disable interrupts and halt */
        }
    }

    /* Hardware IRQ (vectors 32-47) */
    if (frame->int_no >= IRQ_BASE && frame->int_no < IRQ_BASE + 16) {
        uint8_t irq_number = frame->int_no - IRQ_BASE;

        /* Dispatch to specific IRQ handler */
        if (irq_number == 1) {
            keyboard_handler();  /* IRQ1 = PS/2 keyboard */
        }

        /* Acknowledge the interrupt to the PIC */
        /* This MUST be done, or no more IRQs of this type will arrive */
        pic_send_eoi(irq_number);
        return;
    }

    /* Unknown/unhandled interrupt */
    puts("Unhandled interrupt: ");
    put_dec(frame->int_no);
    puts("\n");
}
