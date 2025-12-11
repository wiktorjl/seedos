/*
 * io.h - x86 I/O port access helpers
 *
 * x86 has a separate I/O address space (not memory-mapped).
 * The 'out' and 'in' instructions access it.
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

/*
 * Write a byte to an I/O port.
 */
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile (
        "outb %0, %1"
        :                        /* no outputs */
        : "a"(value), "Nd"(port) /* value in AL, port in DX or immediate */
    );
}

/*
 * Read a byte from an I/O port.
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile (
        "inb %1, %0"
        : "=a"(value)   /* output in AL */
        : "Nd"(port)    /* port in DX or immediate */
    );
    return value;
}

/*
 * Small delay for slow hardware (e.g., PIC).
 * Port 0x80 is used for POST codes, safe for delay.
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* IO_H */
