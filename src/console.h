/*
 * console.h - Console Output Interface
 *
 * Provides text and graphics output to the framebuffer. Includes both
 * high-level text output (with cursor tracking and scrolling) and
 * low-level drawing primitives.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"
#include "limine.h"

/*
 * console_init - Initialize the console with a framebuffer.
 *
 * @fb: Pointer to Limine framebuffer structure.
 *
 * Must be called before any other console functions.
 */
void console_init(struct limine_framebuffer *fb);

/* =============================================================================
 * High-Level Text Output
 *
 * These functions track cursor position and handle control characters.
 * =============================================================================
 */

/*
 * console_putchar - Write a character at the cursor position.
 *
 * @c:     Character to write (handles \n, \r, \b, \t).
 * @color: 32-bit RGB color (0xRRGGBB).
 *
 * Advances cursor and scrolls if necessary.
 */
void console_putchar(char c, uint32_t color);

/*
 * console_puts - Write a null-terminated string at the cursor position.
 *
 * @str:   String to write.
 * @color: 32-bit RGB color (0xRRGGBB).
 */
void console_puts(const char *str, uint32_t color);

/*
 * console_set_cursor - Set cursor position in pixels.
 *
 * @x: X coordinate (pixels from left).
 * @y: Y coordinate (pixels from top).
 */
void console_set_cursor(int x, int y);

/*
 * console_get_cursor - Get current cursor position.
 *
 * @x: Output pointer for X coordinate (may be NULL).
 * @y: Output pointer for Y coordinate (may be NULL).
 */
void console_get_cursor(int *x, int *y);

/* =============================================================================
 * Screen Management
 * =============================================================================
 */

/*
 * console_clear - Clear the entire screen.
 *
 * @color: Fill color (32-bit RGB).
 *
 * Resets cursor to top-left corner.
 */
void console_clear(uint32_t color);

/*
 * console_scroll_back - Scroll back into history (Page Up).
 *
 * @lines: Number of lines to scroll back.
 */
void console_scroll_back(int lines);

/*
 * console_scroll_forward - Scroll forward toward live view (Page Down).
 *
 * @lines: Number of lines to scroll forward.
 */
void console_scroll_forward(int lines);

/*
 * console_scroll_to_bottom - Return to live view.
 *
 * Call this when user types while scrolled back.
 */
void console_scroll_to_bottom(void);

/*
 * console_is_scrolled_back - Check if viewing history.
 *
 * Returns: 1 if scrolled back, 0 if at live view.
 */
int console_is_scrolled_back(void);

/*
 * console_get_dimensions - Get screen size in character cells.
 *
 * @cols: Output pointer for column count (8px per column, may be NULL).
 * @rows: Output pointer for row count (16px per row, may be NULL).
 */
void console_get_dimensions(int *cols, int *rows);

/* =============================================================================
 * Low-Level Drawing
 *
 * These functions draw at absolute positions without cursor tracking.
 * =============================================================================
 */

/*
 * console_draw_char - Draw a single character at a specific position.
 *
 * @c:     Character to draw.
 * @x:     X coordinate in pixels.
 * @y:     Y coordinate in pixels.
 * @color: 32-bit RGB color.
 */
void console_draw_char(char c, int x, int y, uint32_t color);

/*
 * console_draw_string - Draw a string at a specific position.
 *
 * @str:   String to draw.
 * @x:     X coordinate in pixels.
 * @y:     Y coordinate in pixels.
 * @color: 32-bit RGB color.
 */
void console_draw_string(const char *str, int x, int y, uint32_t color);

/*
 * console_draw_image - Draw a raw pixel buffer to the screen.
 *
 * @pixels: Array of 32-bit RGB pixels (row-major order).
 * @w:      Image width in pixels.
 * @h:      Image height in pixels.
 * @x:      X coordinate for top-left corner.
 * @y:      Y coordinate for top-left corner.
 */
void console_draw_image(const uint32_t *pixels, int w, int h, int x, int y);

/*
 * console_fill_rect - Fill a rectangular region with a solid color.
 *
 * @x:     X coordinate of top-left corner.
 * @y:     Y coordinate of top-left corner.
 * @w:     Width in pixels.
 * @h:     Height in pixels.
 * @color: 32-bit RGB fill color.
 */
void console_fill_rect(int x, int y, int w, int h, uint32_t color);

/* =============================================================================
 * Cursor Blinking
 * =============================================================================
 */

/*
 * console_update_cursor - Update cursor blink state.
 *
 * @ticks: Current tick count (for timing blink interval).
 *
 * Should be called periodically from the timer interrupt.
 */
void console_update_cursor(uint64_t ticks);

/*
 * console_hide_cursor - Hide the blinking cursor.
 */
void console_hide_cursor(void);

/*
 * console_show_cursor - Show the blinking cursor.
 */
void console_show_cursor(void);

#endif /* CONSOLE_H */
