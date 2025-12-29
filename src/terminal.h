/*
 * terminal.h - Terminal Abstraction
 *
 * Allows multiple terminals with different backends (serial, framebuffer).
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#include "types.h"

/* Terminal output backends */
#define TERM_BACKEND_NONE       0
#define TERM_BACKEND_SERIAL     (1 << 0)
#define TERM_BACKEND_FB         (1 << 1)
#define TERM_BACKEND_ALL        (TERM_BACKEND_SERIAL | TERM_BACKEND_FB)

/* Terminal structure */
typedef struct terminal {
    uint32_t backends;          /* Bitmask of enabled backends */
    uint32_t color;             /* Current text color (for FB backend) */
} terminal_t;

/* Initialize the terminal system */
void terminal_init(void);

/* Get the current active terminal */
terminal_t *terminal_get_active(void);

/* Set the active terminal */
void terminal_set_active(terminal_t *term);

/* Create a new terminal with specified backends */
void terminal_create(terminal_t *term, uint32_t backends, uint32_t color);

/* Terminal output functions */
void terminal_putchar(terminal_t *term, char c);
void terminal_puts(terminal_t *term, const char *str);
void terminal_set_color(terminal_t *term, uint32_t color);

/* Screen management (operates on FB backend) */
void terminal_clear(terminal_t *term);
void terminal_get_cursor(terminal_t *term, int *col, int *row);
void terminal_get_dimensions(terminal_t *term, int *cols, int *rows);

/* Default terminal (outputs to all backends) */
extern terminal_t terminal_default;

#endif /* TERMINAL_H */
