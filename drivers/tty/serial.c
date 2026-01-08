// SPDX-License-Identifier: GPL-2.0-only
/*
 * Serial Port Driver (COM1)
 *
 * Output to serial port for debugging and logging.
 *
 * TODO: Add spinlock protection for thread safety.
 */

#include "serial.h"
#include "io.h"

#define COM1 0x3F8

static int serial_tx_ready(void)
{
	return inb(COM1 + 5) & 0x20;
}

/**
 * serial_init - Initialize COM1 serial port at 115200 8N1
 */
void serial_init(void)
{
	outb(COM1 + 1, 0x00);	/* Disable interrupts */
	outb(COM1 + 3, 0x80);	/* Enable DLAB */
	outb(COM1 + 0, 0x01);	/* Divisor low: 115200 baud */
	outb(COM1 + 1, 0x00);	/* Divisor high */
	outb(COM1 + 3, 0x03);	/* 8N1 */
	outb(COM1 + 2, 0xC7);	/* Enable FIFO, 14-byte threshold */
	outb(COM1 + 4, 0x0B);	/* IRQs enabled, RTS/DSR set */
}

/**
 * serial_putchar - Write a character to the serial port
 * @c: character to write
 */
void serial_putchar(char c)
{
	while (!serial_tx_ready())
		;
	outb(COM1, c);
}

/**
 * serial_puts - Write a string to the serial port
 * @str: null-terminated string
 *
 * Converts LF to CRLF automatically.
 */
void serial_puts(const char *str)
{
	while (*str) {
		if (*str == '\n')
			serial_putchar('\r');
		serial_putchar(*str++);
	}
}
