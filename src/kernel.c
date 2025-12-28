#include "limine.h"
#include "console.h"

void kmain(void) {
    struct limine_framebuffer *fb = limine_get_framebuffer();
    if (!fb) {
        return;
    }

    console_init(fb);
    console_draw_string("Hello, SeedOS!", 0, 0, 0x00FFFFFF);
}
