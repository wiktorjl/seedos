// Kernel printf: formatted output implementation

#include "kprintf.h"
#include "config.h"
#include "terminal.h"
#include <stdint.h>
#include <stddef.h>

// Helper: print a single character
static void kputc(terminal_t *term, char c, int *count) {
    terminal_putchar(term, c);
    (*count)++;
}

// Helper: print a string
static void kputs(terminal_t *term, const char *s, int *count) {
    if (!s) s = "(null)";
    while (*s) {
        kputc(term, *s++, count);
    }
}

// Helper: print unsigned integer in given base
static void print_uint(terminal_t *term, uint64_t value, int base, int uppercase, int *count) {
    char buf[24];  // Enough for 64-bit in binary
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (value == 0) {
        kputc(term, '0', count);
        return;
    }

    while (value > 0) {
        buf[i++] = digits[value % base];
        value /= base;
    }

    while (i > 0) {
        kputc(term, buf[--i], count);
    }
}

// Helper: print signed integer
static void print_int(terminal_t *term, int64_t value, int *count) {
    if (value < 0) {
        kputc(term, '-', count);
        value = -value;
    }
    print_uint(term, (uint64_t)value, 10, 0, count);
}

// Core vprintf implementation
int tkvprintf(terminal_t *term, const char *fmt, va_list args) {
    int count = 0;

    if (!term) term = terminal_get_active();

    while (*fmt) {
        if (*fmt != '%') {
            kputc(term, *fmt++, &count);
            continue;
        }

        fmt++;  // Skip '%'

        // Handle flags (simplified)
        int zero_pad = 0;
        int width = 0;
        int is_long = 0;

        // Check for '0' padding flag
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }

        // Parse width
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        // Check for 'l' or 'll' length modifier
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long = 2;
                fmt++;
            }
        }

        // Handle format specifier
        switch (*fmt) {
            case 'c':
                kputc(term, (char)va_arg(args, int), &count);
                break;

            case 's':
                kputs(term, va_arg(args, const char *), &count);
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
                print_int(term, val, &count);
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
                print_uint(term, val, 10, 0, &count);
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
                print_uint(term, val, 16, *fmt == 'X', &count);
                break;
            }

            case 'p': {
                void *ptr = va_arg(args, void *);
                kputs(term, "0x", &count);
                print_uint(term, (uint64_t)(uintptr_t)ptr, 16, 0, &count);
                break;
            }

            case '%':
                kputc(term, '%', &count);
                break;

            case '\0':
                // Premature end of format string
                return count;

            default:
                // Unknown specifier, print as-is
                kputc(term, '%', &count);
                kputc(term, *fmt, &count);
                break;
        }

        fmt++;
    }

    return count;
}

int kvprintf(const char *fmt, va_list args) {
    return tkvprintf(terminal_get_active(), fmt, args);
}

int tkprintf(terminal_t *term, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = tkvprintf(term, fmt, args);
    va_end(args);
    return ret;
}

int kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = tkvprintf(terminal_get_active(), fmt, args);
    va_end(args);
    return ret;
}

int kprintf_log(int level, const char *prefix, uint32_t color, const char *fmt, ...) {
    if (level > CONFIG_LOG_LEVEL)
        return 0;

    // Use active terminal but override color
    terminal_t term = *terminal_get_active();
    term.color = color;

    terminal_puts(&term, prefix);

    va_list args;
    va_start(args, fmt);
    int count = tkvprintf(&term, fmt, args);
    va_end(args);

    terminal_putchar(&term, '\n');

    return count;
}
