#include "serial.h"

/*
 * TODO: Add spinlock/mutex protection for thread safety when kernel
 * supports multiple threads/cores. Currently not thread-safe.
 */

#define COM1 0x3F8

// x86 port I/O (inline assembly)
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);    // Disable interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x01);    // Divisor low byte: 115200 baud
    outb(COM1 + 1, 0x00);    // Divisor high byte
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, 1 stop bit
    outb(COM1 + 2, 0xC7);    // Enable FIFO, clear, 14-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static int serial_tx_ready(void) {
    return inb(COM1 + 5) & 0x20;  // Check LSR bit 5 (THR empty)
}

void serial_putchar(char c) {
    while (!serial_tx_ready());   // Wait for transmit buffer
    outb(COM1, c);
}

void serial_puts(const char *str) {
    while (*str) {
        if (*str == '\n') serial_putchar('\r');  // CR+LF for terminals
        serial_putchar(*str++);
    }
}