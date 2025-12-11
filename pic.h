/*
 * pic.h - 8259 Programmable Interrupt Controller
 */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/*
 * Initialize and remap the PICs.
 * Maps IRQ 0-7 to interrupts 32-39
 * Maps IRQ 8-15 to interrupts 40-47
 */
void pic_init(void);

/*
 * Send End-of-Interrupt signal.
 * Must be called at the end of every IRQ handler.
 */
void pic_send_eoi(uint8_t irq);

/*
 * Enable a specific IRQ (unmask it)
 */
void pic_unmask(uint8_t irq);

/*
 * Disable a specific IRQ (mask it)
 */
void pic_mask(uint8_t irq);

#endif /* PIC_H */