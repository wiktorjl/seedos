// Console output interface (framebuffer backend)

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include "limine.h"

void console_init(struct limine_framebuffer *fb);

// High-level text output (tracks cursor, handles newlines)
void console_putchar(char c, uint32_t color);
void console_puts(const char *str, uint32_t color);
void console_set_cursor(int x, int y);
void console_get_cursor(int *x, int *y);

// Screen management
void console_clear(uint32_t color);
void console_scroll(int lines);
void console_get_dimensions(int *cols, int *rows);

// Low-level drawing (no cursor tracking)
void console_draw_char(char c, int x, int y, uint32_t color);
void console_draw_string(const char *str, int x, int y, uint32_t color);
void console_draw_image(const uint32_t *pixels, int w, int h, int x, int y);
void console_fill_rect(int x, int y, int w, int h, uint32_t color);

#endif
