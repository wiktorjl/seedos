/*
 * stdio.c - Standard Input/Output
 *
 * Implements printf/sprintf with common format specifiers.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Character I/O
 * ============================================================================ */

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
    putchar('\n');
    return 0;
}

int getchar(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        return (unsigned char)c;
    }
    return EOF;
}

/* ============================================================================
 * Printf Implementation
 * ============================================================================ */

/* Format a number into buffer, returns length */
static int format_unsigned(char *buf, unsigned long n, int base, int uppercase) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int i = 0;

    if (n == 0) {
        buf[0] = '0';
        return 1;
    }

    while (n > 0) {
        tmp[i++] = digits[n % base];
        n /= base;
    }

    int len = i;
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    return len;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    size_t pos = 0;

    /* Helper macro to add a character */
    #define PUT(c) do { \
        if (pos < size - 1) str[pos] = (c); \
        pos++; \
    } while(0)

    while (*format) {
        if (*format != '%') {
            PUT(*format++);
            continue;
        }

        format++;  /* skip '%' */

        /* Parse flags */
        int zero_pad = 0;
        int left_align = 0;

        while (*format == '0' || *format == '-') {
            if (*format == '0') zero_pad = 1;
            if (*format == '-') left_align = 1;
            format++;
        }

        /* Parse width */
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format++ - '0');
        }

        /* Parse length modifier */
        int is_long = 0;
        if (*format == 'l') {
            is_long = 1;
            format++;
            if (*format == 'l') {
                format++;  /* ll treated same as l for simplicity */
            }
        }

        /* Parse format specifier */
        char buf[24];
        int len = 0;
        int is_negative = 0;
        char pad_char = (zero_pad && !left_align) ? '0' : ' ';

        switch (*format++) {
            case 'd':
            case 'i': {
                long val = is_long ? va_arg(ap, long) : va_arg(ap, int);
                if (val < 0) {
                    is_negative = 1;
                    val = -val;
                }
                len = format_unsigned(buf, (unsigned long)val, 10, 0);
                break;
            }

            case 'u': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                           : va_arg(ap, unsigned int);
                len = format_unsigned(buf, val, 10, 0);
                break;
            }

            case 'x': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                           : va_arg(ap, unsigned int);
                len = format_unsigned(buf, val, 16, 0);
                break;
            }

            case 'X': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                           : va_arg(ap, unsigned int);
                len = format_unsigned(buf, val, 16, 1);
                break;
            }

            case 'p': {
                unsigned long val = (unsigned long)va_arg(ap, void *);
                PUT('0');
                PUT('x');
                width = width > 2 ? width - 2 : 0;
                len = format_unsigned(buf, val, 16, 0);
                break;
            }

            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t slen = strlen(s);

                /* Right padding for left align */
                if (!left_align) {
                    while (width > (int)slen) {
                        PUT(' ');
                        width--;
                    }
                }

                while (*s) {
                    PUT(*s++);
                }

                /* Left padding for right align */
                if (left_align) {
                    while (width > (int)slen) {
                        PUT(' ');
                        width--;
                    }
                }
                continue;
            }

            case 'c': {
                char c = (char)va_arg(ap, int);
                PUT(c);
                continue;
            }

            case '%':
                PUT('%');
                continue;

            default:
                /* Unknown specifier - print as-is */
                PUT('%');
                PUT(*(format - 1));
                continue;
        }

        /* Calculate padding needed */
        int total_len = len + (is_negative ? 1 : 0);
        int padding = (width > total_len) ? width - total_len : 0;

        /* Left-aligned: content first, then padding */
        if (left_align) {
            if (is_negative) PUT('-');
            for (int i = 0; i < len; i++) PUT(buf[i]);
            while (padding--) PUT(' ');
        } else {
            /* Right-aligned: padding, then content */
            if (pad_char == '0' && is_negative) {
                PUT('-');
                is_negative = 0;
            }
            while (padding--) PUT(pad_char);
            if (is_negative) PUT('-');
            for (int i = 0; i < len; i++) PUT(buf[i]);
        }
    }

    /* Null-terminate */
    if (size > 0) {
        str[pos < size ? pos : size - 1] = '\0';
    }

    return (int)pos;

    #undef PUT
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, (size_t)-1, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *format, va_list ap) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    int to_write = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    write(STDOUT_FILENO, buf, to_write);
    return len;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}
