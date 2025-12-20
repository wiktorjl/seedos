/*
 * serial.h - Serial Port Driver (UART 16550)
 *
 * This module provides serial port output for kernel debugging.
 * Serial output is essential during early boot before the framebuffer
 * is initialized, and remains useful for logging throughout operation.
 *
 * Hardware Overview:
 *
 *   The UART 16550 is the standard PC serial controller. It connects
 *   to COM1 at I/O port 0x3F8. We use polling mode (no interrupts)
 *   for simplicity and reliability.
 *
 *   UART 16550 registers (at base address 0x3F8 for COM1):
 *     +0: Data register (read/write data, or divisor low when DLAB=1)
 *     +1: Interrupt enable (or divisor high when DLAB=1)
 *     +2: FIFO control (write) / Interrupt ID (read)
 *     +3: Line control (data bits, parity, stop bits, DLAB)
 *     +4: Modem control (RTS, DTR signals)
 *     +5: Line status (transmit ready, receive ready, errors)
 *
 * Configuration:
 *
 *   We configure the UART for 38400 baud, 8N1:
 *     - 38400 baud (bits per second)
 *     - 8 data bits per character
 *     - No parity bit
 *     - 1 stop bit
 *
 * Usage with QEMU:
 *
 *   Run QEMU with: -serial stdio
 *   Serial output appears in your terminal alongside QEMU.
 */

#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

/* =============================================================================
 * Serial Port API Functions
 * =============================================================================
 */

/*
 * serial_init - Initialize the serial port for 38400 baud, 8N1.
 *
 * Must be called before any other serial functions.
 * Safe to call very early in boot (no dependencies).
 */
void serial_init(void);

/*
 * serial_putc - Write a single character to the serial port.
 *
 * @c: Character to write.
 *
 * Blocks until the transmitter is ready (polling mode).
 */
void serial_putc(char c);

/*
 * serial_puts - Write a null-terminated string to the serial port.
 *
 * @s: String to write.
 *
 * Automatically converts '\n' to '\r\n' for proper terminal display.
 */
void serial_puts(const char *s);

/*
 * serial_put_hex - Print a 64-bit value in hexadecimal.
 *
 * @value: Value to print.
 *
 * Prints with "0x" prefix and all 16 hex digits (zero-padded).
 * Example: serial_put_hex(255) prints "0x00000000000000ff"
 */
void serial_put_hex(uint64_t value);

/*
 * serial_put_dec - Print a 64-bit value in decimal.
 *
 * @value: Value to print.
 *
 * Prints without leading zeros.
 * Example: serial_put_dec(42) prints "42"
 */
void serial_put_dec(uint64_t value);

/* =============================================================================
 * Serial Port Input Functions
 *
 * These functions enable reading from the serial port, which is useful for
 * automated testing with QEMU (input can be piped via -serial stdio).
 * =============================================================================
 */

/*
 * serial_has_char - Check if a character is available to read.
 *
 * Returns non-zero if data is available in the receive buffer.
 */
int serial_has_char(void);

/*
 * serial_getc - Read a single character from the serial port.
 *
 * Blocks until a character is available (polling mode).
 * Returns the received character.
 */
char serial_getc(void);

/*
 * serial_read - Read up to len characters from the serial port.
 *
 * @buf: Buffer to store characters.
 * @len: Maximum number of characters to read.
 *
 * Non-blocking: returns immediately with available characters.
 * Returns the number of characters actually read.
 */
size_t serial_read(char *buf, size_t len);

/*
 * serial_wait - Block until serial input is available.
 *
 * Uses HLT to reduce CPU usage while waiting.
 */
void serial_wait(void);

#endif /* SERIAL_H */
