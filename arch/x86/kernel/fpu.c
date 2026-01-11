// SPDX-License-Identifier: GPL-2.0-only
/*
 * FPU/SSE State Management Implementation
 *
 * Enables and manages floating point unit and SSE for userspace processes.
 * The kernel itself never uses FPU/SSE (compiled with -mno-sse), so this
 * is purely for userspace support.
 */

#include "fpu.h"
#include "log.h"

/*
 * Default MXCSR value for SSE
 *
 * Bits:
 *   [0]     IE  - Invalid operation exception mask (1 = masked)
 *   [1]     DE  - Denormal exception mask (1 = masked)
 *   [2]     ZE  - Divide-by-zero exception mask (1 = masked)
 *   [3]     OE  - Overflow exception mask (1 = masked)
 *   [4]     UE  - Underflow exception mask (1 = masked)
 *   [5]     PE  - Precision exception mask (1 = masked)
 *   [6]     DAZ - Denormals are zeros (0 = disabled)
 *   [7-12]  -   - Reserved / exception flags
 *   [13-14] RC  - Rounding control (00 = round to nearest)
 *   [15]    FZ  - Flush to zero (0 = disabled)
 *
 * 0x1F80 = all exceptions masked, round to nearest
 */
#define MXCSR_DEFAULT   0x1F80

/*
 * Default x87 FPU control word
 *
 * Bits:
 *   [0]     IM  - Invalid operation mask (1 = masked)
 *   [1]     DM  - Denormal mask (1 = masked)
 *   [2]     ZM  - Zero divide mask (1 = masked)
 *   [3]     OM  - Overflow mask (1 = masked)
 *   [4]     UM  - Underflow mask (1 = masked)
 *   [5]     PM  - Precision mask (1 = masked)
 *   [8-9]   PC  - Precision control (11 = 64-bit extended)
 *   [10-11] RC  - Rounding control (00 = round to nearest)
 *
 * 0x037F = all exceptions masked, 64-bit precision, round to nearest
 */
#define FCW_DEFAULT     0x037F

void fpu_init(void)
{
    uint64_t cr0, cr4;

    /*
     * Configure CR0:
     *   Clear EM (bit 2) - Don't emulate FPU, use real hardware
     *   Set MP (bit 1)   - Monitor coprocessor, needed for proper behavior
     *   Clear TS (bit 3) - Task switched flag (for lazy switching, clear for now)
     */
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~CR0_EM;     /* Clear emulation - allow FPU instructions */
    cr0 |= CR0_MP;      /* Set monitor coprocessor */
    cr0 &= ~CR0_TS;     /* Clear task switched (no lazy switching yet) */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    /*
     * Configure CR4:
     *   Set OSFXSR (bit 9)      - Enable fxsave/fxrstor and SSE
     *   Set OSXMMEXCPT (bit 10) - Enable SSE exceptions (#XM)
     */
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSFXSR;      /* Enable SSE */
    cr4 |= CR4_OSXMMEXCPT;  /* Enable SSE exceptions */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    /*
     * Initialize x87 FPU state
     * fninit resets the FPU to default state
     */
    __asm__ volatile("fninit");

    log_debug("FPU: CR0.EM=0, CR0.MP=1, CR4.OSFXSR=1, CR4.OSXMMEXCPT=1");
    log_debug("FPU: x87 and SSE enabled for userspace");
}

void fpu_save(fpu_state_t *state)
{
    /*
     * fxsave saves:
     *   - x87 FPU state (control word, status, tag, etc.)
     *   - x87 register stack ST(0)-ST(7)
     *   - SSE state (MXCSR)
     *   - SSE registers XMM0-XMM15
     *
     * Total: 512 bytes, must be 16-byte aligned
     */
    __asm__ volatile("fxsave %0" : "=m"(*state));
}

void fpu_restore(fpu_state_t *state)
{
    /*
     * fxrstor restores all state saved by fxsave
     */
    __asm__ volatile("fxrstor %0" :: "m"(*state));
}

void fpu_init_state(fpu_state_t *state)
{
    /*
     * Zero the entire state area first
     */
    uint8_t *p = (uint8_t *)state;
    for (int i = 0; i < 512; i++)
        p[i] = 0;

    /*
     * Set default control words
     *
     * fxsave layout:
     *   Offset 0x00: FCW (2 bytes) - x87 control word
     *   Offset 0x02: FSW (2 bytes) - x87 status word
     *   Offset 0x04: FTW (1 byte)  - x87 tag word (abridged)
     *   Offset 0x18: MXCSR (4 bytes) - SSE control/status
     *   Offset 0x1C: MXCSR_MASK (4 bytes) - valid MXCSR bits
     */
    uint16_t *fcw = (uint16_t *)&state->state[0x00];
    uint32_t *mxcsr = (uint32_t *)&state->state[0x18];

    *fcw = FCW_DEFAULT;
    *mxcsr = MXCSR_DEFAULT;
}
