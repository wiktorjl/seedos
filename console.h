/*
 * console.h - Unified console output (serial + framebuffer)
 *
 * All kernel output should go through these functions.
 * They write to both serial port and framebuffer.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/*
 * Initialize console (call after fb_init)
 */
void console_init(void);

/*
 * Output functions - write to both serial and framebuffer
 */
void puts(const char *s);
void putc(char c);
void put_hex(uint64_t value);
void put_dec(uint64_t value);

#endif /* CONSOLE_H */
