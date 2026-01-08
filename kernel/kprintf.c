// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel printf implementation
 *
 * Formatted output to terminals with support for standard printf specifiers.
 */

#include "kprintf.h"
#include "config.h"
#include "terminal.h"
#include "types.h"
#include <stddef.h>

static void kputc(terminal_t *term, char c, int *count)
{
	terminal_putchar(term, c);
	(*count)++;
}

static void kputs_padded(terminal_t *term, const char *s, int width,
			 int left_align, int *count)
{
	int len = 0;
	const char *p;

	if (!s)
		s = "(null)";

	p = s;
	while (*p++)
		len++;

	if (!left_align && width > len) {
		for (int i = 0; i < width - len; i++)
			kputc(term, ' ', count);
	}

	while (*s)
		kputc(term, *s++, count);

	if (left_align && width > len) {
		for (int i = 0; i < width - len; i++)
			kputc(term, ' ', count);
	}
}

static void kputs(terminal_t *term, const char *s, int *count)
{
	kputs_padded(term, s, 0, 0, count);
}

static void print_uint_padded(terminal_t *term, uint64_t value, int base,
			      int uppercase, int width, int zero_pad,
			      int left_align, int *count)
{
	char buf[65];
	const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
	int i = 0;
	int num_len, padding;

	if (value == 0) {
		buf[i++] = '0';
	} else {
		while (value > 0) {
			buf[i++] = digits[value % base];
			value /= base;
		}
	}

	num_len = i;
	padding = (width > num_len) ? width - num_len : 0;

	if (!left_align && padding > 0) {
		char pad_char = zero_pad ? '0' : ' ';
		for (int p = 0; p < padding; p++)
			kputc(term, pad_char, count);
	}

	while (i > 0)
		kputc(term, buf[--i], count);

	if (left_align && padding > 0) {
		for (int p = 0; p < padding; p++)
			kputc(term, ' ', count);
	}
}

static void print_int_padded(terminal_t *term, int64_t value, int width,
			     int zero_pad, int left_align, int *count)
{
	int negative = 0;
	uint64_t uval;
	char buf[21];
	int i = 0;
	uint64_t tmp;
	int num_len, padding;

	if (value < 0) {
		negative = 1;
		uval = (uint64_t)(-value);
	} else {
		uval = (uint64_t)value;
	}

	tmp = uval;
	if (tmp == 0) {
		buf[i++] = '0';
	} else {
		while (tmp > 0) {
			buf[i++] = '0' + (tmp % 10);
			tmp /= 10;
		}
	}

	num_len = i + (negative ? 1 : 0);
	padding = (width > num_len) ? width - num_len : 0;

	if (!left_align && padding > 0) {
		if (zero_pad && negative) {
			kputc(term, '-', count);
			negative = 0;
		}
		char pad_char = zero_pad ? '0' : ' ';
		for (int p = 0; p < padding; p++)
			kputc(term, pad_char, count);
	}

	if (negative)
		kputc(term, '-', count);

	while (i > 0)
		kputc(term, buf[--i], count);

	if (left_align && padding > 0) {
		for (int p = 0; p < padding; p++)
			kputc(term, ' ', count);
	}
}

/**
 * tkvprintf - Formatted output to a specific terminal
 * @term: target terminal
 * @fmt: format string
 * @args: variable argument list
 *
 * Return: number of characters written
 */
int tkvprintf(terminal_t *term, const char *fmt, va_list args)
{
	int count = 0;

	if (!term)
		term = terminal_get_active();

	while (*fmt) {
		if (*fmt != '%') {
			kputc(term, *fmt++, &count);
			continue;
		}

		fmt++;

		int zero_pad = 0;
		int left_align = 0;
		int width = 0;
		int is_long = 0;

		while (*fmt == '-' || *fmt == '0') {
			if (*fmt == '-')
				left_align = 1;
			else if (*fmt == '0')
				zero_pad = 1;
			fmt++;
		}

		if (left_align)
			zero_pad = 0;

		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}

		if (*fmt == 'l') {
			is_long = 1;
			fmt++;
			if (*fmt == 'l') {
				is_long = 2;
				fmt++;
			}
		}

		switch (*fmt) {
		case 'c':
			kputc(term, (char)va_arg(args, int), &count);
			break;

		case 's':
			kputs_padded(term, va_arg(args, const char *),
				     width, left_align, &count);
			break;

		case 'd':
		case 'i': {
			int64_t val;
			if (is_long == 2)
				val = va_arg(args, int64_t);
			else if (is_long == 1)
				val = va_arg(args, long);
			else
				val = va_arg(args, int);
			print_int_padded(term, val, width, zero_pad,
					 left_align, &count);
			break;
		}

		case 'u': {
			uint64_t val;
			if (is_long == 2)
				val = va_arg(args, uint64_t);
			else if (is_long == 1)
				val = va_arg(args, unsigned long);
			else
				val = va_arg(args, unsigned int);
			print_uint_padded(term, val, 10, 0, width, zero_pad,
					  left_align, &count);
			break;
		}

		case 'x':
		case 'X': {
			uint64_t val;
			if (is_long == 2)
				val = va_arg(args, uint64_t);
			else if (is_long == 1)
				val = va_arg(args, unsigned long);
			else
				val = va_arg(args, unsigned int);
			print_uint_padded(term, val, 16, *fmt == 'X', width,
					  zero_pad, left_align, &count);
			break;
		}

		case 'o': {
			uint64_t val;
			if (is_long == 2)
				val = va_arg(args, uint64_t);
			else if (is_long == 1)
				val = va_arg(args, unsigned long);
			else
				val = va_arg(args, unsigned int);
			print_uint_padded(term, val, 8, 0, width, zero_pad,
					  left_align, &count);
			break;
		}

		case 'b': {
			uint64_t val;
			if (is_long == 2)
				val = va_arg(args, uint64_t);
			else if (is_long == 1)
				val = va_arg(args, unsigned long);
			else
				val = va_arg(args, unsigned int);
			print_uint_padded(term, val, 2, 0, width, zero_pad,
					  left_align, &count);
			break;
		}

		case 'p': {
			void *ptr = va_arg(args, void *);
			kputs(term, "0x", &count);
			print_uint_padded(term, (uint64_t)(uintptr_t)ptr, 16, 0,
					  0, 0, 0, &count);
			break;
		}

		case '%':
			kputc(term, '%', &count);
			break;

		case '\0':
			return count;

		default:
			kputc(term, '%', &count);
			kputc(term, *fmt, &count);
			break;
		}

		fmt++;
	}

	return count;
}

/**
 * kvprintf - Formatted output to active terminal (va_list version)
 * @fmt: format string
 * @args: variable argument list
 *
 * Return: number of characters written
 */
int kvprintf(const char *fmt, va_list args)
{
	return tkvprintf(terminal_get_active(), fmt, args);
}

/**
 * tkprintf - Formatted output to a specific terminal
 * @term: target terminal
 * @fmt: format string
 *
 * Return: number of characters written
 */
int tkprintf(terminal_t *term, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = tkvprintf(term, fmt, args);
	va_end(args);
	return ret;
}

/**
 * kprintf - Formatted output to active terminal
 * @fmt: format string
 *
 * Return: number of characters written
 */
int kprintf(const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = tkvprintf(terminal_get_active(), fmt, args);
	va_end(args);
	return ret;
}

/**
 * kprintf_log - Log message with level filtering and color
 * @level: log level (compared against CONFIG_LOG_LEVEL)
 * @prefix: string prefix (e.g., "[INFO] ")
 * @color: text color
 * @fmt: format string
 *
 * Return: number of characters written, or 0 if filtered
 */
int kprintf_log(int level, const char *prefix, uint32_t color,
		const char *fmt, ...)
{
	terminal_t term;
	va_list args;
	int count;

	if (level > CONFIG_LOG_LEVEL)
		return 0;

	term = *terminal_get_active();
	term.color = color;

	terminal_puts(&term, prefix);

	va_start(args, fmt);
	count = tkvprintf(&term, fmt, args);
	va_end(args);

	terminal_putchar(&term, '\n');

	return count;
}
