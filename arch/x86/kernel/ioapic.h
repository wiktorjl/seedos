/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * I/O APIC definitions
 */

#ifndef _IOAPIC_H
#define _IOAPIC_H

#include "types.h"

/* I/O APIC indirect register access ports */
#define IOAPIC_REGSEL       0x00
#define IOAPIC_WIN          0x10

/* I/O APIC registers */
#define IOAPIC_ID           0x00
#define IOAPIC_VER          0x01
#define IOAPIC_ARB          0x02
#define IOAPIC_REDTBL_BASE  0x10

/* Redirection entry bits - see Intel SDM Vol 3, section 10.11.1 */
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

#define IOAPIC_DEST(apic_id)        ((uint64_t)(apic_id) << 56)

/* ISA IRQ numbers */
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

/**
 * ioapic_init - Initialize the I/O APIC
 *
 * Maps registers and masks all interrupts. Must be called after acpi_init().
 */
void ioapic_init(void);

/**
 * ioapic_route_irq - Route an ISA IRQ to a CPU
 * @irq: ISA IRQ number (0-15)
 * @vector: interrupt vector to deliver (32-254)
 * @apic_id: destination Local APIC ID
 */
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t apic_id);

/**
 * ioapic_mask_irq - Mask (disable) an IRQ
 * @irq: IRQ number
 */
void ioapic_mask_irq(uint8_t irq);

/**
 * ioapic_unmask_irq - Unmask (enable) an IRQ
 * @irq: IRQ number
 */
void ioapic_unmask_irq(uint8_t irq);

#endif /* _IOAPIC_H */
