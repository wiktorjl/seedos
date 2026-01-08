/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Console output interface
 *
 * Text and graphics output to framebuffer with cursor tracking,
 * scrollback history, and low-level drawing primitives.
 */

#ifndef _CONSOLE_H
#define _CONSOLE_H

#include "types.h"
#include "limine.h"

/**
 * console_init - Initialize the console with a framebuffer
 * @fb: pointer to Limine framebuffer structure
 */
void console_init(struct limine_framebuffer *fb);

/**
 * console_putchar - Write a character at the cursor position
 * @c: character to write (handles \n, \r, \b, \t)
 * @color: 32-bit RGB color
 */
void console_putchar(char c, uint32_t color);

/**
 * console_puts - Write a null-terminated string at cursor position
 * @str: string to write
 * @color: 32-bit RGB color
 */
void console_puts(const char *str, uint32_t color);

/**
 * console_set_cursor - Set cursor position in pixels
 * @x: X coordinate
 * @y: Y coordinate
 */
void console_set_cursor(int x, int y);

/**
 * console_get_cursor - Get current cursor position in pixels
 * @x: output X coordinate
 * @y: output Y coordinate
 */
void console_get_cursor(int *x, int *y);

/**
 * console_clear - Clear the entire screen
 * @color: fill color
 */
void console_clear(uint32_t color);

/**
 * console_scroll_back - Scroll back into history
 * @lines: number of lines to scroll back
 */
void console_scroll_back(int lines);

/**
 * console_scroll_forward - Scroll forward toward live view
 * @lines: number of lines to scroll forward
 */
void console_scroll_forward(int lines);

/**
 * console_scroll_to_bottom - Return to live view
 */
void console_scroll_to_bottom(void);

/**
 * console_is_scrolled_back - Check if viewing history
 *
 * Return: non-zero if scrolled back, 0 if at live view
 */
int console_is_scrolled_back(void);

/**
 * console_get_dimensions - Get screen size in character cells
 * @cols: output column count (8px per column)
 * @rows: output row count (16px per row)
 */
void console_get_dimensions(int *cols, int *rows);

/**
 * console_draw_char - Draw a character at absolute position
 * @c: character to draw
 * @x: X coordinate in pixels
 * @y: Y coordinate in pixels
 * @color: 32-bit RGB color
 */
void console_draw_char(char c, int x, int y, uint32_t color);

/**
 * console_draw_string - Draw a string at absolute position
 * @str: string to draw
 * @x: X coordinate in pixels
 * @y: Y coordinate in pixels
 * @color: 32-bit RGB color
 */
void console_draw_string(const char *str, int x, int y, uint32_t color);

/**
 * console_draw_image - Draw a raw pixel buffer to the screen
 * @pixels: array of 32-bit RGB pixels (row-major)
 * @w: image width in pixels
 * @h: image height in pixels
 * @x: X coordinate for top-left corner
 * @y: Y coordinate for top-left corner
 */
void console_draw_image(const uint32_t *pixels, int w, int h, int x, int y);

/**
 * console_fill_rect - Fill a rectangle with a solid color
 * @x: X coordinate of top-left corner
 * @y: Y coordinate of top-left corner
 * @w: width in pixels
 * @h: height in pixels
 * @color: 32-bit RGB fill color
 */
void console_fill_rect(int x, int y, int w, int h, uint32_t color);

/**
 * console_update_cursor - Update cursor blink state
 * @ticks: current tick count
 */
void console_update_cursor(uint64_t ticks);

/**
 * console_hide_cursor - Hide the blinking cursor
 */
void console_hide_cursor(void);

/**
 * console_show_cursor - Show the blinking cursor
 */
void console_show_cursor(void);

/**
 * console_set_fullscreen - Enable or disable fullscreen mode
 * @enabled: 1 to suppress text output, 0 to restore
 */
void console_set_fullscreen(int enabled);

/**
 * console_get_fullscreen - Check if fullscreen mode is active
 *
 * Return: non-zero if fullscreen, 0 if normal
 */
int console_get_fullscreen(void);

#endif /* _CONSOLE_H */
