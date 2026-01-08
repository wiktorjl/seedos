// SPDX-License-Identifier: GPL-2.0-only
/*
 * Terminal abstraction layer
 *
 * Routes output to different backends (framebuffer, serial) per terminal.
 */

#include "terminal.h"
#include "config.h"
#include "console.h"
#include "serial.h"

terminal_t terminal_default = {
	.backends = TERM_BACKEND_ALL,
	.color = CONFIG_CONSOLE_COLOR_DEFAULT
};

static terminal_t *active_terminal = &terminal_default;

/**
 * terminal_init - Initialize terminal subsystem
 */
void terminal_init(void)
{
#if CONFIG_OUTPUT_SERIAL
	serial_init();
#endif
	active_terminal = &terminal_default;
}

/**
 * terminal_get_active - Get currently active terminal
 *
 * Return: pointer to active terminal
 */
terminal_t *terminal_get_active(void)
{
	return active_terminal;
}

/**
 * terminal_set_active - Set the active terminal
 * @term: terminal to make active
 */
void terminal_set_active(terminal_t *term)
{
	if (term)
		active_terminal = term;
}

/**
 * terminal_create - Initialize a terminal structure
 * @term: terminal to initialize
 * @backends: backend flags (TERM_BACKEND_*)
 * @color: default text color
 */
void terminal_create(terminal_t *term, uint32_t backends, uint32_t color)
{
	if (!term)
		return;
	term->backends = backends;
	term->color = color;
}

/**
 * terminal_putchar - Write a character to terminal
 * @term: terminal to write to, or NULL for active terminal
 * @c: character to write
 */
void terminal_putchar(terminal_t *term, char c)
{
	if (!term)
		term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB)
		console_putchar(c, term->color);
#endif

#if CONFIG_OUTPUT_SERIAL
	if (term->backends & TERM_BACKEND_SERIAL)
		serial_putchar(c);
#endif
}

/**
 * terminal_puts - Write a string to terminal
 * @term: terminal to write to, or NULL for active terminal
 * @str: null-terminated string
 */
void terminal_puts(terminal_t *term, const char *str)
{
	if (!term)
		term = active_terminal;
	while (*str)
		terminal_putchar(term, *str++);
}

/**
 * terminal_set_color - Set terminal text color
 * @term: terminal to modify, or NULL for active terminal
 * @color: 32-bit RGB color
 */
void terminal_set_color(terminal_t *term, uint32_t color)
{
	if (!term)
		term = active_terminal;
	term->color = color;
}

/**
 * terminal_clear - Clear terminal screen
 * @term: terminal to clear, or NULL for active terminal
 */
void terminal_clear(terminal_t *term)
{
	if (!term)
		term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB)
		console_clear(0x000000);
#endif

#if CONFIG_OUTPUT_SERIAL
	if (term->backends & TERM_BACKEND_SERIAL)
		serial_puts("\033[2J\033[H");
#endif
}

/**
 * terminal_get_cursor - Get cursor position in character cells
 * @term: terminal to query, or NULL for active terminal
 * @col: output column position
 * @row: output row position
 */
void terminal_get_cursor(terminal_t *term, int *col, int *row)
{
	if (!term)
		term = active_terminal;

	int px = 0, py = 0;
#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB)
		console_get_cursor(&px, &py);
#endif
	if (col)
		*col = px / 8;
	if (row)
		*row = py / 16;
}

/**
 * terminal_get_dimensions - Get terminal size in character cells
 * @term: terminal to query, or NULL for active terminal
 * @cols: output column count
 * @rows: output row count
 */
void terminal_get_dimensions(terminal_t *term, int *cols, int *rows)
{
	if (!term)
		term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB) {
		console_get_dimensions(cols, rows);
		return;
	}
#endif
	if (cols)
		*cols = 80;
	if (rows)
		*rows = 24;
}

/**
 * terminal_scroll_back - Scroll back into history
 * @term: terminal to scroll, or NULL for active terminal
 * @lines: number of lines to scroll back
 */
void terminal_scroll_back(terminal_t *term, int lines)
{
	if (!term)
		term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB)
		console_scroll_back(lines);
#endif
}

/**
 * terminal_scroll_forward - Scroll forward toward live view
 * @term: terminal to scroll, or NULL for active terminal
 * @lines: number of lines to scroll forward
 */
void terminal_scroll_forward(terminal_t *term, int lines)
{
	if (!term)
		term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB)
		console_scroll_forward(lines);
#endif
}

/**
 * terminal_scroll_to_bottom - Return to live view
 * @term: terminal to scroll, or NULL for active terminal
 */
void terminal_scroll_to_bottom(terminal_t *term)
{
	if (!term)
		term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB)
		console_scroll_to_bottom();
#endif
}

/**
 * terminal_is_scrolled_back - Check if viewing history
 * @term: terminal to query, or NULL for active terminal
 *
 * Return: non-zero if scrolled back, 0 if at live view
 */
int terminal_is_scrolled_back(terminal_t *term)
{
	if (!term)
		term = active_terminal;

#if CONFIG_OUTPUT_CONSOLE
	if (term->backends & TERM_BACKEND_FB)
		return console_is_scrolled_back();
#endif
	return 0;
}
