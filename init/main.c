// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel entry point
 *
 * Main kernel initialization and boot sequence.
 */

#include "limine.h"
#include "console.h"
#include "cpu.h"
#include "log.h"
#include "logo.h"
#include "terminal.h"
#include "kprintf.h"
#include "gdt.h"
#include "percpu.h"
#include "fpu.h"
#include "syscall.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "acpi.h"
#include "apic.h"
#include "ioapic.h"
#include "keyboard.h"
#include "sysinfo.h"
#include "kthread.h"
#include "process.h"
#include "kshell.h"
#include "vfs.h"
#include "tty_dev.h"
#include "ext2.h"
#include <stdint.h>
#include <threads.h>

void dummy_function(void *arg)
{
	log_info("Hello from a kernel thread! Argument: %llu", (uint64_t)arg);
}

void printA(void *arg)
{
	for (volatile int i = 0; i < 1000000; i++) {
		kthread_sleep(500);
		log_info("A");
	}
	log_info("A - DONE");
}

/**
 * kmain - Kernel entry point after bootloader handoff
 *
 * Initializes all kernel subsystems in dependency order:
 *   1. Console/terminal for output
 *   2. IDT for interrupt handling
 *   3. PMM/VMM for memory management
 *   4. Heap for dynamic allocation
 *   5. ACPI/APIC/IOAPIC for hardware
 *   6. Keyboard for input
 *
 * Then starts the kernel shell.
 */
void kmain(void)
{
	struct limine_framebuffer *fb = limine_get_framebuffer();

	if (fb == NULL)
		return;

	console_init(fb);
	terminal_init();

	log_info("TERM: Initialized");

	log_info("STACK: Top: 0x%llx", cpu_get_stack_top());
	log_info("STACK: Size: %llu bytes", (uint64_t)stack_size);

	gdt_init();
	log_info("GDT: Initialized");

	percpu_init();
	log_info("PERCPU: Initialized");

	fpu_init();
	log_info("FPU: Initialized");

	syscall_init();
	log_info("SYSCALL: Initialized");

	idt_install();
	log_info("IDT: Initialized");

	pmm_init(limine_get_memmap(), limine_get_hhdm_offset());
	log_info("PMM: %llu/%llu pages free", pmm_get_free_pages(), pmm_get_usable_pages());
	log_info("PMM: Initialized");

	vmm_init(limine_get_hhdm_offset());
	/* Print some info about vmm */
	log_info("VMM: Kernel PML4 at physical address 0x%llx", vmm_get_kernel_pml4());
	log_info("VMM: Page size: %llu bytes", PAGE_SIZE);
	log_info("VMM: Initialized");

	kheap_init();
	log_info("HEAP: Initialized");

	/* Print location and size of heap and stack */
	log_info("HEAP: Used: %llu bytes, Free: %llu bytes",
		 (uint64_t)kheap_get_used(), (uint64_t)kheap_get_free());

	acpi_init();
	log_info("ACPI: Initialized");

	apic_init();
	log_info("APIC: Initialized");

	ioapic_init();
	log_info("IOAPIC: Initialized");

	keyboard_init();
	log_info("KEYBOARD: Initialized");

	/* Enable interrupts after all hardware is initialized */
	cpu_enable_interrupts();
	log_info("INT: Enabled");

	/* Print system summary */
	sysinfo_init();
	sysinfo_print_summary();

	/* Init kernel threads */
	kthread_init();
	log_info("KTHREAD: Initialized");

	/* Init virtual filesystem */
	vfs_init();
	log_info("VFS: Initialized");

	/* Init TTY device layer */
	tty_init();
	log_info("TTY: Initialized");

	/* Init process management */
	process_init();
	log_info("PROCESS: Initialized");

	/* Mount initrd and test ext2 */
	struct limine_file *initrd = limine_get_module(0);
	if (initrd) {
		log_info("INITRD: Found at %p, size %llu bytes",
		         initrd->address, initrd->size);

		if (ext2_init(initrd->address, initrd->size) == 0) {
			/* Test: read /hello.txt */
			uint32_t ino = ext2_lookup("/hello.txt");
			if (ino) {
				ext2_inode_t *inode = ext2_read_inode(ino);
				if (inode) {
					char buf[128] = {0};
					ssize_t n = ext2_read_file(inode, 0, buf, sizeof(buf) - 1);
					if (n > 0) {
						/* Remove trailing newline for cleaner output */
						if (buf[n-1] == '\n') buf[n-1] = '\0';
						log_info("INITRD: /hello.txt says: \"%s\"", buf);
					}
				}
			} else {
				log_warn("INITRD: /hello.txt not found");
			}
		}
	} else {
		log_warn("INITRD: No module loaded (boot without initrd)");
	}

#ifdef CONFIG_AUTO_INIT
	/* Automatically launch /init from initrd */
	#include "kinit.h"
	start_init();  /* Does not return */
#else
	/* Start the kernel shell */
	kshell_init();
	kshell_run();  /* Does not return */
#endif
}
