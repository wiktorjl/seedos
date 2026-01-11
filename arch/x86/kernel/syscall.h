/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Syscall/Sysret Support
 *
 * The syscall instruction provides fast user->kernel transitions.
 * Unlike int 0x80 (which uses IDT and TSS), syscall uses MSRs:
 *
 *   EFER.SCE  - Enable syscall instruction
 *   STAR      - Segment selectors for syscall/sysret
 *   LSTAR     - Syscall entry point address
 *   SFMASK    - RFLAGS bits to clear on syscall
 *
 * When user executes syscall:
 *   1. RCX = RIP (return address)
 *   2. R11 = RFLAGS (saved flags)
 *   3. RIP = LSTAR (jump to kernel handler)
 *   4. CS  = STAR[47:32] (kernel code segment)
 *   5. SS  = STAR[47:32] + 8 (kernel data segment)
 *   6. RFLAGS &= ~SFMASK (clear specified flags)
 *
 * When kernel executes sysret:
 *   1. RIP = RCX (return to user)
 *   2. RFLAGS = R11 (restore flags)
 *   3. CS  = STAR[63:48] + 16 (user code segment)
 *   4. SS  = STAR[63:48] + 8 (user data segment)
 */

#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "types.h"

/*
 * MSR addresses for syscall configuration
 */
#define MSR_EFER    0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR    0xC0000081  /* Segment selectors */
#define MSR_LSTAR   0xC0000082  /* Long mode syscall entry point */
#define MSR_CSTAR   0xC0000083  /* Compat mode syscall (not used) */
#define MSR_SFMASK  0xC0000084  /* RFLAGS mask for syscall */

/*
 * EFER bits
 */
#define EFER_SCE    (1 << 0)    /* System Call Extensions - enable syscall/sysret */
#define EFER_LME    (1 << 8)    /* Long Mode Enable */
#define EFER_LMA    (1 << 10)   /* Long Mode Active (read-only) */
#define EFER_NXE    (1 << 11)   /* No-Execute Enable */

/*
 * RFLAGS bits for SFMASK
 */
#define RFLAGS_IF   (1 << 9)    /* Interrupt Flag */
#define RFLAGS_DF   (1 << 10)   /* Direction Flag */
#define RFLAGS_TF   (1 << 8)    /* Trap Flag (single-step) */
#define RFLAGS_AC   (1 << 18)   /* Alignment Check */

/*
 * Syscall frame - saved registers from syscall entry
 *
 * This matches the order registers are pushed in syscall_entry.S.
 * Passed as pointer to syscall_dispatch for clean argument access.
 */
typedef struct {
    uint64_t nr;        /* Syscall number (from RAX) */
    uint64_t arg1;      /* Arg 1 (from RDI) */
    uint64_t arg2;      /* Arg 2 (from RSI) */
    uint64_t arg3;      /* Arg 3 (from RDX) */
    uint64_t arg4;      /* Arg 4 (from R10 - Linux ABI) */
    uint64_t arg5;      /* Arg 5 (from R8) */
    uint64_t arg6;      /* Arg 6 (from R9) */
} syscall_frame_t;

/**
 * syscall_init - Initialize syscall/sysret support
 *
 * Configures the MSRs to enable the syscall instruction and set up
 * the entry point. Must be called after GDT is initialized.
 */
void syscall_init(void);

/**
 * syscall_dispatch - C syscall handler
 * @frame: Pointer to saved syscall registers
 *
 * Called from syscall_entry.S with pointer to syscall_frame_t.
 * Returns syscall result (or negative errno on error).
 */
int64_t syscall_dispatch(syscall_frame_t *frame);

/*
 * syscall_entry - Assembly syscall entry point
 *
 * Defined in syscall_entry.S. This is where RIP jumps on syscall.
 * Handles register saving, stack switching, and calls C handler.
 */
extern void syscall_entry(void);

#endif /* _SYSCALL_H */
