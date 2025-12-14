/*
 * keyboard.h - PS/2 Keyboard Driver
 *
 * This driver handles the PS/2 keyboard, which is the standard PC keyboard
 * interface. Even on modern systems with USB keyboards, the BIOS typically
 * emulates PS/2 for compatibility (USB Legacy Support).
 *
 * PS/2 Keyboard Overview:
 *
 *   The PS/2 keyboard communicates via two I/O ports:
 *     - Port 0x60: Data port (read scancodes, write commands to keyboard)
 *     - Port 0x64: Status/command port (check status, send commands to controller)
 *
 *   When a key is pressed or released, the keyboard controller:
 *     1. Places the scancode in port 0x60
 *     2. Asserts IRQ1 (mapped to INT 33 after PIC remapping)
 *
 * Scancode Sets:
 *
 *   PS/2 keyboards support three scancode sets. Set 1 (XT) is most common:
 *     - Key press: Scancode value (0x00-0x7F)
 *     - Key release: Scancode | 0x80 (high bit set)
 *
 *   For example, 'A' key:
 *     - Press:   0x1E
 *     - Release: 0x9E (0x1E | 0x80)
 *
 * Keyboard Buffer:
 *
 *   This driver uses a circular buffer to store typed characters.
 *   The interrupt handler converts scancodes to ASCII and queues them.
 *   The shell (or other code) reads characters via keyboard_get_char().
 *
 *   [Producer: IRQ handler] --> [Circular Buffer] --> [Consumer: shell]
 *
 * Limitations:
 *   - Only handles basic US keyboard layout
 *   - No support for extended keys (F1-F12, arrows, etc.)
 *   - No caps lock support (only shift)
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

/* =============================================================================
 * Keyboard API Functions
 * =============================================================================
 */

/*
 * keyboard_init - Initialize the PS/2 keyboard driver.
 *
 * Enables IRQ1 so the keyboard can generate interrupts.
 * Must be called after pic_init() and idt_init().
 */
void keyboard_init(void);

/*
 * keyboard_handler - Handle a keyboard interrupt.
 *
 * Called by the interrupt dispatcher when IRQ1 fires.
 * Reads the scancode from port 0x60, converts to ASCII,
 * and adds to the keyboard buffer.
 *
 * Note: This runs in interrupt context - keep it fast!
 */
void keyboard_handler(void);

/*
 * keyboard_has_char - Check if a character is available in the buffer.
 *
 * Returns: Non-zero if at least one character is available, 0 otherwise.
 *
 * Use this to poll for input without blocking.
 */
int keyboard_has_char(void);

/*
 * keyboard_get_char - Get the next character from the keyboard buffer.
 *
 * Returns: The next ASCII character, or 0 if buffer is empty.
 *
 * This is non-blocking. Use keyboard_has_char() first to check
 * if input is available, or use a loop:
 *
 *   while(!keyboard_has_char()) { }
 *   char c = keyboard_get_char();
 */
char keyboard_get_char(void);

size_t keyboard_read(char *buf, size_t len);

/*
 * keyboard_wait - Block until keyboard input is available.
 *
 * Blocks the current process until at least one character is in the buffer.
 * Uses proper interrupt-safe synchronization.
 */
void keyboard_wait(void);

#endif /* KEYBOARD_H */