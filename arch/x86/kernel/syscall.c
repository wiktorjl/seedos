// SPDX-License-Identifier: GPL-2.0-only
/*
 * Syscall/Sysret MSR Configuration
 *
 * Sets up the MSRs that control fast syscall/sysret instructions.
 */

#include "syscall.h"
#include "percpu.h"
#include "gdt.h"
#include "log.h"

void syscall_init(void)
{
    uint64_t efer, star;

    /*
     * Enable syscall/sysret in EFER
     *
     * EFER (Extended Feature Enable Register) controls CPU features.
     * We need to set SCE (System Call Extensions) to enable syscall.
     */
    efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    /*
     * Configure STAR (Segment Selector Register)
     *
     * STAR layout:
     *   Bits 63:48 = SYSRET CS/SS base
     *   Bits 47:32 = SYSCALL CS/SS base
     *   Bits 31:0  = Reserved (must be 0)
     *
     * On SYSCALL:
     *   CS = STAR[47:32]     = 0x08 (GDT_KERNEL_CODE)
     *   SS = STAR[47:32] + 8 = 0x10 (GDT_KERNEL_DATA)
     *
     * On SYSRET:
     *   SS = STAR[63:48] + 8  = 0x18 (GDT_USER_DATA)
     *   CS = STAR[63:48] + 16 = 0x20 (GDT_USER_CODE)
     *
     * Note: SYSRET's weird +8/+16 is why User Data must come before
     * User Code in the GDT!
     */
    star = ((uint64_t)GDT_KERNEL_DATA << 48) |  /* SYSRET base: 0x10 */
           ((uint64_t)GDT_KERNEL_CODE << 32);   /* SYSCALL base: 0x08 */
    wrmsr(MSR_STAR, star);

    /*
     * Set LSTAR (Long mode Syscall Target Address)
     *
     * This is where RIP jumps when user executes syscall.
     * Points to our assembly entry stub that handles register
     * saving and stack switching.
     */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /*
     * Set SFMASK (Syscall Flag Mask)
     *
     * RFLAGS bits to CLEAR on syscall entry. We clear:
     *   IF (bit 9)  - Disable interrupts until we set up kernel stack
     *   DF (bit 10) - Clear direction flag (string ops go forward)
     *   TF (bit 8)  - Clear trap flag (no single-stepping in kernel)
     *   AC (bit 18) - Clear alignment check
     *
     * Interrupts are re-enabled after we've saved state and have
     * a valid kernel stack.
     */
    wrmsr(MSR_SFMASK, RFLAGS_IF | RFLAGS_DF | RFLAGS_TF | RFLAGS_AC);

    log_debug("SYSCALL: EFER=0x%llx (SCE enabled)", rdmsr(MSR_EFER));
    log_debug("SYSCALL: STAR=0x%llx (kernel=0x%02x, user_base=0x%02x)",
              rdmsr(MSR_STAR), GDT_KERNEL_CODE, GDT_KERNEL_DATA);
    log_debug("SYSCALL: LSTAR=0x%llx", rdmsr(MSR_LSTAR));
    log_debug("SYSCALL: SFMASK=0x%llx", rdmsr(MSR_SFMASK));
}
