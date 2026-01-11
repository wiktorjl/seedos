/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FPU/SSE State Management
 *
 * x86-64 has multiple sets of registers beyond the general purpose ones:
 *
 *   x87 FPU:  ST(0)-ST(7)     - 80-bit floating point stack
 *   SSE:      XMM0-XMM15      - 128-bit vector registers
 *   AVX:      YMM0-YMM15      - 256-bit (extends XMM)
 *
 * The kernel is compiled with -mno-sse and never touches these registers.
 * But userspace programs use them freely, so we must save/restore them
 * during context switches to prevent processes from corrupting each other's
 * floating point state.
 *
 * fxsave/fxrstor handle both x87 FPU and SSE state in one 512-byte block.
 */

#ifndef _FPU_H
#define _FPU_H

#include "types.h"

/*
 * FPU/SSE state save area
 *
 * fxsave stores 512 bytes of state. Must be 16-byte aligned.
 * Layout (simplified):
 *   0x000: FCW, FSW, FTW, FOP    - x87 control/status
 *   0x008: FIP, FCS, FDP, FDS    - x87 instruction/data pointers
 *   0x018: MXCSR, MXCSR_MASK     - SSE control/status
 *   0x020: ST(0)-ST(7)           - x87 registers (128 bytes)
 *   0x0A0: XMM0-XMM15            - SSE registers (256 bytes)
 *   0x1A0: Reserved              - Padding to 512 bytes
 */
typedef struct {
    uint8_t state[512];
} __attribute__((aligned(16))) fpu_state_t;

/*
 * CR0 bits for FPU control
 */
#define CR0_EM  (1 << 2)    /* Emulation - if set, FPU instructions cause #NM */
#define CR0_MP  (1 << 1)    /* Monitor coprocessor */
#define CR0_TS  (1 << 3)    /* Task switched - for lazy FPU switching */

/*
 * CR4 bits for SSE control
 */
#define CR4_OSFXSR      (1 << 9)    /* OS supports fxsave/fxrstor */
#define CR4_OSXMMEXCPT  (1 << 10)   /* OS handles SSE exceptions */

/**
 * fpu_init - Enable FPU and SSE support
 *
 * Configures CR0 and CR4 to allow userspace to use x87 FPU and SSE
 * instructions. Must be called during boot before any user process runs.
 */
void fpu_init(void);

/**
 * fpu_save - Save current FPU/SSE state
 * @state: Destination buffer (must be 16-byte aligned)
 *
 * Saves all x87 FPU and SSE registers to memory using fxsave.
 * Called during context switch before switching to another process.
 */
void fpu_save(fpu_state_t *state);

/**
 * fpu_restore - Restore FPU/SSE state
 * @state: Source buffer (must be 16-byte aligned)
 *
 * Restores all x87 FPU and SSE registers from memory using fxrstor.
 * Called during context switch when resuming a process.
 */
void fpu_restore(fpu_state_t *state);

/**
 * fpu_init_state - Initialize FPU state to clean defaults
 * @state: State buffer to initialize
 *
 * Sets up a clean FPU/SSE state for a new process.
 * Equivalent to the state after finit + SSE reset.
 */
void fpu_init_state(fpu_state_t *state);

#endif /* _FPU_H */
