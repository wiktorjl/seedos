// Kernel printf: formatted output implementation

#include "kprintf.h"
#include "config.h"
#include "terminal.h"
#include "types.h"
#include <stddef.h>

// Helper: print a single character
static void kputc(terminal_t *term, char c, int *count) {
    terminal_putchar(term, c);
    (*count)++;
}

// Helper: print a string with optional width and alignment
static void kputs_padded(terminal_t *term, const char *s, int width, int left_align, int *count) {
    if (!s) s = "(null)";

    // Calculate string length
    int len = 0;
    const char *p = s;
    while (*p++) len++;

    // Right-align: print padding first
    if (!left_align && width > len) {
        for (int i = 0; i < width - len; i++) {
            kputc(term, ' ', count);
        }
    }

    // Print the string
    while (*s) {
        kputc(term, *s++, count);
    }

    // Left-align: print padding after
    if (left_align && width > len) {
        for (int i = 0; i < width - len; i++) {
            kputc(term, ' ', count);
        }
    }
}

// Helper: print a string (simple version for compatibility)
static void kputs(terminal_t *term, const char *s, int *count) {
    kputs_padded(term, s, 0, 0, count);
}

// Helper: print unsigned integer in given base with optional width/padding/alignment
static void print_uint_padded(terminal_t *term, uint64_t value, int base, int uppercase,
                               int width, int zero_pad, int left_align, int *count) {
    char buf[65];  // Enough for 64-bit in binary
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            buf[i++] = digits[value % base];
            value /= base;
        }
    }

    int num_len = i;
    int padding = (width > num_len) ? width - num_len : 0;

    // Right-align: print padding first
    if (!left_align && padding > 0) {
        char pad_char = zero_pad ? '0' : ' ';
        for (int p = 0; p < padding; p++) {
            kputc(term, pad_char, count);
        }
    }

    // Print digits in reverse order
    while (i > 0) {
        kputc(term, buf[--i], count);
    }

    // Left-align: print padding after
    if (left_align && padding > 0) {
        for (int p = 0; p < padding; p++) {
            kputc(term, ' ', count);
        }
    }
}

// Helper: print signed integer with optional width/padding/alignment
static void print_int_padded(terminal_t *term, int64_t value, int width, int zero_pad, int left_align, int *count) {
    int negative = 0;
    uint64_t uval;

    if (value < 0) {
        negative = 1;
        uval = (uint64_t)(-value);
    } else {
        uval = (uint64_t)value;
    }

    // Calculate number of digits
    char buf[21];
    int i = 0;
    uint64_t tmp = uval;
    if (tmp == 0) {
        buf[i++] = '0';
    } else {
        while (tmp > 0) {
            buf[i++] = '0' + (tmp % 10);
            tmp /= 10;
        }
    }

    int num_len = i + (negative ? 1 : 0);
    int padding = (width > num_len) ? width - num_len : 0;

    // Right-align: print padding first (before sign for space, after for zero)
    if (!left_align && padding > 0) {
        if (zero_pad && negative) {
            kputc(term, '-', count);
            negative = 0;  // Already printed
        }
        char pad_char = zero_pad ? '0' : ' ';
        for (int p = 0; p < padding; p++) {
            kputc(term, pad_char, count);
        }
    }

    // Print sign
    if (negative) {
        kputc(term, '-', count);
    }

    // Print digits in reverse order
    while (i > 0) {
        kputc(term, buf[--i], count);
    }

    // Left-align: print padding after
    if (left_align && padding > 0) {
        for (int p = 0; p < padding; p++) {
            kputc(term, ' ', count);
        }
    }
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

        // Handle flags
        int zero_pad = 0;
        int left_align = 0;
        int width = 0;
        int is_long = 0;

        // Parse flags (can appear in any order)
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') {
                left_align = 1;
            } else if (*fmt == '0') {
                zero_pad = 1;
            }
            fmt++;
        }

        // Left-align overrides zero-padding (standard printf behavior)
        if (left_align) {
            zero_pad = 0;
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
                kputs_padded(term, va_arg(args, const char *), width, left_align, &count);
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
                print_int_padded(term, val, width, zero_pad, left_align, &count);
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
                print_uint_padded(term, val, 10, 0, width, zero_pad, left_align, &count);
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
                print_uint_padded(term, val, 16, *fmt == 'X', width, zero_pad, left_align, &count);
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
                print_uint_padded(term, val, 8, 0, width, zero_pad, left_align, &count);
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
                print_uint_padded(term, val, 2, 0, width, zero_pad, left_align, &count);
                break;
            }

            case 'p': {
                void *ptr = va_arg(args, void *);
                kputs(term, "0x", &count);
                print_uint_padded(term, (uint64_t)(uintptr_t)ptr, 16, 0, 0, 0, 0, &count);
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
