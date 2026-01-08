/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Serial Port Driver Interface
 */

#ifndef _SERIAL_H
#define _SERIAL_H

#include "types.h"

/**
 * serial_init - Initialize COM1 serial port at 115200 8N1
 */
void serial_init(void);

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
