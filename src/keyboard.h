/*
 * keyboard.h - PS/2 Keyboard Driver
 *
 * Implements interrupt-driven keyboard input using the PS/2 controller.
 * Scancodes are translated to ASCII and buffered for retrieval.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

/* =============================================================================
 * Keyboard Configuration
 * =============================================================================
 */

#define KBD_BUFFER_SIZE     64      /* Keyboard input buffer size */
#define IRQ_KEYBOARD        33      /* Interrupt vector for keyboard (ISA IRQ 1 + 32) */

/* =============================================================================
 * Special Key Codes
 *
 * Non-printable keys are represented as codes >= 0x80
 * =============================================================================
 */

#define KEY_ESCAPE      0x1B
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A

/* Extended keys (returned as negative values or special codes) */
#define KEY_F1          0x80
#define KEY_F2          0x81
#define KEY_F3          0x82
#define KEY_F4          0x83
#define KEY_F5          0x84
#define KEY_F6          0x85
#define KEY_F7          0x86
#define KEY_F8          0x87
#define KEY_F9          0x88
#define KEY_F10         0x89
#define KEY_F11         0x8A
#define KEY_F12         0x8B

#define KEY_UP          0x90
#define KEY_DOWN        0x91
#define KEY_LEFT        0x92
#define KEY_RIGHT       0x93

#define KEY_HOME        0x94
#define KEY_END         0x95
#define KEY_PAGEUP      0x96
#define KEY_PAGEDOWN    0x97
#define KEY_INSERT      0x98
#define KEY_DELETE      0x99

/* =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * keyboard_init - Initialize the PS/2 keyboard driver.
 *
 * Sets up the keyboard interrupt handler via I/O APIC.
 * Must be called after ioapic_init().
 */
void keyboard_init(void);

/*
 * keyboard_getchar - Get a character from the keyboard buffer.
 *
 * Returns: ASCII character, or -1 if buffer is empty (non-blocking).
 */
int keyboard_getchar(void);

/*
 * keyboard_read - Read a character, blocking until one is available.
 *
 * Returns: ASCII character.
 *
 * This function halts the CPU while waiting for input.
 */
char keyboard_read(void);

/*
 * keyboard_has_input - Check if keyboard input is available.
 *
 * Returns: 1 if there's input in the buffer, 0 otherwise.
 */
int keyboard_has_input(void);

#endif /* KEYBOARD_H */
