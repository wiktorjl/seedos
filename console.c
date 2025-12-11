/*
 * console.c - Unified console output (serial + framebuffer)
 */

#include "console.h"
#include "fb.h"

/* External serial functions from kernel.c */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void serial_put_hex(uint64_t value);
extern void serial_put_dec(uint64_t value);

/* Flag to track if framebuffer is ready */
static int fb_ready = 0;

void console_init(void) {
    fb_ready = 1;
}

void puts(const char *s) {
    serial_puts(s);
    if (fb_ready) {
        fb_console_puts(s);
    }
}

void putc(char c) {
    serial_putc(c);
    if (fb_ready) {
        fb_console_putc(c);
    }
}

/*
 * Hex printing constants.
 * HEX64_DIGITS = 16 nibbles in a 64-bit value
 * HEX64_TOP_NIBBLE_SHIFT = 60 = (16 - 1) * 4 bits per nibble
 */
#define HEX64_DIGITS           16
#define HEX64_TOP_NIBBLE_SHIFT 60
#define BITS_PER_NIBBLE        4

void put_hex(uint64_t value) {
    serial_put_hex(value);
    if (fb_ready) {
        char buf[19];  /* "0x" + 16 hex digits + '\0' */
        buf[0] = '0';
        buf[1] = 'x';
        for (int i = 0; i < HEX64_DIGITS; i++) {
            int digit = (value >> (HEX64_TOP_NIBBLE_SHIFT - i * BITS_PER_NIBBLE)) & 0xF;
            buf[2 + i] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        }
        buf[18] = '\0';
        fb_console_puts(buf);
    }
}

void put_dec(uint64_t value) {
    serial_put_dec(value);
    if (fb_ready) {
        if (value == 0) {
            fb_console_putc('0');
            return;
        }
        char buf[21];
        int i = 20;
        buf[i] = '\0';
        while (value > 0) {
            buf[--i] = '0' + (value % 10);
            value /= 10;
        }
        fb_console_puts(&buf[i]);
    }
}
