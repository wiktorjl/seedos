// Unified I/O: routes output to enabled backends

#include "io.h"
#include "config.h"
#include "console.h"

void io_init(void) {
    // Future: initialize serial port here
}

void putchar(char c) {
#if CONFIG_OUTPUT_CONSOLE
    console_putchar(c, CONFIG_CONSOLE_COLOR_DEFAULT);
#endif

#if CONFIG_OUTPUT_SERIAL
    // serial_putchar(c);
#endif
}

void puts(const char *str) {
    while (*str)
        putchar(*str++);
}
