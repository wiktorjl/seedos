/*
 * console.c - Console Output
 *
 * Text and graphics rendering to the framebuffer. Uses an 8x16 bitmap font
 * for text output with automatic line wrapping and scrolling.
 *
 * All text output is stored in a scrollback buffer. The framebuffer is
 * rendered from this buffer, enabling Page Up/Down navigation through history.
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
static int screen_rows, screen_cols; /* Screen dimensions in characters */

/* Cursor state */
static int cursor_visible;          /* Is cursor currently drawn? */
static int cursor_enabled;          /* Is cursor blinking enabled? */
static uint64_t cursor_last_toggle; /* Tick count at last toggle */
static int cursor_draw_x, cursor_draw_y; /* Position where cursor was last drawn */

#define CURSOR_BLINK_TICKS  50      /* Toggle every 50 ticks (500ms at 100Hz) */
#define CURSOR_COLOR        0xCCCCCC /* Light gray cursor */

/* =============================================================================
 * Internal Helpers
 * =============================================================================
 */

static void scrollback_init(void) {
    scrollback_head = 0;
    scrollback_count = 1;  /* Start with 1 line (the current line) */
    view_offset = 0;
    current_col = 0;
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

/* Get a line from the scrollback buffer by offset from bottom */
static console_line_t *scrollback_get_line(int lines_from_bottom) {
    if (lines_from_bottom >= scrollback_count) {
        return 0;
    }
    int idx = (scrollback_head - lines_from_bottom + SCROLLBACK_LINES) % SCROLLBACK_LINES;
    return &scrollback[idx];
}

/* Get current cursor position in pixels based on scrollback state */
static void get_cursor_position(int *x, int *y) {
    int cursor_row = scrollback_count - 1;
    if (cursor_row >= screen_rows) {
        cursor_row = screen_rows - 1;
    }
    *y = cursor_row * 16;
    *x = current_col * 8;
}

/* Render visible portion of scrollback buffer to framebuffer */
static void render_view(void) {
    if (!framebuffer) return;

    /* Clear screen */
    console_fill_rect(0, 0, framebuffer->width, framebuffer->height, 0x000000);

    /* Render lines from scrollback buffer */
    for (int row = 0; row < screen_rows; row++) {
        int lines_from_bottom = view_offset + (screen_rows - 1 - row);
        console_line_t *line = scrollback_get_line(lines_from_bottom);

        if (!line) continue;

        int y = row * 16;
        for (int col = 0; col < line->length && col < screen_cols; col++) {
            int x = col * 8;
            console_draw_char(line->cells[col].ch, x, y, line->cells[col].color);
        }
    }
}

/* Draw cursor as an underline */
static void draw_cursor(void) {
    if (!framebuffer || view_offset > 0) return;

    int x, y;
    get_cursor_position(&x, &y);

    if (x < 0 || y < 0 || x >= (int)framebuffer->width || y + 16 > (int)framebuffer->height)
        return;

    console_fill_rect(x, y + 14, 8, 2, CURSOR_COLOR);
    cursor_draw_x = x;
    cursor_draw_y = y;
}

/* Erase cursor at last drawn position */
static void erase_cursor(void) {
    if (!framebuffer) return;

    if (cursor_draw_x < 0 || cursor_draw_y < 0 ||
        cursor_draw_x >= (int)framebuffer->width ||
        cursor_draw_y + 16 > (int)framebuffer->height)
        return;

    console_fill_rect(cursor_draw_x, cursor_draw_y + 14, 8, 2, 0x000000);
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

void console_init(struct limine_framebuffer *fb) {
    framebuffer = fb;
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

void console_get_cursor(int *x, int *y) {
    get_cursor_position(x, y);
}

void console_set_cursor(int x, int y) {
    /* Convert pixel position to column position for scrollback */
    current_col = x / 8;
    if (current_col > MAX_COLS) current_col = MAX_COLS;

    /* Adjust scrollback line count if cursor is positioned below current content */
    int target_row = y / 16;
    while (scrollback_count <= target_row && scrollback_count < SCROLLBACK_LINES) {
        scrollback_newline();
    }
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
    scrollback_init();
}

void console_putchar(char c, uint32_t color) {
    if (!framebuffer) return;

    /* If scrolled back, return to live view on any input */
    if (view_offset > 0) {
        view_offset = 0;
        render_view();
    }

    /* Calculate screen row for current line */
    int screen_row = scrollback_count - 1;
    if (screen_row >= screen_rows) {
        screen_row = screen_rows - 1;
    }

    if (c == '\n') {
        scrollback_newline();
        /* If we exceeded screen, scroll up by re-rendering */
        if (scrollback_count > screen_rows) {
            render_view();
        }
    } else if (c == '\r') {
        current_col = 0;
    } else if (c == '\b') {
        if (current_col > 0) {
            current_col--;
            scrollback[scrollback_head].length = current_col;
            /* Erase character at old position */
            console_fill_rect(current_col * 8, screen_row * 16, 8, 16, 0x000000);
        }
    } else if (c == '\t') {
        int old_col = current_col;
        for (int i = 0; i < 4 && current_col < MAX_COLS; i++) {
            scrollback_putchar(' ', color);
        }
        /* Draw spaces directly */
        for (int i = old_col; i < current_col; i++) {
            console_draw_char(' ', i * 8, screen_row * 16, color);
        }
    } else {
        /* Normal character - draw directly to screen */
        int draw_col = current_col;
        scrollback_putchar(c, color);
        console_draw_char(c, draw_col * 8, screen_row * 16, color);
    }

    /* Wrap at right edge */
    if (current_col >= screen_cols) {
        scrollback_newline();
        if (scrollback_count > screen_rows) {
            render_view();
        }
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

    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;

    const uint8_t *glyph = font_data + (unsigned char)c * 16;
    uint32_t *fb = (uint32_t *)framebuffer->address;
    uint64_t pitch = framebuffer->pitch / 4;

    int max_row = (y + 16 > fb_h) ? fb_h - y : 16;
    int max_col = (x + 8 > fb_w) ? fb_w - x : 8;

    for (int row = 0; row < max_row; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < max_col; col++) {
            if (bits & (0x80 >> col))
                fb[(y + row) * pitch + (x + col)] = color;
            else
                fb[(y + row) * pitch + (x + col)] = 0x000000;
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

    int max_offset = scrollback_count > screen_rows ? scrollback_count - screen_rows : 0;

    int new_offset = view_offset + lines;
    if (new_offset > max_offset) {
        new_offset = max_offset;
    }

    view_offset = new_offset;
    render_view();
}

void console_scroll_forward(int lines) {
    if (!framebuffer || lines <= 0) return;

    view_offset -= lines;
    if (view_offset < 0) {
        view_offset = 0;
    }

    render_view();
}

void console_scroll_to_bottom(void) {
    if (view_offset == 0) return;

    view_offset = 0;
    render_view();

    if (cursor_visible) {
        draw_cursor();
    }
}

int console_is_scrolled_back(void) {
    return view_offset > 0;
}

/* =============================================================================
 * Cursor Blinking
 * =============================================================================
 */

void console_update_cursor(uint64_t ticks) {
    if (!cursor_enabled || view_offset > 0) return;

    int cur_x, cur_y;
    get_cursor_position(&cur_x, &cur_y);

    /* If cursor has moved, update immediately */
    if (cursor_visible && (cur_x != cursor_draw_x || cur_y != cursor_draw_y)) {
        erase_cursor();
        draw_cursor();
        cursor_last_toggle = ticks;
        return;
    }

    /* Toggle cursor on interval */
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
    if (!cursor_visible && view_offset == 0) {
        draw_cursor();
        cursor_visible = 1;
    }
}
