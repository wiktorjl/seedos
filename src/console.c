// Console output: text and image rendering to framebuffer
//
// TODO: Add spinlock/mutex protection for thread safety when kernel
// supports multiple threads/cores. Currently not thread-safe.

#include "console.h"

extern const uint8_t font_data[];   // 8x16 bitmap font (256 glyphs)

static struct limine_framebuffer *framebuffer;
static int cursor_x, cursor_y;      // Cursor position in pixels

/* Cursor blinking state */
static int cursor_visible;          /* Is cursor currently drawn? */
static int cursor_enabled;          /* Is cursor blinking enabled? */
static uint64_t cursor_last_toggle; /* Tick count at last toggle */
static int cursor_draw_x, cursor_draw_y; /* Position where cursor was last drawn */

#define CURSOR_BLINK_TICKS  50      /* Toggle every 50 ticks (500ms at 100Hz) */
#define CURSOR_COLOR        0xCCCCCC /* Light gray cursor */

void console_init(struct limine_framebuffer *fb) {
    framebuffer = fb;
    cursor_x = 0;
    cursor_y = 0;
    cursor_visible = 0;
    cursor_enabled = 1;
    cursor_last_toggle = 0;
    cursor_draw_x = 0;
    cursor_draw_y = 0;
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

/*
 * TODO: Optimize scroll performance. Current implementation copies pixel-by-pixel
 * which is O(width * height). Consider:
 * - Using memcpy() per row for better cache utilization
 * - Ring buffer approach for O(1) logical scrolling
 */
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
    } else if (c == '\b') {
        /* Backspace: move cursor back one character */
        if (cursor_x >= 8) {
            cursor_x -= 8;
        }
    } else if (c == '\t') {
        /* Advance by 4 characters (32 pixels) */
        cursor_x += 32;
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

    int fb_w = framebuffer->width;
    int fb_h = framebuffer->height;

    /* Bounds check: skip if character is fully outside framebuffer */
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;

    const uint8_t *glyph = font_data + (unsigned char)c * 16;
    uint32_t *fb = (uint32_t *)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;    // pixels per row (assuming 32bpp)

    /* Calculate safe drawing bounds (clip to framebuffer edges) */
    int max_row = (y + 16 > fb_h) ? fb_h - y : 16;
    int max_col = (x + 8 > fb_w) ? fb_w - x : 8;

    for (int row = 0; row < max_row; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < max_col; col++) {
            /* Draw foreground or background for each pixel */
            if (bits & (0x80 >> col))
                fb[(y + row) * pitch + (x + col)] = color;
            else
                fb[(y + row) * pitch + (x + col)] = 0x000000;  /* Black background */
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

/* =============================================================================
 * Cursor Rendering
 * =============================================================================
 */

/*
 * Draw cursor as an underline at the current position.
 */
static void draw_cursor(void) {
    if (!framebuffer) return;

    int fb_w = framebuffer->width;
    int fb_h = framebuffer->height;

    /* Don't draw if cursor is off-screen */
    if (cursor_x < 0 || cursor_y < 0 || cursor_x >= fb_w || cursor_y + 16 > fb_h)
        return;

    /* Draw underline cursor (2 pixels tall at bottom of character cell) */
    console_fill_rect(cursor_x, cursor_y + 14, 8, 2, CURSOR_COLOR);

    /* Remember where we drew the cursor */
    cursor_draw_x = cursor_x;
    cursor_draw_y = cursor_y;
}

/*
 * Erase cursor at the position where it was last drawn.
 */
static void erase_cursor(void) {
    if (!framebuffer) return;

    int fb_w = framebuffer->width;
    int fb_h = framebuffer->height;

    if (cursor_draw_x < 0 || cursor_draw_y < 0 || cursor_draw_x >= fb_w || cursor_draw_y + 16 > fb_h)
        return;

    /* Erase underline at last drawn position (draw black) */
    console_fill_rect(cursor_draw_x, cursor_draw_y + 14, 8, 2, 0x000000);
}

void console_update_cursor(uint64_t ticks) {
    if (!cursor_enabled) return;

    /* If cursor has moved since last draw, erase at old position */
    if (cursor_visible && (cursor_x != cursor_draw_x || cursor_y != cursor_draw_y)) {
        erase_cursor();
        draw_cursor();
        cursor_last_toggle = ticks;  /* Reset blink timer on movement */
        return;
    }

    /* Check if it's time to toggle */
    if (ticks - cursor_last_toggle >= CURSOR_BLINK_TICKS) {
        cursor_last_toggle = ticks;

        if (cursor_visible) {
            erase_cursor();
            cursor_visible = 0;
        } else {
            draw_cursor();
            cursor_visible = 1;
        }
    }
}

void console_hide_cursor(void) {
    if (cursor_visible) {
        erase_cursor();
        cursor_visible = 0;
    }
}

void console_show_cursor(void) {
    if (!cursor_visible) {
        draw_cursor();
        cursor_visible = 1;
    }
}
