/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel Init Loader
 *
 * Loads and executes /init from the initrd filesystem.
 */

#ifndef _KINIT_H
#define _KINIT_H

/**
 * start_init - Load and execute /init from initrd
 *
 * This is the final step in kernel initialization.
 * After this, we're running in userspace.
 *
 * Prerequisites:
 * - All kernel subsystems initialized
 * - ext2 filesystem mounted on initrd
 * - /init exists in the initrd
 *
 * This function does NOT return.
 */
void start_init(void) __attribute__((noreturn));

#endif /* _KINIT_H */
