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

/* =============================================================================
 * Test Functions
 * =============================================================================
 */

static void test_heap(void) {
    log_info("HEAP: Running tests...");

    /* Test 1: Basic allocation */
    void *p1 = kmalloc(64);
    if (p1 == NULL) {
        log_error("HEAP: Test 1 FAILED - kmalloc(64) returned NULL");
        return;
    }
    log_info("HEAP: Test 1 OK - kmalloc(64) = %p", p1);

    /* Test 2: Zero-initialized allocation */
    uint8_t *p2 = kzalloc(128);
    if (p2 == NULL) {
        log_error("HEAP: Test 2 FAILED - kzalloc(128) returned NULL");
        return;
    }
    int zeroed = 1;
    for (int i = 0; i < 128; i++) {
        if (p2[i] != 0) {
            zeroed = 0;
            break;
        }
    }
    if (!zeroed) {
        log_error("HEAP: Test 2 FAILED - kzalloc memory not zeroed");
        return;
    }
    log_info("HEAP: Test 2 OK - kzalloc(128) = %p (zeroed)", p2);

    /* Test 3: Multiple allocations */
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(100 + i * 50);
        if (ptrs[i] == NULL) {
            log_error("HEAP: Test 3 FAILED - allocation %d returned NULL", i);
            return;
        }
    }
    log_info("HEAP: Test 3 OK - 10 allocations succeeded");

    /* Test 4: Free and realloc */
    kfree(p1);
    kfree(p2);
    for (int i = 0; i < 10; i++) {
        kfree(ptrs[i]);
    }
    log_info("HEAP: Test 4 OK - all frees succeeded");

    /* Test 5: Realloc */
    void *p3 = kmalloc(32);
    void *p4 = krealloc(p3, 256);
    if (p4 == NULL) {
        log_error("HEAP: Test 5 FAILED - krealloc returned NULL");
        return;
    }
    log_info("HEAP: Test 5 OK - krealloc(32 -> 256) = %p", p4);
    kfree(p4);

    /* Test 6: Large allocation */
    void *p5 = kmalloc(16384);
    if (p5 == NULL) {
        log_error("HEAP: Test 6 FAILED - kmalloc(16384) returned NULL");
        return;
    }
    log_info("HEAP: Test 6 OK - kmalloc(16384) = %p", p5);
    kfree(p5);

    log_info("HEAP: All tests passed! Used: %llu bytes, Free: %llu bytes",
             (uint64_t)kheap_get_used(), (uint64_t)kheap_get_free());
}

/* =============================================================================
 * Kernel Main Entry Point
 * =============================================================================
 */

void kmain(void) {
    struct limine_framebuffer *fb = limine_get_framebuffer();
    if (fb == NULL) return;

    console_init(fb);
    terminal_init();
    logo_display();
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

    cpu_enable_interrupts();
    log_info("INT: Enabled");

    acpi_init();
    log_info("ACPI: Initialized");

    apic_init();
    log_info("APIC: Initialized");

    ioapic_init();
    log_info("IOAPIC: Initialized");

    keyboard_init();
    log_info("KEYBOARD: Initialized");

    kprintf("\nWelcome to SeedOS!\n");
    kprintf("Type something: ");

    /* Simple echo loop */
    for (;;) {
        int c = keyboard_getchar();
        if (c != -1) {
            if (c == '\n' || c == '\r') {
                kprintf("\n");
            } else if (c == KEY_BACKSPACE) {
                kprintf("\b \b");  /* Backspace, space, backspace */
            } else if (c == KEY_TAB) {
                kprintf("\t");
            } else if (c >= 32 && c < 127) {
                kprintf("%c", c);
            }
        } else {
            __asm__ volatile ("hlt");  /* Wait for interrupt */
        }
    }
}
