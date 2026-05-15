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
#include "keyboard.h"
#include "ioapic.h"
#include "apic.h"
#include "idt.h"
#include "log.h"

#define COM1 0x3F8

/* COM1 lives on ISA IRQ 4; vector 32 = APIC timer, 33 = keyboard. */
#define IRQ_COM1 34

/* UART register offsets we use here. */
#define UART_RBR 0       /* DLAB=0: receive buffer */
#define UART_IER 1       /* DLAB=0: interrupt enable */
#define UART_LSR 5       /* line status */
#define UART_LSR_DR 0x01 /* data ready */

#define UART_IER_RDA 0x01 /* receive-data-available interrupt enable */

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

/* CSI state for swallowing ANSI escape sequences (e.g. arrow keys).
 * 0 = normal, 1 = saw ESC, 2 = saw ESC '[' (consume until final byte). */
static int csi_state;

static void serial_dispatch_byte(uint8_t byte)
{
	switch (csi_state) {
	case 1:
		csi_state = (byte == '[') ? 2 : 0;
		return;
	case 2:
		/* CSI final byte is in 0x40..0x7E; intermediates/params precede it. */
		if (byte >= 0x40 && byte <= 0x7E)
			csi_state = 0;
		return;
	}

	if (byte == 0x1B) {
		csi_state = 1;
		return;
	}
	if (byte == '\r')
		byte = '\n';      /* terminals send CR for Enter, kshell wants LF */
	else if (byte == 0x7F)
		byte = '\b';      /* terminals send DEL for Backspace */

	keyboard_inject((char)byte);
}

static void serial_irq_handler(interrupt_frame_t *frame)
{
	(void)frame;

	while (inb(COM1 + UART_LSR) & UART_LSR_DR)
		serial_dispatch_byte(inb(COM1 + UART_RBR));

	apic_eoi();
}

void serial_irq_init(void)
{
	idt_register_irq(IRQ_COM1, serial_irq_handler);
	ioapic_route_irq(ISA_IRQ_COM1, IRQ_COM1, 0);
	outb(COM1 + UART_IER, UART_IER_RDA);
	log_info("SERIAL: COM1 RX on vector %d", IRQ_COM1);
}
