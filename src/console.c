// Console output: text and image rendering to framebuffer

#include "console.h"

extern const uint8_t font_data[];   // 8x16 bitmap font (256 glyphs)

static struct limine_framebuffer *framebuffer;
static int cursor_x, cursor_y;      // Cursor position in pixels

void console_init(struct limine_framebuffer *fb) {
    framebuffer = fb;
    cursor_x = 0;
    cursor_y = 0;
}

void console_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

void console_putchar(char c, uint32_t color) {
    if (framebuffer == 0) return;

    int fb_w = framebuffer->width;
    int fb_h = framebuffer->height;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 16;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 32) & ~31;   // Align to 4-char tab stop
    } else {
        console_draw_char(c, cursor_x, cursor_y, color);
        cursor_x += 8;
    }

    // Wrap at right edge
    if (cursor_x + 8 > fb_w) {
        cursor_x = 0;
        cursor_y += 16;
    }

    // TODO: scroll when cursor_y exceeds fb_h
}

void console_puts(const char *str, uint32_t color) {
    while (*str)
        console_putchar(*str++, color);
}

void console_draw_char(char c, int x, int y, uint32_t color) {
    if (framebuffer == 0) return;

    const uint8_t *glyph = font_data + (unsigned char)c * 16;
    uint32_t *fb = (uint32_t *)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;    // pixels per row (assuming 32bpp)

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                fb[(y + row) * pitch + (x + col)] = color;
        }
    }
}

void console_draw_string(const char *str, int x, int y, uint32_t color) {
    while (*str) {
        console_draw_char(*str++, x, y, color);
        x += 8;
    }
}

void console_draw_image(const uint32_t *pixels, int img_w, int img_h, int x, int y) {
    if (framebuffer == 0) return;

    uint32_t *fb = (uint32_t *)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;
    int fb_w = framebuffer->width;
    int fb_h = framebuffer->height;

    for (int row = 0; row < img_h && y + row < fb_h; row++) {
        for (int col = 0; col < img_w && x + col < fb_w; col++) {
            fb[(y + row) * pitch + (x + col)] = pixels[row * img_w + col];
        }
    }
}
