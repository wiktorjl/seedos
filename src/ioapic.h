/*
 * ioapic.h - I/O APIC (Advanced Programmable Interrupt Controller)
 *
 * The I/O APIC handles external interrupts from devices (keyboard, disk, etc.)
 * and routes them to Local APICs on CPUs. It replaces the legacy 8259 PIC.
 *
 * Each I/O APIC has 24 redirection entries that map device IRQs to interrupt
 * vectors and specify delivery options (destination CPU, trigger mode, etc.).
 */

#ifndef IOAPIC_H
#define IOAPIC_H

#include "types.h"

/* =============================================================================
 * I/O APIC Register Access
 *
 * The I/O APIC uses indirect register access:
 *   - Write register index to IOREGSEL (offset 0x00)
 *   - Read/write data from IOWIN (offset 0x10)
 * =============================================================================
 */

#define IOAPIC_REGSEL       0x00    /* Register Select (index) */
#define IOAPIC_WIN          0x10    /* Register Window (data) */

/* =============================================================================
 * I/O APIC Registers (accessed via REGSEL/WIN)
 * =============================================================================
 */

#define IOAPIC_ID           0x00    /* I/O APIC ID */
#define IOAPIC_VER          0x01    /* I/O APIC Version */
#define IOAPIC_ARB          0x02    /* Arbitration ID */
#define IOAPIC_REDTBL_BASE  0x10    /* Redirection Table base (entries at 0x10-0x3F) */

/* =============================================================================
 * Redirection Table Entry Format (64 bits)
 *
 * Each entry controls how one IRQ is delivered:
 *   Bits 0-7:   Vector (interrupt vector number, 0x10-0xFE)
 *   Bits 8-10:  Delivery Mode (000=Fixed, 001=Lowest Priority, etc.)
 *   Bit 11:     Destination Mode (0=Physical, 1=Logical)
 *   Bit 12:     Delivery Status (read-only, 0=idle, 1=pending)
 *   Bit 13:     Pin Polarity (0=active high, 1=active low)
 *   Bit 14:     Remote IRR (read-only, for level-triggered)
 *   Bit 15:     Trigger Mode (0=edge, 1=level)
 *   Bit 16:     Mask (0=enabled, 1=masked/disabled)
 *   Bits 56-63: Destination (APIC ID in physical mode)
 * =============================================================================
 */

#define IOAPIC_DELIVERY_FIXED       (0 << 8)
#define IOAPIC_DELIVERY_LOWEST      (1 << 8)
#define IOAPIC_DELIVERY_SMI         (2 << 8)
#define IOAPIC_DELIVERY_NMI         (4 << 8)
#define IOAPIC_DELIVERY_INIT        (5 << 8)
#define IOAPIC_DELIVERY_EXTINT      (7 << 8)

#define IOAPIC_DESTMODE_PHYSICAL    (0 << 11)
#define IOAPIC_DESTMODE_LOGICAL     (1 << 11)

#define IOAPIC_POLARITY_HIGH        (0 << 13)
#define IOAPIC_POLARITY_LOW         (1 << 13)

#define IOAPIC_TRIGGER_EDGE         (0 << 15)
#define IOAPIC_TRIGGER_LEVEL        (1 << 15)

#define IOAPIC_MASKED               (1 << 16)

/* Destination APIC ID is in bits 56-63 (high 32 bits, shifted left 24) */
#define IOAPIC_DEST(apic_id)        ((uint64_t)(apic_id) << 56)

/* =============================================================================
 * ISA IRQ Numbers
 * =============================================================================
 */

#define ISA_IRQ_TIMER       0
#define ISA_IRQ_KEYBOARD    1
#define ISA_IRQ_CASCADE     2
#define ISA_IRQ_COM2        3
#define ISA_IRQ_COM1        4
#define ISA_IRQ_LPT2        5
#define ISA_IRQ_FLOPPY      6
#define ISA_IRQ_LPT1        7
#define ISA_IRQ_RTC         8
#define ISA_IRQ_MOUSE       12
#define ISA_IRQ_ATA1        14
#define ISA_IRQ_ATA2        15

/* =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * ioapic_init - Initialize the I/O APIC.
 *
 * Maps the I/O APIC registers and masks all interrupts.
 * Must be called after ACPI init (needs I/O APIC address from MADT).
 */
void ioapic_init(void);

/*
 * ioapic_route_irq - Route an ISA IRQ to a CPU.
 *
 * @irq:    ISA IRQ number (0-15)
 * @vector: Interrupt vector to deliver (32-254)
 * @apic_id: Destination Local APIC ID
 *
 * Configures the redirection entry for the specified IRQ.
 * Uses edge-triggered, active-high, fixed delivery by default.
 */
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t apic_id);

/*
 * ioapic_mask_irq - Mask (disable) an IRQ.
 *
 * @irq: ISA IRQ number (0-15)
 */
void ioapic_mask_irq(uint8_t irq);

/*
 * ioapic_unmask_irq - Unmask (enable) an IRQ.
 *
 * @irq: ISA IRQ number (0-15)
 */
void ioapic_unmask_irq(uint8_t irq);

#endif /* IOAPIC_H */
