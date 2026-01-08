/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel shell interface
 */

#ifndef _KSHELL_H
#define _KSHELL_H

#define KSHELL_PROMPT		"seed> "
#define KSHELL_PROMPT_COLOR	0x00FF00
#define KSHELL_INPUT_COLOR	0xFFFFFF
#define KSHELL_ERROR_COLOR	0xFF0000
#define KSHELL_MAX_INPUT	256
#define KSHELL_MAX_ARGS		16
#define KSHELL_HISTORY_SIZE	16

/**
 * kshell_init - Initialize the kernel shell
 *
 * Clears input buffer and history. Call once before kshell_run().
 */
void kshell_init(void);

/**
 * kshell_run - Run the shell main loop
 *
 * This function does not return. It reads commands, parses them,
 * and dispatches to registered command handlers.
 */
void kshell_run(void);

#endif /* _KSHELL_H */
