/*
 * shell.h - Simple Kernel Shell
 *
 * This module provides an interactive command-line interface for testing
 * and debugging kernel functionality. It runs in kernel mode (ring 0).
 *
 * Architecture:
 *
 *   ┌────────────────────────────────────────────────────────────────┐
 *   │  User types on keyboard                                        │
 *   └────────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌────────────────────────────────────────────────────────────────┐
 *   │  Keyboard IRQ → keyboard_handler() → keyboard buffer           │
 *   └────────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌────────────────────────────────────────────────────────────────┐
 *   │  Main loop: keyboard_get_char() → shell_input()                │
 *   └────────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌────────────────────────────────────────────────────────────────┐
 *   │  Shell buffers characters until Enter, then executes command   │
 *   └────────────────────────────────────────────────────────────────┘
 *
 * Input Handling:
 *
 *   The shell uses character-by-character input (not line-buffered):
 *   - Printable chars: Added to command buffer and echoed
 *   - Backspace: Removes last char from buffer, erases from screen
 *   - Enter: Executes the buffered command
 *
 * Available Commands:
 *
 *   help      - List all available commands
 *   meminfo   - Show physical memory statistics (total/free/used pages)
 *   alloc     - Allocate one physical page (for testing PMM)
 *   free <a>  - Free a physical page at address <a>
 *   crash     - Deliberately trigger a page fault (test exception handler)
 *   divzero   - Deliberately trigger divide-by-zero (test exception handler)
 *   clear     - Clear the screen
 *
 * Note: This is a kernel shell, not a user-space shell. It has full
 * access to kernel functions and memory.
 */

#ifndef SHELL_H
#define SHELL_H

/* =============================================================================
 * Shell API Functions
 * =============================================================================
 */

/*
 * shell_init - Initialize the shell and display welcome message.
 *
 * Prints the shell banner and initial prompt. Call this once during
 * kernel initialization after console and keyboard are ready.
 */
void shell_init(void);

/*
 * shell_prompt - Display the shell prompt.
 *
 * Prints "seed> " to indicate the shell is ready for input.
 * Called internally after command execution; not typically called directly.
 */
void shell_prompt(void);

/*
 * shell_input - Process a single character of input.
 *
 * @c: The character received from the keyboard.
 *
 * This function should be called for each character from the keyboard.
 * It handles:
 *   - Buffering characters until Enter is pressed
 *   - Echoing typed characters to the screen
 *   - Backspace handling
 *   - Command execution on Enter
 *
 * Typical usage in main loop:
 *
 *   while(1) {
 *       if(keyboard_has_char()) {
 *           shell_input(keyboard_get_char());
 *       }
 *   }
 */
void shell_input(char c);

#endif /* SHELL_H */