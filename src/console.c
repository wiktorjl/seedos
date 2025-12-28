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

void console_get_cursor(int *x, int *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void console_get_dimensions(int *cols, int *rows) {
    if (!framebuffer) {
        if (cols) *cols = 0;
        if (rows) *rows = 0;
        return;
    }
    if (cols) *cols = framebuffer->width / 8;
    if (rows) *rows = framebuffer->height / 16;
}

void console_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!framebuffer) return;

    uint32_t *fb = (uint32_t *)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;
    int fb_w = framebuffer->width;
    int fb_h = framebuffer->height;

    for (int row = 0; row < h && y + row < fb_h; row++) {
        for (int col = 0; col < w && x + col < fb_w; col++) {
            fb[(y + row) * pitch + (x + col)] = color;
        }
    }
}

void console_clear(uint32_t color) {
    if (!framebuffer) return;
    console_fill_rect(0, 0, framebuffer->width, framebuffer->height, color);
    cursor_x = 0;
    cursor_y = 0;
}

void console_scroll(int lines) {
    if (!framebuffer || lines <= 0) return;

    uint32_t *fb = (uint32_t *)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;
    int fb_h = framebuffer->height;
    int scroll_pixels = lines * 16;

    if (scroll_pixels >= fb_h) {
        console_clear(0x000000);
        return;
    }

    // Move framebuffer content up
    for (int row = 0; row < fb_h - scroll_pixels; row++) {
        for (uint64_t col = 0; col < pitch; col++) {
            fb[row * pitch + col] = fb[(row + scroll_pixels) * pitch + col];
        }
    }

    // Clear the bottom area
    for (int row = fb_h - scroll_pixels; row < fb_h; row++) {
        for (uint64_t col = 0; col < pitch; col++) {
            fb[row * pitch + col] = 0x000000;
        }
    }

    // Adjust cursor
    cursor_y -= scroll_pixels;
    if (cursor_y < 0) cursor_y = 0;
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

    // Scroll when cursor exceeds screen height
    if (cursor_y + 16 > fb_h) {
        console_scroll(1);
    }
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
