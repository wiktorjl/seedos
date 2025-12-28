#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include "limine.h"

void console_init(struct limine_framebuffer *fb);
void console_draw_char(char c, int x, int y, uint32_t color);
void console_draw_string(const char *str, int x, int y, uint32_t color);
void console_draw_image(const uint32_t *pixels, int img_w, int img_h, int x, int y);

#endif
