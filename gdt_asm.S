/*
 * gdt_asm.S - Assembly routines for GDT and TSS loading
 *
 * Loading a new GDT requires:
 * 1. lgdt instruction to point CPU at new GDT
 * 2. Far jump to reload CS with new selector
 * 3. Reload data segment registers (DS, SS, ES, FS, GS)
 */

.section .note.GNU-stack, "", @progbits

.section .text
.code64

/*
 * void gdt_load(struct gdtr *gdtr, uint16_t code_sel, uint16_t data_sel)
 *
 * Arguments:
 *   %rdi = pointer to GDTR
 *   %rsi = kernel code selector
 *   %rdx = kernel data selector
 */
.global gdt_load
gdt_load:
    /* Load the GDT */
    lgdt (%rdi)

    /* Reload data segments with new data selector */
    movw %dx, %ds
    movw %dx, %es
    movw %dx, %fs
    movw %dx, %gs
    movw %dx, %ss

    /*
     * Reload CS with a far return.
     * We can't do a direct far jump in 64-bit mode, so we:
     * 1. Push the new CS
     * 2. Push the return address
     * 3. Execute retfq (far return)
     */
    pushq %rsi              /* Push new CS */
    leaq .reload_cs(%rip), %rax
    pushq %rax              /* Push return address */
    retfq                   /* Far return - pops RIP and CS */

.reload_cs:
    ret


/*
 * void tss_load(uint16_t tss_sel)
 *
 * Arguments:
 *   %di = TSS selector
 */
.global tss_load
tss_load:
    ltr %di
    ret
