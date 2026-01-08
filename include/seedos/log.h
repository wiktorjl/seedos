/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Logging interface
 *
 * Leveled logging macros filtered by CONFIG_LOG_LEVEL.
 *
 * Log Levels (in order of severity):
 *   LOG_PANIC - System is unusable, will halt
 *   LOG_ERROR - Error conditions
 *   LOG_WARN  - Warning conditions
 *   LOG_INFO  - Informational messages
 *   LOG_DEBUG - Debug-level messages
 *   LOG_TRACE - Fine-grained tracing
 *
 * Usage:
 *   log_info("Initialized %s with %d pages", name, count);
 *   log_error("Failed to allocate memory");
 */

#ifndef _LOG_H
#define _LOG_H

#include "config.h"
#include "kprintf.h"

#define log_panic(fmt, ...) \
    kprintf_log(LOG_PANIC, "[ panic ] ", CONFIG_CONSOLE_COLOR_PANIC, fmt, ##__VA_ARGS__)

#define log_error(fmt, ...) \
    kprintf_log(LOG_ERROR, "[ error ] ", CONFIG_CONSOLE_COLOR_ERROR, fmt, ##__VA_ARGS__)

#define log_warn(fmt, ...) \
    kprintf_log(LOG_WARN,  "[  warn ] ", CONFIG_CONSOLE_COLOR_WARN, fmt, ##__VA_ARGS__)

#define log_info(fmt, ...) \
    kprintf_log(LOG_INFO,  "[  info ] ", CONFIG_CONSOLE_COLOR_INFO, fmt, ##__VA_ARGS__)

#define log_debug(fmt, ...) \
    kprintf_log(LOG_DEBUG, "[ debug ] ", CONFIG_CONSOLE_COLOR_DEBUG, fmt, ##__VA_ARGS__)

#define log_trace(fmt, ...) \
    kprintf_log(LOG_TRACE, "[ trace ] ", CONFIG_CONSOLE_COLOR_TRACE, fmt, ##__VA_ARGS__)

#endif /* _LOG_H */
