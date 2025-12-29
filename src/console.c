/*
 * console.c - Console Output
 *
 * Text and graphics rendering to the framebuffer. Uses an 8x16 bitmap font
 * for text output with automatic line wrapping and scrolling.
 *
 * Includes a scrollback buffer for Page Up/Down navigation through history.
 */

#include "console.h"

extern const uint8_t font_data[];   /* 8x16 bitmap font (256 glyphs) */

/* =============================================================================
 * Scrollback Buffer
 * =============================================================================
 */

#define SCROLLBACK_LINES    1000    /* Number of lines to keep in history */
#define MAX_COLS            160     /* Max columns per line (enough for wide screens) */

typedef struct {
    char ch;
    uint32_t color;
} console_cell_t;

typedef struct {
    console_cell_t cells[MAX_COLS];
    int length;                     /* Number of chars in this line */
} console_line_t;

static console_line_t scrollback[SCROLLBACK_LINES];
static int scrollback_head;         /* Index of current line being written */
static int scrollback_count;        /* Total lines in buffer (max SCROLLBACK_LINES) */
static int view_offset;             /* Lines scrolled back from live (0 = live view) */
static int current_col;             /* Current column in the current line */

/* =============================================================================
 * Framebuffer State
 * =============================================================================
 */

static struct limine_framebuffer *framebuffer;
static int cursor_x, cursor_y;      /* Cursor position in pixels */
static int screen_rows, screen_cols; /* Screen dimensions in characters */

/* Cursor blinking state */
static int cursor_visible;          /* Is cursor currently drawn? */
static int cursor_enabled;          /* Is cursor blinking enabled? */
static uint64_t cursor_last_toggle; /* Tick count at last toggle */
static int cursor_draw_x, cursor_draw_y; /* Position where cursor was last drawn */

#define CURSOR_BLINK_TICKS  50      /* Toggle every 50 ticks (500ms at 100Hz) */
#define CURSOR_COLOR        0xCCCCCC /* Light gray cursor */

/* Forward declarations */
static void draw_cursor(void);
static void erase_cursor(void);

/* =============================================================================
 * Internal Helpers
 * =============================================================================
 */

static void scrollback_init(void) {
    scrollback_head = 0;
    scrollback_count = 1;  /* Start with 1 line (the current line) */
    view_offset = 0;
    current_col = 0;

    /* Initialize first line */
    scrollback[0].length = 0;
}

/* Advance to the next line in the scrollback buffer */
static void scrollback_newline(void) {
    scrollback_head = (scrollback_head + 1) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    }
    scrollback[scrollback_head].length = 0;
    current_col = 0;
}

/* Add a character to the current line in the scrollback buffer */
static void scrollback_putchar(char c, uint32_t color) {
    if (current_col < MAX_COLS) {
        console_line_t *line = &scrollback[scrollback_head];
        line->cells[current_col].ch = c;
        line->cells[current_col].color = color;
        current_col++;
        line->length = current_col;
    }
}

/* Get a line from the scrollback buffer (0 = oldest visible, going up) */
static console_line_t *scrollback_get_line(int lines_from_bottom) {
    if (lines_from_bottom >= scrollback_count) {
        return 0;  /* No line that far back */
    }

    int idx = (scrollback_head - lines_from_bottom + SCROLLBACK_LINES) % SCROLLBACK_LINES;
    return &scrollback[idx];
}

/* Render visible portion of scrollback buffer to framebuffer */
static void console_render_view(void) {
    if (!framebuffer) return;

    /* Clear screen */
    console_fill_rect(0, 0, framebuffer->width, framebuffer->height, 0x000000);

    /* Calculate how many lines we can display */
    int visible_rows = screen_rows;

    /* Render lines from scrollback buffer */
    for (int row = 0; row < visible_rows; row++) {
        /* Calculate which line from scrollback to display */
        int lines_from_bottom = view_offset + (visible_rows - 1 - row);
        console_line_t *line = scrollback_get_line(lines_from_bottom);

        if (!line) continue;  /* No content that far back */

        /* Render each character in the line */
        int y = row * 16;
        for (int col = 0; col < line->length && col < screen_cols; col++) {
            int x = col * 8;
            console_draw_char(line->cells[col].ch, x, y, line->cells[col].color);
        }
    }

    /* Update cursor position for live view */
    if (view_offset == 0) {
        /* In live view, cursor is at the write position */
        int cursor_row = scrollback_count > 0 ? (scrollback_count - 1) : 0;
        if (cursor_row >= screen_rows) {
            cursor_row = screen_rows - 1;
        }
        cursor_y = cursor_row * 16;
        cursor_x = current_col * 8;
    }
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

void console_init(struct limine_framebuffer *fb) {
    framebuffer = fb;
    cursor_x = 0;
    cursor_y = 0;
    cursor_visible = 0;
    cursor_enabled = 1;
    cursor_last_toggle = 0;
    cursor_draw_x = 0;
    cursor_draw_y = 0;

    if (fb) {
        screen_cols = fb->width / 8;
        screen_rows = fb->height / 16;
    } else {
        screen_cols = 80;
        screen_rows = 25;
    }

    scrollback_init();
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
    if (cols) *cols = screen_cols;
    if (rows) *rows = screen_rows;
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

    /* Also reset scrollback */
    scrollback_init();
}

void console_scroll(int lines) {
    /* With scrollback buffer, we don't need to copy pixels anymore.
     * The scrollback buffer handles history, and we just re-render. */
    if (!framebuffer || lines <= 0) return;

    /* If we're in scrolled-back view, adjust view_offset */
    if (view_offset > 0) {
        view_offset += lines;
    }
}

void console_putchar(char c, uint32_t color) {
    if (!framebuffer) return;

    if (c == '\n') {
        scrollback_newline();
        cursor_x = 0;
        cursor_y += 16;
    } else if (c == '\r') {
        current_col = 0;
        cursor_x = 0;
    } else if (c == '\b') {
        /* Backspace: move cursor back one character */
        if (current_col > 0) {
            current_col--;
            scrollback[scrollback_head].length = current_col;
        }
        if (cursor_x >= 8) {
            cursor_x -= 8;
        }
    } else if (c == '\t') {
        /* Advance by 4 characters */
        for (int i = 0; i < 4; i++) {
            scrollback_putchar(' ', color);
        }
        cursor_x += 32;
    } else {
        scrollback_putchar(c, color);

        /* Only render to framebuffer if in live view */
        if (view_offset == 0) {
            console_draw_char(c, cursor_x, cursor_y, color);
        }
        cursor_x += 8;
    }

    /* Wrap at right edge */
    if (cursor_x + 8 > (int)framebuffer->width) {
        scrollback_newline();
        cursor_x = 0;
        cursor_y += 16;
    }

    /* Scroll when cursor exceeds screen height */
    if (cursor_y + 16 > (int)framebuffer->height) {
        /* Scroll the framebuffer up if in live view */
        if (view_offset == 0) {
            /* Move framebuffer content up */
            uint32_t *fb = (uint32_t *)framebuffer->address;
            uint64_t pitch = framebuffer->pitch / 4;
            int fb_h = framebuffer->height;
            int scroll_pixels = 16;

            for (int row = 0; row < fb_h - scroll_pixels; row++) {
                for (uint64_t col = 0; col < pitch; col++) {
                    fb[row * pitch + col] = fb[(row + scroll_pixels) * pitch + col];
                }
            }

            /* Clear the bottom line */
            for (int row = fb_h - scroll_pixels; row < fb_h; row++) {
                for (uint64_t col = 0; col < pitch; col++) {
                    fb[row * pitch + col] = 0x000000;
                }
            }
        }

        cursor_y -= 16;
        if (cursor_y < 0) cursor_y = 0;
    }
}

void console_puts(const char *str, uint32_t color) {
    while (*str)
        console_putchar(*str++, color);
}

void console_draw_char(char c, int x, int y, uint32_t color) {
    if (!framebuffer) return;

    int fb_w = framebuffer->width;
    int fb_h = framebuffer->height;

    /* Bounds check: skip if character is fully outside framebuffer */
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;

    const uint8_t *glyph = font_data + (unsigned char)c * 16;
    uint32_t *fb = (uint32_t *)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;    /* pixels per row (assuming 32bpp) */

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
    if (!framebuffer) return;

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
 * Scrollback Navigation
 * =============================================================================
 */

void console_scroll_back(int lines) {
    if (!framebuffer || lines <= 0) return;

    /* Calculate max scroll (can't scroll past oldest line).
     * scrollback_count includes current line, so we can scroll back
     * until the oldest line reaches the top of the screen. */
    int max_offset = scrollback_count > screen_rows ? scrollback_count - screen_rows : 0;

    int new_offset = view_offset + lines;
    if (new_offset > max_offset) {
        new_offset = max_offset;
    }

    view_offset = new_offset;
    console_render_view();
}

void console_scroll_forward(int lines) {
    if (!framebuffer || lines <= 0) return;

    view_offset -= lines;
    if (view_offset < 0) {
        view_offset = 0;
    }

    console_render_view();
}

void console_scroll_to_bottom(void) {
    if (view_offset == 0) return;  /* Already at bottom */

    view_offset = 0;
    console_render_view();

    /* Redraw cursor at current position */
    if (cursor_visible) {
        draw_cursor();
    }
}

int console_is_scrolled_back(void) {
    return view_offset > 0;
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

    /* Don't blink cursor when scrolled back */
    if (view_offset > 0) return;

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
