/*
 * kprintf.h - Kernel Printf
 *
 * Formatted output for the kernel.
 */

#ifndef KPRINTF_H
#define KPRINTF_H

#include <stdarg.h>
#include "terminal.h"

/* Printf to active terminal */
int kprintf(const char *fmt, ...);

/* Printf to specific terminal */
int tkprintf(terminal_t *term, const char *fmt, ...);

/* Vprintf variants (for building on top of kprintf) */
int kvprintf(const char *fmt, va_list args);
int tkvprintf(terminal_t *term, const char *fmt, va_list args);

/* Logging with level, prefix, and color (used by log.h macros) */
int kprintf_log(int level, const char *prefix, uint32_t color, const char *fmt, ...);

#endif /* KPRINTF_H */
