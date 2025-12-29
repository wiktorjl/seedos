#include "idt.h"
#include "kprintf.h"
#include "log.h"

static idt_entry_t idt[IDT_SIZE];
static idt_ptr_t idtr;

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
    "Reserved", 
    "Reserved", 
    "Reserved", 
    "Reserved",
    "Reserved", 
    "Reserved", 
    "Reserved", 
    "Reserved",
    "Security Exception",
    "Reserved"
};

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

static void (*isr_stubs[IDT_SIZE])(void) = {
    isr_0, isr_1, isr_2, isr_3, isr_4, isr_5, isr_6, isr_7, isr_8, isr_9,  
    isr_10, isr_11, isr_12, isr_13, isr_14, isr_15, isr_16, isr_17, isr_18, 
    isr_19, isr_20, isr_21, isr_22, isr_23, isr_24, isr_25, isr_26, isr_27, 
    isr_28, isr_29, isr_30, isr_31,
};

void idt_set_gate(int n, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist) {    
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = selector;
    idt[n].ist = ist & 0x07;
    idt[n].type_attr = type_attr;
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}

void idt_install(void) {
    for(int i = 0; i < IDT_SIZE; i++) {
        idt_set_gate(i, (uint64_t)isr_stubs[i], GDT_SELECTOR_FROM_LIMINE, IDT_GATE_INTERRUPT, 0);
    }

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    asm volatile ("lidt %0" : : "m"(idtr));
}

void backtrace(uint64_t rip, uint64_t rbp) {
    log_debug("Backtrace:");

    // Frame 0: the faulting instruction
    log_debug("  [0] 0x%016llx", rip);

    for (int i = 1; i < 10 && rbp != 0; i++) {
        uint64_t *frame = (uint64_t *)rbp;
        uint64_t ret_addr = frame[1];   // return address
        uint64_t prev_rbp = frame[0];   // previous frame pointer

        log_debug("  [%d] 0x%016llx", i, ret_addr - 1); // -1 to get inside the calling instruction

        rbp = prev_rbp;
    }
}

void interrupt_handler(interrupt_frame_t *frame) {

    log_panic("EXCEPTION: %s (int %d, error=0x%x)",
            exception_names[frame->int_no], frame->int_no, frame->error_code);
    log_panic("RIP: 0x%016llx  RSP: 0x%016llx", frame->rip, frame->rsp);
    log_panic("RAX: 0x%016llx  RBX: 0x%016llx", frame->rax, frame->rbx);
    log_panic("RCX: 0x%016llx  RDX: 0x%016llx", frame->rcx, frame->rdx);

    backtrace(frame->rip, frame->rbp);

    if(frame->int_no == 1) {
        log_panic("Debug Exception - Halting for debugging");
    } else {
       // Halt - don't return from fatal exceptions
       log_panic("System halted.\n");
        while (1) {
            asm volatile ("hlt");
        }
    }
}