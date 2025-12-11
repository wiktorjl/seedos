/*
 * pic.c - 8259 Programmable Interrupt Controller (PIC)
 *
 * This file implements the driver for the 8259 PIC, which handles hardware
 * interrupts from devices like the keyboard, timer, and disk controllers.
 *
 * The 8259 PIC Initialization Sequence:
 *
 *   The PIC requires a specific 4-byte initialization sequence called
 *   ICW1-ICW4 (Initialization Command Words). The sequence must be sent
 *   to BOTH the master and slave PICs.
 *
 *   Step 1: ICW1 (to command port)
 *     - Bit 4 = 1: Initialization mode
 *     - Bit 0 = 1: ICW4 will be sent
 *
 *   Step 2: ICW2 (to data port)
 *     - Sets the interrupt vector offset
 *     - Master: 32 (IRQs 0-7 -> INT 32-39)
 *     - Slave: 40 (IRQs 8-15 -> INT 40-47)
 *
 *   Step 3: ICW3 (to data port)
 *     - Master: Bitmask of which IRQ lines have slaves (bit 2 = IRQ2)
 *     - Slave: Its cascade identity (2 = connected to master's IRQ2)
 *
 *   Step 4: ICW4 (to data port)
 *     - Bit 0 = 1: 8086 mode (vs ancient 8080 mode)
 *
 * After initialization, the IMR (Interrupt Mask Register) controls which
 * IRQs are enabled. A set bit means masked (disabled).
 */

#include "pic.h"
#include "io.h"

/* =============================================================================
 * PIC I/O Port Addresses
 *
 * Each PIC has two ports: command and data.
 * Command port: Used for ICW1, EOI, and OCW commands
 * Data port: Used for ICW2-4 and reading/writing the mask register
 * =============================================================================
 */
#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA    0x21
#define PIC_SLAVE_COMMAND  0xA0
#define PIC_SLAVE_DATA     0xA1

/* =============================================================================
 * PIC Initialization Command Words (ICW)
 * =============================================================================
 */
#define ICW1_INIT    0x10  /* Bit 4: Start initialization sequence */
#define ICW1_ICW4    0x01  /* Bit 0: ICW4 will be sent */
#define ICW4_8086    0x01  /* Bit 0: 8086/88 mode (vs 8080 mode) */

/* =============================================================================
 * PIC Operation Command Words (OCW)
 * =============================================================================
 */
#define OCW2_EOI     0x20  /* Non-specific End-of-Interrupt command */

/* =============================================================================
 * Interrupt Vector Offsets
 *
 * These are the interrupt vector numbers where we remap the PICs.
 * Chosen to avoid conflict with CPU exceptions (0-31).
 * =============================================================================
 */
#define PIC_MASTER_OFFSET 32  /* IRQ 0-7  -> INT 32-39 */
#define PIC_SLAVE_OFFSET  40  /* IRQ 8-15 -> INT 40-47 */

/* =============================================================================
 * Cascade Configuration
 *
 * The slave PIC is connected to the master's IRQ2 line.
 * =============================================================================
 */
#define PIC_CASCADE_IRQ       2  /* Slave is on master's IRQ2 */
#define PIC_MASTER_CASCADE    4  /* Bit 2 set = slave on IRQ2 (bitmask) */
#define PIC_SLAVE_CASCADE_ID  2  /* Slave's cascade identity */

/* Number of IRQs per PIC chip */
#define IRQS_PER_PIC 8

/*
 * pic_init - Initialize and remap both PICs.
 *
 * Sends the ICW1-ICW4 sequence to both PICs to configure them.
 * This must be done before enabling interrupts.
 */
void pic_init(void) {
    /*
     * Save current interrupt masks.
     * During initialization, we'll lose the current mask state,
     * so we save and restore it to preserve any BIOS settings.
     */
    uint8_t saved_master_mask = inb(PIC_MASTER_DATA);
    uint8_t saved_slave_mask = inb(PIC_SLAVE_DATA);

    /*
     * ICW1: Start initialization sequence.
     * Writing to command port with bit 4 set triggers init mode.
     * Bit 0 set means we'll send ICW4.
     */
    outb(PIC_MASTER_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC_SLAVE_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /*
     * ICW2: Set interrupt vector offsets.
     * This is where we remap the PICs to avoid the CPU exception range.
     */
    outb(PIC_MASTER_DATA, PIC_MASTER_OFFSET);
    io_wait();
    outb(PIC_SLAVE_DATA, PIC_SLAVE_OFFSET);
    io_wait();

    /*
     * ICW3: Configure cascade (master/slave connection).
     * Master: Bitmask indicating which IRQ has a slave (IRQ2 = bit 2 = 4)
     * Slave: Its cascade identity number (2 = connected to master's IRQ2)
     */
    outb(PIC_MASTER_DATA, PIC_MASTER_CASCADE);
    io_wait();
    outb(PIC_SLAVE_DATA, PIC_SLAVE_CASCADE_ID);
    io_wait();

    /*
     * ICW4: Set operating mode.
     * We need 8086 mode for x86 processors (vs ancient 8080 mode).
     */
    outb(PIC_MASTER_DATA, ICW4_8086);
    io_wait();
    outb(PIC_SLAVE_DATA, ICW4_8086);
    io_wait();

    /*
     * Restore the saved interrupt masks.
     * After init, all IRQs would be enabled - we restore original state.
     */
    outb(PIC_MASTER_DATA, saved_master_mask);
    outb(PIC_SLAVE_DATA, saved_slave_mask);
}

/*
 * pic_send_eoi - Acknowledge an interrupt to the PIC.
 *
 * The PIC keeps track of which interrupt is being serviced. Until we
 * send EOI, it won't deliver lower-priority interrupts. For slave PIC
 * IRQs (8-15), we must send EOI to BOTH PICs because the slave's
 * interrupt goes through the master's IRQ2.
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= IRQS_PER_PIC) {
        /* IRQ came from slave PIC - acknowledge slave first */
        outb(PIC_SLAVE_COMMAND, OCW2_EOI);
    }
    /* Always acknowledge master (slave IRQs cascade through master) */
    outb(PIC_MASTER_COMMAND, OCW2_EOI);
}

/*
 * pic_unmask - Enable an IRQ by clearing its mask bit.
 *
 * The IMR (Interrupt Mask Register) uses inverted logic:
 *   - Bit = 0: IRQ is enabled (unmasked)
 *   - Bit = 1: IRQ is disabled (masked)
 */
void pic_unmask(uint8_t irq) {
    uint16_t port;
    uint8_t irq_bit;

    if (irq < IRQS_PER_PIC) {
        port = PIC_MASTER_DATA;
        irq_bit = irq;
    } else {
        port = PIC_SLAVE_DATA;
        irq_bit = irq - IRQS_PER_PIC;
    }

    uint8_t mask = inb(port);
    mask &= ~(1 << irq_bit);  /* Clear bit to enable IRQ */
    outb(port, mask);
}

/*
 * pic_mask - Disable an IRQ by setting its mask bit.
 *
 * Masked IRQs are completely ignored by the PIC.
 * This is useful for:
 *   - Disabling unused IRQs to prevent spurious interrupts
 *   - Temporarily blocking an IRQ during critical sections
 */
void pic_mask(uint8_t irq) {
    uint16_t port;
    uint8_t irq_bit;

    if (irq < IRQS_PER_PIC) {
        port = PIC_MASTER_DATA;
        irq_bit = irq;
    } else {
        port = PIC_SLAVE_DATA;
        irq_bit = irq - IRQS_PER_PIC;
    }

    uint8_t mask = inb(port);
    mask |= (1 << irq_bit);  /* Set bit to disable IRQ */
    outb(port, mask);
}