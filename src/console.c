#include "console.h"

// Font data - 256 chars, 16 bytes each (defined in boot.S)
extern const uint8_t font_data[];

static struct limine_framebuffer *framebuffer;

void console_init(struct limine_framebuffer *fb) {
    framebuffer = fb;
}

void console_draw_char(char c, int x, int y, uint32_t color) {
    if (!framebuffer) return;

    const uint8_t *glyph = font_data + (unsigned char)c * 16;
    uint32_t *fb_base = (uint32_t *)framebuffer->address;
    uint64_t pitch_pixels = framebuffer->pitch / 4;

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fb_base[(y + row) * pitch_pixels + (x + col)] = color;
            }
        }
    }
}

void console_draw_string(const char *str, int x, int y, uint32_t color) {
    while (*str) {
        console_draw_char(*str, x, y, color);
        x += 8;
        str++;
    }
}
