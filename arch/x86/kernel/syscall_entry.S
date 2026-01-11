/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Syscall Entry Point
 *
 * This is where the CPU jumps when user code executes syscall.
 * Full implementation in Task 2.2 - this is a minimal stub for Task 2.1.
 *
 * On entry from syscall instruction:
 *   RCX = user RIP (return address)
 *   R11 = user RFLAGS
 *   RAX = syscall number
 *   RDI, RSI, RDX, R10, R8, R9 = arguments (Linux x86-64 ABI)
 *   RSP = still user stack! (not switched)
 *   CS/SS = kernel segments (from STAR MSR)
 *   RFLAGS = masked by SFMASK (IF cleared)
 */

.section .text

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    /*
     * STUB: Full implementation in Task 2.2
     *
     * For now, just hang. We shouldn't reach here until
     * we have userspace processes making syscalls.
     */
    cli
1:
    hlt
    jmp 1b

.size syscall_entry, . - syscall_entry
