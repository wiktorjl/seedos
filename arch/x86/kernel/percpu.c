// SPDX-License-Identifier: GPL-2.0-only
/*
 * Per-CPU Data Structure Implementation
 *
 * Sets up CPU-local storage accessed via GS segment. Critical for
 * syscall entry where we need to find kernel data without any
 * available registers.
 *
 * On syscall entry:
 *   1. User's GS is active (could be anything - TLS, etc.)
 *   2. swapgs exchanges GS_BASE with KERNEL_GS_BASE
 *   3. Now GS points to percpu_t, we can access kernel_rsp
 *   4. Before returning: swapgs restores user's GS
 */

#include "percpu.h"
#include "log.h"

/*
 * Per-CPU data for the boot CPU.
 * In a multiprocessor system, we'd allocate one of these per CPU.
 */
static percpu_t cpu0_percpu __attribute__((aligned(64)));

/**
 * percpu_init - Initialize per-CPU data for the boot CPU
 *
 * Sets up the percpu structure and configures GS_BASE MSRs:
 *   - GS_BASE = percpu address (active in kernel mode)
 *   - KERNEL_GS_BASE = percpu address (will be swapped in by swapgs)
 *
 * After this, the kernel can access percpu data via %gs:offset.
 * User processes will set their own GS_BASE (for TLS), and swapgs
 * will swap between user GS and kernel percpu.
 */
void percpu_init(void)
{
    uint64_t percpu_addr = (uint64_t)&cpu0_percpu;

    /* Initialize the structure */
    cpu0_percpu.current    = NULL;  /* No process running yet */
    cpu0_percpu.kernel_rsp = 0;     /* Will be set on context switch */
    cpu0_percpu.user_rsp   = 0;     /* Will be set during syscall */
    cpu0_percpu.self       = percpu_addr;  /* For sanity checks */
    cpu0_percpu.cpu_id     = 0;     /* Boot CPU */
    cpu0_percpu.irq_depth  = 0;     /* Not in interrupt */

    /*
     * Set both GS_BASE and KERNEL_GS_BASE to our percpu address.
     *
     * GS_BASE: Currently active GS base. Since we're in kernel mode
     * right now, this gives us immediate access via %gs:offset.
     *
     * KERNEL_GS_BASE: The value swapgs will load into GS_BASE.
     * When user code runs, GS_BASE will hold user's value (TLS).
     * On syscall entry, swapgs swaps them, giving us kernel access.
     */
    wrmsr(MSR_GS_BASE, percpu_addr);
    wrmsr(MSR_KERNEL_GS_BASE, percpu_addr);

    log_debug("PERCPU: Initialized at 0x%llx", percpu_addr);
    log_debug("PERCPU: GS_BASE=0x%llx, KERNEL_GS_BASE=0x%llx",
              rdmsr(MSR_GS_BASE), rdmsr(MSR_KERNEL_GS_BASE));
}

/**
 * percpu_get - Get pointer to current CPU's per-CPU data
 */
percpu_t *percpu_get(void)
{
    return &cpu0_percpu;
}

/**
 * percpu_set_kernel_stack - Set kernel stack for syscall entry
 * @rsp: Top of kernel stack for current process
 *
 * Called during context switch. When the new process makes a syscall,
 * the syscall handler will load this RSP before pushing anything.
 */
void percpu_set_kernel_stack(uint64_t rsp)
{
    cpu0_percpu.kernel_rsp = rsp;
}

/**
 * percpu_set_current - Set currently running process
 * @proc: Process that is about to run
 */
void percpu_set_current(struct process *proc)
{
    cpu0_percpu.current = proc;
}

/**
 * percpu_get_current - Get currently running process
 */
struct process *percpu_get_current(void)
{
    return cpu0_percpu.current;
}
