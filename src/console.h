/*
 * console.h - Unified Console Output (Serial + Framebuffer)
 *
 * This module provides unified output functions that write to both
 * the serial port (for debugging in a terminal) and the framebuffer
 * (for display on the screen).
 *
 * Why Two Outputs?
 *
 *   - Serial: Works from the very start of boot, easy to log, copy, search
 *   - Framebuffer: Visible on the screen, no external terminal needed
 *
 *   During development, serial output is invaluable for debugging.
 *   Run QEMU with `-serial stdio` to see serial output in your terminal.
 *
 * Output Flow:
 *
 *   ┌────────────────────────────────────────────────────────────┐
 *   │  puts("Hello")                                             │
 *   └────────────────────────────────────────────────────────────┘
 *                                │
 *               ┌────────────────┴────────────────┐
 *               ▼                                 ▼
 *   ┌───────────────────────┐       ┌───────────────────────┐
 *   │  serial_puts()        │       │  fb_console_puts()    │
 *   │  → COM1 port 0x3F8    │       │  → Framebuffer pixels │
 *   └───────────────────────┘       └───────────────────────┘
 *               │                                 │
 *               ▼                                 ▼
 *   ┌───────────────────────┐       ┌───────────────────────┐
 *   │  Terminal (host)      │       │  Screen (QEMU window) │
 *   └───────────────────────┘       └───────────────────────┘
 *
 * Usage:
 *
 *   All kernel code should use these functions for output:
 *     puts("message")  - Print a string
 *     putc('c')        - Print a single character
 *     put_hex(0x1234)  - Print a number in hex (with "0x" prefix)
 *     put_dec(42)      - Print a number in decimal
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"

/* =============================================================================
 * Console API Functions
 * =============================================================================
 */

/*
 * console_init - Mark the framebuffer as ready for output.
 *
 * Must be called after fb_init() and fb_console_init().
 * Before this is called, output only goes to serial port.
 */
void console_init(void);

/*
 * puts - Print a null-terminated string.
 *
 * @s: String to print.
 *
 * Outputs to both serial and framebuffer (if ready).
 */
void puts(const char *s);

/*
 * putc - Print a single character.
 *
 * @c: Character to print.
 *
 * Outputs to both serial and framebuffer (if ready).
 */
void putc(char c);

/*
 * put_hex - Print a 64-bit value in hexadecimal.
 *
 * @value: The value to print.
 *
 * Prints with "0x" prefix and all 16 hex digits (zero-padded).
 * Example: put_hex(255) prints "0x00000000000000ff"
 */
void put_hex(uint64_t value);

/*
 * put_dec - Print a 64-bit value in decimal.
 *
 * @value: The value to print.
 *
 * Prints without leading zeros.
 * Example: put_dec(42) prints "42"
 */
void put_dec(uint64_t value);

#endif /* CONSOLE_H */
