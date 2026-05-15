/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Serial Port Driver Interface
 */

#ifndef _SERIAL_H
#define _SERIAL_H

#include "types.h"

/**
 * serial_init - Initialize COM1 serial port at 115200 8N1
 *
 * Brings the UART up for output only. Safe to call before the IDT
 * and IOAPIC are ready (used for early-boot logging).
 */
void serial_init(void);

/**
 * serial_irq_init - Enable receive-side serial input
 *
 * Registers the COM1 IRQ handler, routes IRQ 4 through the I/O APIC,
 * and enables the UART's receive-data-available interrupt. Incoming
 * bytes are translated (CR -> LF, DEL -> BS, ANSI escapes filtered)
 * and injected into the keyboard input ring so kshell reads them.
 *
 * Must be called after ioapic_init() and keyboard_init(), and before
 * cpu_enable_interrupts().
 */
void serial_irq_init(void);

/**
 * serial_putchar - Write a character to the serial port
 * @c: character to write
 */
void serial_putchar(char c);

/**
 * serial_puts - Write a string to the serial port
 * @str: null-terminated string
 *
 * Converts LF to CRLF automatically.
 */
void serial_puts(const char *str);

#endif /* _SERIAL_H */
