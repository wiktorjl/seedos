/*
 * fb.h - Framebuffer Graphics Driver
 *
 * This module provides graphics output via the Limine bootloader's framebuffer.
 * It includes both raw pixel drawing and a text console layer on top.
 *
 * Framebuffer Overview:
 *
 *   A framebuffer is a memory region that directly maps to the display.
 *   Each pixel is represented by bytes in memory:
 *
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  Memory Address                                         │
 *   │  fb_addr + (y * pitch) + (x * bytes_per_pixel)          │
 *   │                                                          │
 *   │  ┌────┬────┬────┬────┬────┬────┐                       │
 *   │  │ B  │ G  │ R  │ B  │ G  │ R  │  ...  (32bpp BGRA)    │
 *   │  └────┴────┴────┴────┴────┴────┘                       │
 *   │    pixel 0       pixel 1                                │
 *   └─────────────────────────────────────────────────────────┘
 *
 *   Pitch vs Width:
 *     - Width: Number of visible pixels per row
 *     - Pitch: Number of BYTES per row (may include padding for alignment)
 *     - Always use pitch for row calculations, not width * bpp!
 *
 * Text Console Layer:
 *
 *   The fb_console_* functions provide a text-mode interface on top of
 *   the graphics framebuffer. Characters are drawn using an 8x16 bitmap font.
 *
 *   ┌──────────────────────────────────────────┐
 *   │  Hello, World!_                          │  <- Text console
 *   │  seed>                                   │
 *   │                                          │
 *   └──────────────────────────────────────────┘
 *
 *   Features:
 *     - 8x16 pixel font (95 printable ASCII characters)
 *     - Line wrapping at screen edge
 *     - Automatic scrolling when screen fills
 *     - Handles \n, \r, \t, \b control characters
 *
 * Color Format:
 *
 *   Colors are 32-bit values in 0xRRGGBB format (alpha ignored).
 *   The actual pixel format depends on the framebuffer mode set by
 *   the bootloader, but 32bpp BGRA is most common.
 */

#ifndef FB_H
#define FB_H

#include <stdint.h>

/* =============================================================================
 * Common Color Definitions (0xRRGGBB format)
 * =============================================================================
 */
#define FB_BLACK     0x000000
#define FB_WHITE     0xFFFFFF
#define FB_RED       0xFF0000
#define FB_GREEN     0x00FF00
#define FB_BLUE      0x0000FF
#define FB_CYAN      0x00FFFF
#define FB_MAGENTA   0xFF00FF
#define FB_YELLOW    0xFFFF00
#define FB_GRAY      0x808080
#define FB_DARK_GRAY 0x404040

/* =============================================================================
 * Framebuffer Initialization
 * =============================================================================
 */

/*
 * fb_init - Initialize the framebuffer from Limine bootloader response.
 *
 * @fb_response: Pointer to limine_framebuffer_response structure.
 *
 * Returns: 0 on success, -1 on failure (no framebuffer or unsupported format).
 *
 * Currently only supports 32-bit color depth (bpp = 32).
 * Must be called before any other fb_* functions.
 */
int fb_init(void *fb_response);

/* =============================================================================
 * Raw Framebuffer Access
 * =============================================================================
 */

/*
 * fb_get_width - Get framebuffer width in pixels.
 */
uint32_t fb_get_width(void);

/*
 * fb_get_height - Get framebuffer height in pixels.
 */
uint32_t fb_get_height(void);

/*
 * fb_clear - Fill entire screen with a solid color.
 *
 * @color: 32-bit color value (0xRRGGBB).
 */
void fb_clear(uint32_t color);

/*
 * fb_putpixel - Draw a single pixel.
 *
 * @x:     X coordinate (0 = left edge)
 * @y:     Y coordinate (0 = top edge)
 * @color: 32-bit color value (0xRRGGBB)
 *
 * Coordinates outside the framebuffer are silently ignored.
 */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);

/*
 * fb_putchar_at - Draw a character at a specific pixel position.
 *
 * @c:  ASCII character to draw (32-126, others become '?')
 * @x:  X coordinate of top-left corner of character
 * @y:  Y coordinate of top-left corner of character
 * @fg: Foreground color (text color)
 * @bg: Background color
 *
 * Uses the built-in 8x16 bitmap font.
 */
void fb_putchar_at(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);

/* =============================================================================
 * Text Console Functions
 *
 * These provide a terminal-like interface with automatic cursor management,
 * line wrapping, and scrolling. Uses the 8x16 font.
 * =============================================================================
 */

/*
 * fb_console_init - Initialize the text console.
 *
 * Calculates console dimensions based on framebuffer size and font size.
 * Clears the screen and positions cursor at top-left.
 * Must be called after fb_init().
 */
void fb_console_init(void);

/*
 * fb_console_putc - Write a single character to the console.
 *
 * @c: Character to write.
 *
 * Handles special characters:
 *   - '\n': Move to beginning of next line
 *   - '\r': Move to beginning of current line
 *   - '\t': Advance to next 8-column tab stop
 *   - '\b': Move cursor back one position (erases character)
 *
 * Automatically wraps lines and scrolls when the screen fills.
 */
void fb_console_putc(char c);

/*
 * fb_console_puts - Write a null-terminated string to the console.
 *
 * @s: String to write.
 */
void fb_console_puts(const char *s);

/*
 * fb_console_clear - Clear the console and reset cursor to top-left.
 */
void fb_console_clear(void);

/*
 * fb_cursor_blink_tick - Update cursor blink state.
 *
 * @current_ticks: Current PIT tick count.
 *
 * Called from the timer interrupt handler to toggle cursor visibility.
 * Blinks approximately every 500ms (50 ticks at 100Hz).
 */
void fb_cursor_blink_tick(uint64_t current_ticks);

/* =============================================================================
 * Cursor Style Configuration
 * =============================================================================
 */

/* Cursor style options */
#define FB_CURSOR_BLOCK      0  /* Solid block cursor (full character cell) */
#define FB_CURSOR_UNDERSCORE 1  /* Underscore cursor */

/*
 * fb_set_cursor_style - Set the cursor display style.
 *
 * @style: FB_CURSOR_BLOCK or FB_CURSOR_UNDERSCORE
 */
void fb_set_cursor_style(int style);

/*
 * fb_get_cursor_style - Get the current cursor style.
 *
 * Returns: FB_CURSOR_BLOCK or FB_CURSOR_UNDERSCORE
 */
int fb_get_cursor_style(void);

#endif /* FB_H */