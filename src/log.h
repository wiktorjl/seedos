/*
 * log.h - Logging Interface
 *
 * Provides level filtering and format string support for kernel logging.
 */

#ifndef LOG_H
#define LOG_H

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

#endif /* LOG_H */
