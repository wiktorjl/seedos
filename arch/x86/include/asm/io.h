/*
 * io.h - x86 Port I/O Functions
 *
 * Provides inline functions for reading and writing to I/O ports.
 * Used by drivers that communicate with hardware via port-mapped I/O.
 */

#ifndef IO_H
#define IO_H

#include "types.h"

/* =============================================================================
 * Port Output Functions
 * =============================================================================
 */

/*
 * outb - Write a byte to an I/O port.
 *
 * @port: The I/O port address.
 * @val:  The byte value to write.
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* =============================================================================
 * Port Input Functions
 * =============================================================================
 */

/*
 * inb - Read a byte from an I/O port.
 *
 * @port: The I/O port address.
 *
 * Returns: The byte value read from the port.
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#endif /* IO_H */
