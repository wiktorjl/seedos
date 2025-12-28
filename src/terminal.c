// Terminal abstraction: routes output to different backends per terminal

#include "terminal.h"
#include "config.h"
#include "console.h"
#include "serial.h"

// Default terminal outputs to all enabled backends
terminal_t terminal_default = {
    .backends = TERM_BACKEND_ALL,
    .color = CONFIG_CONSOLE_COLOR_DEFAULT
};

// Currently active terminal
static terminal_t *active_terminal = &terminal_default;

void terminal_init(void) {
#if CONFIG_OUTPUT_SERIAL
    serial_init();
#endif
    active_terminal = &terminal_default;
}

terminal_t *terminal_get_active(void) {
    return active_terminal;
}

void terminal_set_active(terminal_t *term) {
    if (term)
        active_terminal = term;
}

void terminal_create(terminal_t *term, uint32_t backends, uint32_t color) {
    if (!term) return;
    term->backends = backends;
    term->color = color;
}

void terminal_putchar(terminal_t *term, char c) {
    if (!term) term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
    if (term->backends & TERM_BACKEND_FB)
        console_putchar(c, term->color);
#endif

#if CONFIG_OUTPUT_SERIAL
    if (term->backends & TERM_BACKEND_SERIAL)
        serial_putchar(c);
#endif
}

void terminal_puts(terminal_t *term, const char *str) {
    if (!term) term = active_terminal;
    while (*str)
        terminal_putchar(term, *str++);
}

void terminal_set_color(terminal_t *term, uint32_t color) {
    if (!term) term = active_terminal;
    term->color = color;
}

void terminal_clear(terminal_t *term) {
    if (!term) term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
    if (term->backends & TERM_BACKEND_FB)
        console_clear(0x000000);
#endif

    // For serial, send clear screen escape sequence
#if CONFIG_OUTPUT_SERIAL
    if (term->backends & TERM_BACKEND_SERIAL) {
        serial_puts("\033[2J\033[H");  // Clear screen + home cursor
    }
#endif
}

void terminal_get_cursor(terminal_t *term, int *col, int *row) {
    if (!term) term = active_terminal;

    int px = 0, py = 0;
#if CONFIG_OUTPUT_CONSOLE
    if (term->backends & TERM_BACKEND_FB)
        console_get_cursor(&px, &py);
#endif
    if (col) *col = px / 8;
    if (row) *row = py / 16;
}

void terminal_get_dimensions(terminal_t *term, int *cols, int *rows) {
    if (!term) term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
    if (term->backends & TERM_BACKEND_FB) {
        console_get_dimensions(cols, rows);
        return;
    }
#endif
    // Default for serial (standard terminal size)
    if (cols) *cols = 80;
    if (rows) *rows = 24;
}
