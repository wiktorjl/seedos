/*
 * pit.c - Programmable Interval Timer Driver
 *
 * Implements the system tick timer using the 8254 PIT channel 0.
 * Generates interrupts at PIT_DEFAULT_HZ (100 Hz) for timekeeping.
 *
 * Initialization sequence:
 *   1. Send command byte to select channel 0, mode 3, binary
 *   2. Send 16-bit divisor (LSB first, then MSB)
 *   3. Unmask IRQ 0 to enable timer interrupts
 *
 * The tick counter is incremented by pit_handler() on each IRQ 0,
 * providing a monotonic time source for the kernel.
 */

#include "pit.h"
#include "io.h"
#include "pic.h"
#include "fb.h"

/*
 * Global tick counter - incremented by pit_handler() on each timer interrupt.
 *
 * Marked volatile because it's modified in interrupt context and read
 * from normal code. Without volatile, the compiler might cache the value
 * in a register and never see updates from the interrupt handler.
 */
static volatile uint64_t ticks = 0;

/*
 * pit_init - Configure the PIT and enable timer interrupts.
 *
 * Programs channel 0 for mode 3 (square wave) at ~100 Hz.
 * The divisor is sent LSB-first as required by the PIT.
 */
void pit_init(void) {
    /* Send command byte: channel 0, LSB/MSB access, mode 3, binary */
    outb(PIT_COMMAND_PORT, PIT_COMMAND_RESULT);

    /* Send divisor as two bytes (LSB first, then MSB) */
    outb(PIT_DATA_PORT, (uint8_t)(PIT_DIVISOR & 0xFF));
    outb(PIT_DATA_PORT, (uint8_t)((PIT_DIVISOR >> 8) & 0xFF));

    /* Enable IRQ 0 (timer) - must be done AFTER PIT is configured */
    pic_unmask(0);
}

/*
 * pit_get_ticks - Return current tick count.
 *
 * Each tick represents 1/PIT_DEFAULT_HZ seconds (10ms at 100 Hz).
 * Used for timekeeping, measuring intervals, and implementing sleep().
 */
uint64_t pit_get_ticks(void) {
    return ticks;
}

/*
 * pit_handler - Timer interrupt handler.
 *
 * Called from the interrupt dispatcher when IRQ 0 fires.
 * Simply increments the tick counter - keep it fast!
 * EOI is sent by the caller (interrupt_handler in idt.c).
 */
void pit_handler(void) {
    ticks++;
    fb_cursor_blink_tick(ticks);
}
