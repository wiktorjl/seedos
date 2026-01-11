/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Per-CPU Data Structure
 *
 * Provides CPU-local storage accessible via the GS segment register.
 * Critical for syscall entry where we need to find kernel data without
 * any registers available.
 *
 * How it works:
 *   1. In kernel mode: GS_BASE points to percpu_t
 *   2. In user mode: GS_BASE is whatever user set (usually TLS)
 *   3. KERNEL_GS_BASE always holds the kernel's percpu_t pointer
 *   4. swapgs atomically swaps GS_BASE <-> KERNEL_GS_BASE
 *
 * Syscall entry flow:
 *   - User calls syscall, we arrive in kernel with user's GS
 *   - Execute swapgs: now GS_BASE = &percpu, KERNEL_GS_BASE = user's value
 *   - Access %gs:PERCPU_KERNEL_RSP to get kernel stack
 *   - Before returning: swapgs again to restore user's GS
 */

#ifndef _PERCPU_H
#define _PERCPU_H

#include "types.h"

/* Forward declaration - process_t will be defined later */
struct process;

/*
 * Per-CPU data structure
 *
 * This structure is accessed via GS segment in syscall entry.
 * Keep fields that need fast access at low offsets.
 *
 * IMPORTANT: If you change this structure, update the offset
 * constants below and in any assembly that uses them!
 */
typedef struct {
    struct process *current;    /* +0x00: Currently running process */
    uint64_t kernel_rsp;        /* +0x08: Kernel stack pointer for syscall entry */
    uint64_t user_rsp;          /* +0x10: Saved user RSP during syscall */
    uint64_t self;              /* +0x18: Pointer to this struct (for sanity checks) */
    uint32_t cpu_id;            /* +0x20: CPU number (0 for uniprocessor) */
    uint32_t irq_depth;         /* +0x24: Interrupt nesting depth */
} percpu_t;

/*
 * Offsets for assembly access via %gs:OFFSET
 * Must match structure layout above!
 */
#define PERCPU_CURRENT      0x00
#define PERCPU_KERNEL_RSP   0x08
#define PERCPU_USER_RSP     0x10
#define PERCPU_SELF         0x18
#define PERCPU_CPU_ID       0x20
#define PERCPU_IRQ_DEPTH    0x24

/*
 * MSR addresses for GS base manipulation
 */
#define MSR_GS_BASE         0xC0000101  /* Current GS base */
#define MSR_KERNEL_GS_BASE  0xC0000102  /* Swapped in by swapgs */

/**
 * percpu_init - Initialize per-CPU data for the boot CPU
 *
 * Sets up the percpu structure and configures GS_BASE and KERNEL_GS_BASE
 * MSRs. Must be called early in boot, after GDT is loaded.
 */
void percpu_init(void);

/**
 * percpu_get - Get pointer to current CPU's per-CPU data
 *
 * Returns pointer to the percpu_t for the executing CPU.
 * In uniprocessor SeedOS, this always returns &cpu0_percpu.
 */
percpu_t *percpu_get(void);

/**
 * percpu_set_kernel_stack - Set kernel stack for syscall entry
 * @rsp: Top of kernel stack for current process
 *
 * Called during context switch to set the kernel stack that will
 * be loaded when the new process makes a syscall.
 */
void percpu_set_kernel_stack(uint64_t rsp);

/**
 * percpu_set_current - Set currently running process
 * @proc: Process that is about to run
 *
 * Called during context switch.
 */
void percpu_set_current(struct process *proc);

/**
 * percpu_get_current - Get currently running process
 *
 * Returns the process_t for the currently executing process.
 */
struct process *percpu_get_current(void);

/*
 * MSR access helpers
 */

/**
 * rdmsr - Read a Model-Specific Register
 * @msr: MSR address to read
 *
 * Returns the 64-bit value of the MSR.
 */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/**
 * wrmsr - Write a Model-Specific Register
 * @msr: MSR address to write
 * @value: 64-bit value to write
 */
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

#endif /* _PERCPU_H */
