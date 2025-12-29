/*
 * kshell.h - Kernel Shell
 *
 * Minimal command-line interface for kernel-space interaction.
 */

#ifndef KSHELL_H
#define KSHELL_H

/* =============================================================================
 * Shell Configuration
 * =============================================================================
 */

#define KSHELL_PROMPT           "seed> "
#define KSHELL_PROMPT_COLOR     0x00FF00    /* Green */
#define KSHELL_INPUT_COLOR      0xFFFFFF    /* White */
#define KSHELL_ERROR_COLOR      0xFF0000    /* Red */
#define KSHELL_MAX_INPUT        256         /* Max command line length */
#define KSHELL_MAX_ARGS         16          /* Max arguments per command */
#define KSHELL_HISTORY_SIZE     16          /* Number of commands to remember */

/* =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * kshell_init - Initialize the kernel shell.
 *
 * Clears input buffer and history. Call once before kshell_run().
 */
void kshell_init(void);

/*
 * kshell_run - Run the shell main loop.
 *
 * This function does not return. It reads commands, parses them,
 * and dispatches to registered command handlers.
 */
void kshell_run(void);

#endif /* KSHELL_H */
