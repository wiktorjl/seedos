#include "limine.h"
#include "console.h"
#include "seedos.h"

extern const uint32_t seedos_data[];

void kmain(void) {
    struct limine_framebuffer *fb = limine_get_framebuffer();

    if (!fb) {
        return;
    }

    console_init(fb);
    console_draw_image(seedos_data, SEEDOS_WIDTH, SEEDOS_HEIGHT, 0, 0);
    console_draw_string("[  ok  ]  Initialized framebuffer]", 0, 270, 0x00FF00);
    console_draw_string("[ info ]  Hello, SeedOS!", 0, 270 + 16, 0x00FFFFFF);
}
