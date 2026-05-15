// SPDX-License-Identifier: GPL-2.0-only
/*
 * Interrupt Descriptor Table setup and dispatch
 */

#include "idt.h"
#include "gdt.h"
#include "kprintf.h"
#include "log.h"
#include "apic.h"
#include "vmm.h"
#include "pmm.h"
#include "page.h"
#include "process.h"
#include "memory.h"

/* Local memcpy for COW page copying */
static void idt_memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;
	for (size_t i = 0; i < n; i++)
		d[i] = s[i];
}

static idt_entry_t idt[IDT_SIZE];
static idt_ptr_t idtr;
static irq_handler_t irq_handlers[256];

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
extern void isr_255(void);

static void (*isr_stubs[IDT_SIZE])(void) = {
    isr_0, isr_1, isr_2, isr_3, isr_4, isr_5, isr_6, isr_7, isr_8, isr_9,
    isr_10, isr_11, isr_12, isr_13, isr_14, isr_15, isr_16, isr_17, isr_18,
    isr_19, isr_20, isr_21, isr_22, isr_23, isr_24, isr_25, isr_26, isr_27,
    isr_28, isr_29, isr_30, isr_31,
    isr_32, isr_33, isr_34, isr_35, isr_36, isr_37, isr_38, isr_39,
    isr_40, isr_41, isr_42, isr_43, isr_44, isr_45, isr_46, isr_47,
    [48 ... 254] = 0,
    [255] = isr_255,
};

/**
 * idt_set_gate - Configure a single IDT entry
 * @n: vector number (0-255)
 * @handler: ISR stub address
 * @selector: code segment selector
 * @type_attr: gate type and attributes
 * @ist: IST index (0 = default stack)
 */
void idt_set_gate(int n, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist)
{
	idt[n].offset_low = handler & 0xFFFF;
	idt[n].selector = selector;
	idt[n].ist = ist & 0x07;
	idt[n].type_attr = type_attr;
	idt[n].offset_mid = (handler >> 16) & 0xFFFF;
	idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
	idt[n].zero = 0;
}

/**
 * idt_install - Initialize and load the IDT
 *
 * Installs ISR stubs for all defined vectors and loads the IDT.
 * Must be called before enabling interrupts.
 */
void idt_install(void)
{
	for (int i = 0; i < 256; i++)
		irq_handlers[i] = 0;

	for (int i = 0; i < IDT_SIZE; i++) {
		if (isr_stubs[i] != 0)
			idt_set_gate(i, (uint64_t)isr_stubs[i], GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 0);
	}

	/*
	 * Route critical exceptions through dedicated IST stacks so they
	 * remain serviceable when the regular kernel stack is corrupt or
	 * exhausted. IST indices match the slots populated in gdt.c.
	 */
	idt_set_gate(2,  (uint64_t)isr_2,  GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 1); /* NMI  */
	idt_set_gate(8,  (uint64_t)isr_8,  GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 2); /* #DF  */
	idt_set_gate(18, (uint64_t)isr_18, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 3); /* #MCE */

	idtr.limit = sizeof(idt) - 1;
	idtr.base  = (uint64_t)&idt;

	asm volatile ("lidt %0" : : "m"(idtr));
}

/**
 * idt_register_irq - Register a handler for an IRQ vector
 * @irq: vector number (0-255)
 * @handler: function to call when vector fires
 */
void idt_register_irq(int irq, irq_handler_t handler)
{
	if (irq >= 0 && irq < 256)
		irq_handlers[irq] = handler;
}

/* Validate kernel address (higher half, aligned) */
static inline int is_valid_kernel_addr(uint64_t addr)
{
	return (addr >= 0xFFFF800000000000ULL) && ((addr & 0x7) == 0);
}

/* Forward declaration for backtrace */
void backtrace(uint64_t rip, uint64_t rbp);

/*
 * Page fault error code bits
 */
#define PF_PRESENT      (1 << 0)  /* Page was present */
#define PF_WRITE        (1 << 1)  /* Write access */
#define PF_USER         (1 << 2)  /* User mode access */
#define PF_RESERVED     (1 << 3)  /* Reserved bit set in PTE */
#define PF_INSTRUCTION  (1 << 4)  /* Instruction fetch */

/**
 * handle_cow_fault - Handle Copy-on-Write page fault
 * @fault_addr: address that caused the fault
 * @proc: current process
 *
 * Called when a write fault occurs on a page marked with COW bit.
 * Either makes the page writable (if refcount==1) or copies it.
 *
 * Return: 0 on success, -1 if not a COW fault
 */
static int handle_cow_fault(uint64_t fault_addr, process_t *proc)
{
	uint64_t page_addr = fault_addr & ~0xFFFULL;
	uint64_t pte_flags = vmm_get_pte_flags(proc->pml4_phys, page_addr);
	uint64_t phys_addr = vmm_get_physical(proc->pml4_phys, page_addr);
	uint64_t new_phys;
	uint8_t refcount;

	/* Check if this is a COW page */
	if (!(pte_flags & PTE_COW)) {
		return -1;  /* Not a COW fault */
	}

	log_debug("COW: Fault at 0x%llx, phys=0x%llx", fault_addr, phys_addr);

	refcount = page_get_refcount(phys_addr);

	if (refcount == 1) {
		/*
		 * We're the only owner - just make it writable
		 * Remove COW bit, add writable bit
		 */
		pte_flags &= ~PTE_COW;
		pte_flags |= PTE_WRITABLE;
		vmm_set_pte_flags(proc->pml4_phys, page_addr, pte_flags);
		log_debug("COW: Page 0x%llx now writable (sole owner)", phys_addr);
		return 0;
	}

	/*
	 * Multiple owners - need to copy the page
	 */
	new_phys = pmm_alloc();
	if (new_phys == PMM_ALLOC_FAILED) {
		log_error("COW: Out of memory copying page");
		return -1;
	}

	/* Copy page contents */
	idt_memcpy(phys_to_virt(new_phys), phys_to_virt(phys_addr), PAGE_SIZE);

	/* Update PTE to point to new page with write permission */
	pte_flags &= ~PTE_COW;
	pte_flags |= PTE_WRITABLE;
	vmm_unmap_page(proc->pml4_phys, page_addr);
	vmm_map_page(proc->pml4_phys, page_addr, new_phys, pte_flags);

	/* Set refcount on new page to 1 */
	page_set_refcount(new_phys, 1);

	/* Decrement refcount on old page (may free it) */
	page_unref(phys_addr);

	log_debug("COW: Copied page 0x%llx -> 0x%llx (old refcount=%d)",
	          phys_addr, new_phys, refcount);
	return 0;
}

/**
 * handle_page_fault - Handle page fault exception
 * @frame: interrupt frame with register state
 *
 * Handles COW faults. Other page faults are fatal for now.
 */
static void handle_page_fault(interrupt_frame_t *frame)
{
	uint64_t fault_addr;
	uint64_t error = frame->error_code;
	process_t *proc = process_current();

	/* Get faulting address from CR2 */
	__asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

	/*
	 * Check if this is a write fault we can handle
	 * Conditions for COW handling:
	 * - Write fault (PF_WRITE set)
	 * - Page was present (PF_PRESENT set)
	 * - User mode fault (PF_USER set)
	 * - We have a current process
	 */
	if ((error & (PF_WRITE | PF_PRESENT | PF_USER)) == (PF_WRITE | PF_PRESENT | PF_USER)
	    && proc != NULL) {
		if (handle_cow_fault(fault_addr, proc) == 0) {
			/* COW handled successfully */
			return;
		}
	}

	/*
	 * Not a COW fault - this is a real page fault
	 * For now, just report and halt
	 * TODO: Send SIGSEGV to user process
	 */
	log_panic("PAGE FAULT at 0x%016llx", fault_addr);
	log_panic("Error code: 0x%x (%s%s%s%s)",
	          error,
	          (error & PF_PRESENT) ? "present " : "not-present ",
	          (error & PF_WRITE) ? "write " : "read ",
	          (error & PF_USER) ? "user " : "kernel ",
	          (error & PF_INSTRUCTION) ? "ifetch" : "");
	log_panic("RIP: 0x%016llx  RSP: 0x%016llx", frame->rip, frame->rsp);
	log_panic("RAX: 0x%016llx  RBX: 0x%016llx", frame->rax, frame->rbx);

	backtrace(frame->rip, frame->rbp);

	log_panic("System halted.\n");
	while (1)
		__asm__ volatile("hlt");
}

/**
 * backtrace - Print stack trace via frame pointers
 * @rip: faulting instruction pointer
 * @rbp: frame pointer for stack walk
 */
void backtrace(uint64_t rip, uint64_t rbp)
{
	log_debug("Backtrace:");
	log_debug("  [0] 0x%016llx", rip);

	for (int i = 1; i < 10 && rbp != 0; i++) {
		if (!is_valid_kernel_addr(rbp)) {
			log_debug("  [%d] <invalid frame pointer: 0x%016llx>", i, rbp);
			break;
		}

		uint64_t *frame = (uint64_t *)rbp;
		uint64_t ret_addr = frame[1];
		uint64_t prev_rbp = frame[0];

		log_debug("  [%d] 0x%016llx", i, ret_addr - 1);
		rbp = prev_rbp;
	}
}

/**
 * interrupt_handler - Common interrupt dispatcher
 * @frame: saved register state from ISR stub
 *
 * Routes exceptions (0-31) to panic, dispatches IRQs (32-254) to
 * registered handlers, ignores spurious (255).
 */
void interrupt_handler(interrupt_frame_t *frame)
{
	uint64_t int_no = frame->int_no;

	if (int_no < 32) {
		/* Page fault (vector 14) gets special handling for COW */
		if (int_no == 14) {
			handle_page_fault(frame);
			return;
		}

		/* Other exceptions - panic */
		log_panic("EXCEPTION: %s (int %d, error=0x%x)",
			exception_names[int_no], int_no, frame->error_code);
		log_panic("RIP: 0x%016llx  RSP: 0x%016llx", frame->rip, frame->rsp);
		log_panic("RAX: 0x%016llx  RBX: 0x%016llx", frame->rax, frame->rbx);
		log_panic("RCX: 0x%016llx  RDX: 0x%016llx", frame->rcx, frame->rdx);

		backtrace(frame->rip, frame->rbp);

		log_panic("System halted.\n");
		while (1)
			asm volatile ("hlt");
	}

	if (int_no == 255)
		return;

	if (irq_handlers[int_no] != 0)
		irq_handlers[int_no](frame);
	else
		apic_eoi();
}
