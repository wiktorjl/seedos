// SeedOS kernel entry point

#include "limine.h"
#include "console.h"
#include "cpu.h"
#include "log.h"
#include "logo.h"
#include "terminal.h"
#include "kprintf.h"
#include "idt.h"
#include "pmm.h"

void kmain(void) {
    struct limine_framebuffer *fb = limine_get_framebuffer();
    if (fb == 0) return;

    console_init(fb);
    terminal_init();
    logo_display();
    log_info("TERM: Initialized");

    idt_install();
    log_info("IDT: Initialized");

    pmm_init(limine_get_memmap(), limine_get_hhdm_offset());
    log_info("PMM: %llu/%llu pages free", pmm_get_free_pages(), pmm_get_usable_pages());
    log_info("PMM: Initialized");
    
    cpu_enable_interrupts();
    log_info("INT: Enabled");
    kprintf("\nWelcome to SeedOS!\n");

    /* Idle loop - halt until interrupt, then loop back */
    for (;;) {
        asm volatile ("hlt");
    }
}
