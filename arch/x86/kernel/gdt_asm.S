/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GDT segment register reload
 *
 * After loading a new GDT with lgdt, the segment registers still hold
 * selectors from the old GDT. This routine reloads them with the new
 * selectors.
 *
 * CS cannot be loaded directly - we use a far return (lretq) to
 * atomically update CS:RIP.
 */

/*
 * GDT Selectors - must match gdt.h!
 *
 * These are duplicated here because this is assembly and can't include
 * the C header (which pulls in stdint.h). Keep in sync with gdt.h.
 */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10

.section .text

/*
 * gdt_reload - Reload all segment registers with new GDT selectors
 *
 * Called after lgdt to activate the new segments.
 * Uses far return trick to reload CS.
 */
.global gdt_reload
.type gdt_reload, @function
gdt_reload:
    /*
     * Reload data segment registers.
     * In 64-bit mode, the base is always 0 regardless of selector,
     * but the selector still determines the segment's DPL.
     */
    mov $GDT_KERNEL_DATA, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /*
     * Clear FS and GS for now.
     * FS will be used for TLS (Thread Local Storage) later.
     * GS is used for per-CPU data and will be set up separately.
     */
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs

    /*
     * Reload CS using far return.
     *
     * We can't do "mov %ax, %cs" - CS is read-only except via far jumps/calls.
     * Instead we:
     *   1. Push the new CS selector
     *   2. Push the return address (label after lretq)
     *   3. Execute lretq which pops RIP and CS atomically
     *
     * Stack before lretq:
     *   [RSP]   -> return address (.reload_cs)
     *   [RSP+8] -> new CS selector (GDT_KERNEL_CODE)
     */
    pushq $GDT_KERNEL_CODE   /* New CS selector */
    leaq .reload_cs(%rip), %rax
    pushq %rax               /* Return address */
    lretq                    /* Far return: pop RIP, pop CS */

.reload_cs:
    ret

.size gdt_reload, . - gdt_reload
