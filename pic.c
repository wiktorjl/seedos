/*
 * pic.c - 8259 Programmable Interrupt Controller
 */

#include "pic.h"
#include "io.h"

/* PIC ports */
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

/* PIC commands */
#define ICW1_INIT    0x10  /* Initialization command */
#define ICW1_ICW4    0x01  /* ICW4 needed */
#define ICW4_8086    0x01  /* 8086/88 mode */
#define PIC_EOI      0x20  /* End of interrupt */

/* I/O helpers provided by io.h */

void pic_init(void) {
    /* Save current masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* Start initialization sequence (ICW1) */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: Set vector offsets */
    outb(PIC1_DATA, 32);   /* Master PIC: IRQ 0-7 -> INT 32-39 */
    io_wait();
    outb(PIC2_DATA, 40);   /* Slave PIC: IRQ 8-15 -> INT 40-47 */
    io_wait();

    /* ICW3: Tell PICs about each other */
    outb(PIC1_DATA, 4);    /* Master: slave is on IRQ2 (bit 2) */
    io_wait();
    outb(PIC2_DATA, 2);    /* Slave: cascade identity (IRQ2) */
    io_wait();

    /* ICW4: Set mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Restore masks (all masked initially) */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    /* If IRQ came from slave PIC, send EOI to both */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_unmask(uint8_t irq) {
    uint16_t port;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    uint8_t mask = inb(port);
    mask &= ~(1 << irq);  /* Clear the bit to unmask */
    outb(port, mask);
}

void pic_mask(uint8_t irq) {
    uint16_t port;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    uint8_t mask = inb(port);
    mask |= (1 << irq);  /* Set the bit to mask */
    outb(port, mask);
}