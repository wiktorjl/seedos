/*
 * keyboard.h - PS/2 Keyboard Driver
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/*
 * Initialize the keyboard (enable IRQ1)
 */
void keyboard_init(void);

/*
 * Handle keyboard interrupt - called from interrupt handler
 */
void keyboard_handler(void);

/*
 * Check if a character is available
 */
int keyboard_has_char(void);

/*
 * Get the next character from the keyboard buffer
 * Returns 0 if no character available
 */
char keyboard_get_char(void);

#endif /* KEYBOARD_H */