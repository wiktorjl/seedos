/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TTY Device Interface
 *
 * Provides VFS-compatible file operations for the console device.
 * This bridges the abstract VFS layer with the terminal output and
 * keyboard input drivers.
 */

#ifndef _TTY_DEV_H
#define _TTY_DEV_H

#include "vfs.h"

/**
 * tty_init - Initialize the TTY device subsystem
 *
 * Must be called after terminal_init() and keyboard_init().
 */
void tty_init(void);

/**
 * tty_open - Open a VFS file handle for the console
 * @flags: Open flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * Return: Allocated vfs_file_t or NULL on failure
 */
vfs_file_t *tty_open(int flags);

/**
 * tty_get_ops - Get the TTY file operations structure
 *
 * Return: Pointer to TTY file_ops_t
 */
file_ops_t *tty_get_ops(void);

#endif /* _TTY_DEV_H */
