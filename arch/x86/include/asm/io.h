/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * x86 port I/O primitives
 */

#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#include "types.h"

/**
 * outb - Write byte to I/O port
 * @port: I/O port address
 * @val: byte value to write
 */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * inb - Read byte from I/O port
 * @port: I/O port address
 *
 * Return: byte value read from the port
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#endif /* _ASM_X86_IO_H */
