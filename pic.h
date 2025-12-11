/*
 * pic.h - 8259 Programmable Interrupt Controller (PIC)
 *
 * The 8259 PIC is the classic IBM PC interrupt controller. Modern PCs have
 * two cascaded PICs (master and slave) providing 15 usable IRQ lines.
 *
 * Hardware IRQ Lines:
 *
 *   Master PIC (IRQ 0-7):           Slave PIC (IRQ 8-15):
 *   ┌─────────────────────────┐     ┌─────────────────────────┐
 *   │ IRQ 0: PIT Timer        │     │ IRQ 8:  RTC             │
 *   │ IRQ 1: PS/2 Keyboard    │     │ IRQ 9:  ACPI/Available  │
 *   │ IRQ 2: Cascade (slave)  │────>│ IRQ 10: Available       │
 *   │ IRQ 3: COM2/COM4        │     │ IRQ 11: Available       │
 *   │ IRQ 4: COM1/COM3        │     │ IRQ 12: PS/2 Mouse      │
 *   │ IRQ 5: LPT2/Sound       │     │ IRQ 13: FPU             │
 *   │ IRQ 6: Floppy           │     │ IRQ 14: Primary ATA     │
 *   │ IRQ 7: LPT1/Spurious    │     │ IRQ 15: Secondary ATA   │
 *   └─────────────────────────┘     └─────────────────────────┘
 *
 * Why Remapping is Necessary:
 *
 *   In real mode, the BIOS maps IRQs 0-7 to interrupt vectors 8-15.
 *   This conflicts with x86 CPU exceptions (which use vectors 0-31).
 *   For example, IRQ 0 (timer) would conflict with Double Fault (#8).
 *
 *   We remap the PICs so:
 *     - IRQ 0-7  -> INT 32-39 (master PIC)
 *     - IRQ 8-15 -> INT 40-47 (slave PIC)
 *
 *   This moves hardware interrupts out of the CPU exception range.
 *
 * Interrupt Flow:
 *
 *   1. Hardware device asserts its IRQ line
 *   2. PIC raises INTR pin to CPU
 *   3. CPU acknowledges (INTA) and gets vector number from PIC
 *   4. CPU looks up handler in IDT and executes it
 *   5. Handler must send EOI to PIC before returning (or no more IRQs!)
 *
 * Note: Modern systems use the APIC instead of the 8259 PIC, but the PIC
 * is simpler and sufficient for learning and basic hardware support.
 */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* =============================================================================
 * PIC API Functions
 * =============================================================================
 */

/*
 * pic_init - Initialize and remap both PICs.
 *
 * Performs the ICW (Initialization Command Word) sequence:
 *   ICW1: Start initialization, announce ICW4 will be sent
 *   ICW2: Set interrupt vector offset (32 for master, 40 for slave)
 *   ICW3: Configure master/slave cascade (slave on IRQ2)
 *   ICW4: Set 8086 mode (vs MCS-80/85 mode)
 *
 * After initialization, all IRQs are masked (disabled) by default.
 * Use pic_unmask() to enable specific IRQs.
 */
void pic_init(void);

/*
 * pic_send_eoi - Send End-of-Interrupt signal to acknowledge an IRQ.
 *
 * @irq: The IRQ number (0-15) that was handled
 *
 * CRITICAL: This MUST be called at the end of every IRQ handler!
 * If you forget to send EOI, the PIC will not deliver any more
 * interrupts of that priority or lower.
 *
 * For IRQs 8-15 (slave PIC), EOI must be sent to BOTH PICs.
 */
void pic_send_eoi(uint8_t irq);

/*
 * pic_unmask - Enable a specific IRQ (allow it to trigger interrupts).
 *
 * @irq: The IRQ number (0-15) to enable
 *
 * Clears the corresponding bit in the PIC's Interrupt Mask Register (IMR).
 * A masked (disabled) IRQ is ignored by the PIC.
 */
void pic_unmask(uint8_t irq);

/*
 * pic_mask - Disable a specific IRQ (prevent it from triggering interrupts).
 *
 * @irq: The IRQ number (0-15) to disable
 *
 * Sets the corresponding bit in the PIC's Interrupt Mask Register (IMR).
 * Useful for temporarily disabling an IRQ or preventing spurious interrupts.
 */
void pic_mask(uint8_t irq);

#endif /* PIC_H */