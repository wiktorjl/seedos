/*
 * console.c - Unified Console Output (Serial + Framebuffer)
 *
 * This file implements the console abstraction layer that sends output
 * to both serial port and framebuffer simultaneously.
 *
 * Implementation Notes:
 *
 *   The serial output functions are defined in kernel.c (they're available
 *   from early boot before the framebuffer is initialized).
 *
 *   Framebuffer output is only enabled after console_init() is called,
 *   which should happen after the framebuffer is set up.
 *
 *   For hex/dec output, we duplicate the conversion logic here rather than
 *   calling the serial functions character-by-character, for efficiency.
 */

#include "console.h"
#include "fb.h"

/* =============================================================================
 * External Serial Functions (defined in kernel.c)
 *
 * These are always available and work from the earliest point in boot.
 * =============================================================================
 */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void serial_put_hex(uint64_t value);
extern void serial_put_dec(uint64_t value);

/* =============================================================================
 * Console State
 * =============================================================================
 */

/*
 * Flag indicating whether framebuffer output is ready.
 * Before console_init() is called, only serial output is used.
 */
static int framebuffer_ready = 0;

/* =============================================================================
 * Hex/Decimal Conversion Constants
 * =============================================================================
 */

/* Number of hex digits in a 64-bit value */
#define HEX64_DIGIT_COUNT 16

/* Bit shift to get the top nibble: (16 - 1) * 4 = 60 */
#define HEX64_TOP_NIBBLE_SHIFT 60

/* Bits per hex digit */
#define BITS_PER_HEX_DIGIT 4

/* Maximum decimal digits in uint64_t (18446744073709551615 = 20 digits) */
#define MAX_DECIMAL_DIGITS 20

/* Mask for extracting one hex digit */
#define HEX_DIGIT_MASK 0xF

/* Decimal base for division */
#define DECIMAL_BASE 10

/* =============================================================================
 * Console API Implementation
 * =============================================================================
 */

/*
 * console_init - Enable framebuffer output.
 */
void console_init(void) {
    framebuffer_ready = 1;
}

/*
 * puts - Output a string to both serial and framebuffer.
 */
void puts(const char *s) {
    serial_puts(s);
    if (framebuffer_ready) {
        fb_console_puts(s);
    }
}

/*
 * putc - Output a character to both serial and framebuffer.
 */
void putc(char c) {
    serial_putc(c);
    if (framebuffer_ready) {
        fb_console_putc(c);
    }
}

/*
 * put_hex - Output a 64-bit value in hexadecimal.
 *
 * Format: "0x" followed by exactly 16 hex digits (zero-padded).
 */
void put_hex(uint64_t value) {
    /* Always output to serial */
    serial_put_hex(value);

    /* Output to framebuffer if ready */
    if (framebuffer_ready) {
        /* Buffer: "0x" + 16 digits + null terminator */
        char hex_buffer[19];
        hex_buffer[0] = '0';
        hex_buffer[1] = 'x';

        /* Convert each nibble to a hex digit, from most to least significant */
        for (int i = 0; i < HEX64_DIGIT_COUNT; i++) {
            int shift = HEX64_TOP_NIBBLE_SHIFT - i * BITS_PER_HEX_DIGIT;
            int digit = (value >> shift) & HEX_DIGIT_MASK;
            hex_buffer[2 + i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        }
        hex_buffer[18] = '\0';

        fb_console_puts(hex_buffer);
    }
}

/*
 * put_dec - Output a 64-bit value in decimal.
 *
 * No leading zeros; prints "0" for zero value.
 */
void put_dec(uint64_t value) {
    /* Always output to serial */
    serial_put_dec(value);

    /* Output to framebuffer if ready */
    if (framebuffer_ready) {
        /* Special case for zero */
        if (value == 0) {
            fb_console_putc('0');
            return;
        }

        /* Buffer for decimal digits (built from right to left) */
        char dec_buffer[MAX_DECIMAL_DIGITS + 1];
        int pos = MAX_DECIMAL_DIGITS;
        dec_buffer[pos] = '\0';

        /* Extract digits from least to most significant */
        while (value > 0) {
            dec_buffer[--pos] = '0' + (value % DECIMAL_BASE);
            value /= DECIMAL_BASE;
        }

        /* Output starting from first digit */
        fb_console_puts(&dec_buffer[pos]);
    }
}
