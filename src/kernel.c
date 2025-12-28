// SeedOS kernel entry point

#include "limine.h"
#include "console.h"
#include "io.h"
#include "log.h"
#include "logo.h"

extern const uint32_t logo_data[];

void kmain(void) {
    struct limine_framebuffer *fb = limine_get_framebuffer();
    if (fb == 0) return;

    console_init(fb);
    io_init();
    log_init();

    // Display logo
    console_draw_image(logo_data, LOGO_WIDTH, LOGO_HEIGHT, 0, 0);
    console_set_cursor(0, LOGO_HEIGHT + 8);

    // Test logging
    log_info("Initialized framebuffer");
    log_debug("This debug message is hidden (below LOG_INFO)");
    log_trace("This trace message is also hidden");    
    puts("\nWelcome to SeedOS!\n");
}
