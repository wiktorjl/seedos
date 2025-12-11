/*
 * fb.h - Framebuffer driver
 */

#ifndef FB_H
#define FB_H

#include <stdint.h>

/*
 * Initialize the framebuffer
 * Returns 0 on success, -1 on failure
 */
int fb_init(void *fb_response);

/*
 * Get framebuffer dimensions
 */
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);

/*
 * Clear the screen with a color
 */
void fb_clear(uint32_t color);

/*
 * Draw a single pixel
 */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);

/*
 * Draw a character at pixel position
 */
void fb_putchar_at(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);

/*
 * Console functions - treat framebuffer as a text console
 */
void fb_console_init(void);
void fb_console_putc(char c);
void fb_console_puts(const char *s);
void fb_console_clear(void);

/* Common colors (RGB) */
#define FB_BLACK   0x000000
#define FB_WHITE   0xFFFFFF
#define FB_RED     0xFF0000
#define FB_GREEN   0x00FF00
#define FB_BLUE    0x0000FF
#define FB_CYAN    0x00FFFF
#define FB_MAGENTA 0xFF00FF
#define FB_YELLOW  0xFFFF00
#define FB_GRAY    0x808080
#define FB_DARK_GRAY 0x404040

#endif /* FB_H */