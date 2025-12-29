/*
 * kprintf.h - Kernel Printf
 *
 * Formatted output for the kernel. Supports standard printf format specifiers
 * including %d, %u, %x, %s, %c, %p, and width/precision modifiers.
 */

#ifndef KPRINTF_H
#define KPRINTF_H

#include <stdarg.h>
#include "terminal.h"

/*
 * kprintf - Print formatted output to the active terminal.
 *
 * @fmt: Format string (printf-style).
 * @...: Variable arguments.
 *
 * Returns: Number of characters written.
 */
int kprintf(const char *fmt, ...);

/*
 * tkprintf - Print formatted output to a specific terminal.
 *
 * @term: Terminal to print to.
 * @fmt:  Format string.
 * @...:  Variable arguments.
 *
 * Returns: Number of characters written.
 */
int tkprintf(terminal_t *term, const char *fmt, ...);

/*
 * kvprintf - Print formatted output using va_list.
 *
 * @fmt:  Format string.
 * @args: Variable argument list.
 *
 * Returns: Number of characters written.
 */
int kvprintf(const char *fmt, va_list args);

/*
 * tkvprintf - Print to specific terminal using va_list.
 *
 * @term: Terminal to print to.
 * @fmt:  Format string.
 * @args: Variable argument list.
 *
 * Returns: Number of characters written.
 */
int tkvprintf(terminal_t *term, const char *fmt, va_list args);

/*
 * kprintf_log - Print a log message with level filtering.
 *
 * @level:  Log level (LOG_PANIC, LOG_ERROR, LOG_WARN, etc.).
 * @prefix: Prefix string to prepend (e.g., "[  info ] ").
 * @color:  Text color (32-bit RGB).
 * @fmt:    Format string.
 * @...:    Variable arguments.
 *
 * Returns: Number of characters written, or 0 if filtered.
 *
 * Messages below CONFIG_LOG_LEVEL are suppressed.
 */
int kprintf_log(int level, const char *prefix, uint32_t color, const char *fmt, ...);

#endif /* KPRINTF_H */
