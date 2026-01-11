/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * User Mode Entry
 *
 * Functions for transitioning from kernel mode (Ring 0) to user mode (Ring 3).
 */

#ifndef _USERMODE_H
#define _USERMODE_H

#include "types.h"

/**
 * user_mode_enter - Enter user mode
 * @entry: User-space entry point (RIP)
 * @user_rsp: User-space stack pointer (RSP)
 *
 * Transfers control to Ring 3 at the specified entry point.
 * This function does NOT return - it switches to user mode via iretq.
 *
 * Before calling:
 * - User address space must be set up with code loaded
 * - User stack must be mapped and initialized
 * - TSS.rsp0 should be set to the kernel stack for this process
 * - CR3 should point to the process's page table
 *
 * The function will:
 * - Clear all general-purpose registers (security)
 * - Execute swapgs to switch GS_BASE
 * - Build an iret frame and execute iretq
 */
void user_mode_enter(uint64_t entry, uint64_t user_rsp) __attribute__((noreturn));

#endif /* _USERMODE_H */
