// SeedOS kernel entry point

#include "limine.h"
#include "console.h"
#include "log.h"
#include "logo.h"
#include "terminal.h"
#include "kprintf.h"
#include "idt.h"

extern const uint32_t logo_data[];

void kmain(void) {
    struct limine_framebuffer *fb = limine_get_framebuffer();
    if (fb == 0) return;

    console_init(fb);
    terminal_init();

    // Display logo and log boot messages
    console_draw_image(logo_data, LOGO_WIDTH, LOGO_HEIGHT, 0, 0);
    console_set_cursor(0, LOGO_HEIGHT + 8);
    log_info("Initialized framebuffer");
    log_info("Initialized console");
    log_info("Initialized terminal");

    idt_install();
    log_info("Initialized IDT");

    asm volatile ("sti");
    log_info("Enabled interrupts");

    // Test log messages
    log_debug("This debug message is hidden (below LOG_INFO)");
    log_trace("This trace message is also hidden");

    kprintf("\nWelcome to SeedOS!\n");

    /* To test interrupt handling and stack trace, uncomment the following */
    /*
    int k = 42;
    int l = k / 0;
    kprintf("The answer to life, the universe, and everything is %d\n", k);
    */
}
