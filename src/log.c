// Logging with level filtering and multi-backend output

#include "log.h"
#include "config.h"
#include "console.h"

void log_init(void) {
    // Future: initialize log buffers, serial, etc.
}

static void log_console(int level, const char *prefix, const char *msg, uint32_t color) {
#if CONFIG_OUTPUT_CONSOLE
    if (level > CONFIG_LOG_LEVEL_CONSOLE) return;
    console_puts(prefix, color);
    console_puts(msg, color);
    console_putchar('\n', color);
#endif
}

static void log_serial(int level, const char *prefix, const char *msg) {
#if CONFIG_OUTPUT_SERIAL
    if (level > CONFIG_LOG_LEVEL_SERIAL) return;
    // serial_puts(prefix);
    // serial_puts(msg);
    // serial_putchar('\n');
#endif
}

static void log_msg(int level, const char *prefix, const char *msg, uint32_t color) {
    log_console(level, prefix, msg, color);
    log_serial(level, prefix, msg);
}

void log_panic(const char *msg) {
    log_msg(LOG_PANIC, "[ panic ] ", msg, CONFIG_CONSOLE_COLOR_PANIC);
    // Future: halt or trigger debugger
}

void log_error(const char *msg) {
    log_msg(LOG_ERROR, "[ error ] ", msg, CONFIG_CONSOLE_COLOR_ERROR);
}

void log_warn(const char *msg) {
    log_msg(LOG_WARN,  "[  warn ] ", msg, CONFIG_CONSOLE_COLOR_WARN);
}

void log_info(const char *msg) {
    log_msg(LOG_INFO, "[  info ] ", msg, CONFIG_CONSOLE_COLOR_INFO);
}

void log_debug(const char *msg) {
    log_msg(LOG_DEBUG, "[ debug ] ", msg, CONFIG_CONSOLE_COLOR_DEBUG);
}

void log_trace(const char *msg) {
    log_msg(LOG_TRACE, "[ trace ] ", msg, CONFIG_CONSOLE_COLOR_TRACE);
}
