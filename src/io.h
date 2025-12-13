/*
 * io.h - x86 I/O Port Access Helpers
 *
 * The x86 architecture has a separate 16-bit I/O address space that is
 * distinct from the memory address space. Hardware devices like the PIC,
 * keyboard controller, and serial ports are accessed through this I/O space.
 *
 * I/O Space vs Memory-Mapped I/O:
 *
 *   - I/O Space: Uses IN/OUT instructions, 64K ports (0x0000-0xFFFF)
 *   - Memory-Mapped: Uses regular MOV instructions, part of memory space
 *
 *   Legacy PC hardware uses I/O ports. Modern devices (PCIe) often use MMIO.
 *
 * Common I/O Ports:
 *
 *   0x20-0x21    Master PIC (8259A)
 *   0xA0-0xA1    Slave PIC (8259A)
 *   0x60-0x64    PS/2 Keyboard/Mouse Controller (8042)
 *   0x3F8-0x3FF  COM1 Serial Port
 *   0x80         POST Code / Delay Port
 *
 * Assembly Constraints:
 *
 *   The IN/OUT instructions have specific register requirements:
 *   - Value must be in AL (8-bit), AX (16-bit), or EAX (32-bit)
 *   - Port can be an immediate (0-255) or in DX register
 *
 *   The "Nd" constraint means: DX register or immediate value.
 *   The "a" constraint means: AL/AX/EAX register.
 */

#ifndef IO_H
#define IO_H

#include "types.h"

/* =============================================================================
 * I/O Port Access Functions
 * =============================================================================
 */

/*
 * outb - Write a byte to an I/O port.
 *
 * @port:  The 16-bit I/O port address.
 * @value: The 8-bit value to write.
 *
 * Uses the OUT instruction: OUT port, AL
 */
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile (
        "outb %0, %1"
        :                        /* no outputs */
        : "a"(value), "Nd"(port) /* value in AL, port in DX or immediate */
    );
}

/*
 * inb - Read a byte from an I/O port.
 *
 * @port: The 16-bit I/O port address.
 *
 * Returns: The 8-bit value read from the port.
 *
 * Uses the IN instruction: IN AL, port
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
 * io_wait - Small delay for slow hardware.
 *
 * Some legacy hardware (like the 8259 PIC) needs a brief pause between
 * I/O operations. Writing to port 0x80 (the POST code port) is a
 * standard way to create this delay (~1-4 microseconds).
 *
 * This is safe because port 0x80 is the diagnostic POST port used
 * during boot - writing to it has no effect after boot completes.
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* IO_H */
