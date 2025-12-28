// SeedOS kernel entry point

#include "limine.h"
#include "console.h"
#include "log.h"
#include "logo.h"
#include "terminal.h"
#include "kprintf.h"

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

    // Test log messages
    log_debug("This debug message is hidden (below LOG_INFO)");
    log_trace("This trace message is also hidden");

    kprintf("\nWelcome to SeedOS!\n");
}
