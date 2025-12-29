/*
 * kernel.c - SeedOS Kernel Entry Point
 *
 * Main kernel initialization and boot sequence.
 */

/* =============================================================================
 * Includes
 * =============================================================================
 */

#include "limine.h"
#include "console.h"
#include "cpu.h"
#include "log.h"
#include "logo.h"
#include "terminal.h"
#include "kprintf.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "acpi.h"
#include "apic.h"
#include "ioapic.h"
#include "keyboard.h"
#include "sysinfo.h"
#include "kshell.h"

/* =============================================================================
 * Kernel Main Entry Point
 * =============================================================================
 */

/*
 * kmain - Kernel entry point after bootloader handoff.
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
void kmain(void) {
    struct limine_framebuffer *fb = limine_get_framebuffer();
    if (fb == NULL) return;

    console_init(fb);
    terminal_init();

    log_info("TERM: Initialized");

    log_info("STACK: Top: 0x%llx", cpu_get_stack_top());
    log_info("STACK: Size: %llu bytes", (uint64_t)stack_size);

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

    sysinfo_init();
    sysinfo_print_summary();

    /* Start the kernel shell */
    kshell_init();
    kshell_run();  /* Does not return */
}
