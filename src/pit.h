/*
 * pit.h - 8253/8254 Programmable Interval Timer (PIT)
 *
 * The PIT is a hardware timer that generates periodic interrupts. It's one
 * of the oldest PC peripherals, dating back to the original IBM PC. We use
 * it as our system tick source for timekeeping and preemptive scheduling.
 *
 * PIT Channels:
 *
 *   Channel 0: System timer (IRQ 0) - We use this one
 *   Channel 1: DRAM refresh (obsolete)
 *   Channel 2: PC speaker tone generation
 *
 * How the PIT Works:
 *
 *   The PIT has an internal oscillator running at 1.193182 MHz (the base
 *   frequency). A 16-bit counter counts down from a "divisor" value. When
 *   it reaches zero, an interrupt is generated and the counter reloads.
 *
 *   Interrupt frequency = Base frequency / Divisor
 *
 *   Example: 1193182 / 11932 ≈ 100 Hz (interrupt every 10ms)
 *
 * Operating Modes (selected via command byte):
 *
 *   Mode 0: Interrupt on terminal count (one-shot)
 *   Mode 1: Hardware retriggerable one-shot
 *   Mode 2: Rate generator (divide-by-N counter)
 *   Mode 3: Square wave generator - We use this (symmetric output)
 *   Mode 4: Software triggered strobe
 *   Mode 5: Hardware triggered strobe
 *
 * Command Byte Format (written to port 0x43):
 *
 *   7   6   5   4   3  2  1  0
 *   ┌───┬───┬───┬───┬──┬──┬──┬───┐
 *   │SC1 SC0│RW1 RW0│M2 M1 M0│BCD│
 *   └───┴───┴───┴───┴──┴──┴──┴───┘
 *
 *   SC (Select Channel): 00=Ch0, 01=Ch1, 10=Ch2, 11=Read-back
 *   RW (Read/Write):     00=Latch, 01=LSB only, 10=MSB only, 11=LSB then MSB
 *   M  (Mode):           000-101 for modes 0-5
 *   BCD:                 0=Binary, 1=BCD (we use binary)
 *
 * Usage:
 *   1. pit_init() - Configure PIT for 100 Hz interrupts
 *   2. PIT generates IRQ 0 -> interrupt 32 (after PIC remapping)
 *   3. pit_handler() increments tick count and sends EOI
 *   4. pit_get_ticks() returns elapsed ticks since boot
 */

#ifndef PIT_H
#define PIT_H

#include "types.h"

/* =============================================================================
 * PIT Hardware Constants
 * =============================================================================
 */

/*
 * PIT_FREQUENCY - Base oscillator frequency in Hz.
 *
 * This is the famous 1.193182 MHz frequency, derived from the original
 * IBM PC's 14.31818 MHz crystal divided by 12. This odd value exists
 * for historical compatibility with NTSC color TV timing.
 */
#define PIT_FREQUENCY 1193182

/*
 * PIT_DEFAULT_HZ - Desired interrupt frequency (100 Hz = 10ms per tick).
 *
 * 100 Hz is a good balance between timer precision and interrupt overhead.
 * Higher values give finer time resolution but more CPU time spent handling
 * timer interrupts.
 */
#define PIT_DEFAULT_HZ 100

/*
 * PIT I/O Ports:
 *   - Channel 0 data port: Read/write the 16-bit counter value
 *   - Command port: Write the mode/command byte
 */
#define PIT_DATA_PORT    0x40  /* Channel 0 data port */
#define PIT_COMMAND_PORT 0x43  /* Mode/command register */

/*
 * PIT_DIVISOR - Counter reload value to achieve PIT_DEFAULT_HZ.
 *
 * Calculated as: base_frequency / desired_frequency
 * For 100 Hz: 1193182 / 100 = 11931.82 ≈ 11932
 */
#define PIT_DIVISOR (PIT_FREQUENCY / PIT_DEFAULT_HZ)

/* =============================================================================
 * PIT Command Byte Bits
 *
 * These bits are OR'd together to form the command byte for port 0x43.
 * =============================================================================
 */
#define PIT_COMMAND_BITS_CHANNEL0    0x00  /* Select channel 0 (bits 7-6 = 00) */
#define PIT_COMMAND_BITS_ACCESS_LOHI 0x30  /* Access mode: LSB then MSB (bits 5-4 = 11) */
#define PIT_COMMAND_BITS_MODE3       0x06  /* Mode 3: Square wave generator (bits 3-1 = 011) */
#define PIT_COMMAND_BITS_BINARY      0x00  /* Binary counting mode (bit 0 = 0) */

/*
 * PIT_COMMAND_RESULT - Complete command byte for our configuration.
 *
 * Channel 0, LSB/MSB access, Mode 3 (square wave), Binary counting.
 * Value: 0x00 | 0x30 | 0x06 | 0x00 = 0x36
 */
#define PIT_COMMAND_RESULT (PIT_COMMAND_BITS_CHANNEL0 | PIT_COMMAND_BITS_ACCESS_LOHI | PIT_COMMAND_BITS_MODE3 | PIT_COMMAND_BITS_BINARY)

/* =============================================================================
 * PIT API Functions
 * =============================================================================
 */

/*
 * pit_init - Initialize the PIT for periodic interrupts.
 *
 * Configures channel 0 in mode 3 (square wave) with the divisor
 * calculated to generate PIT_DEFAULT_HZ interrupts per second.
 * Must be called after PIC initialization.
 */
void pit_init(void);

/*
 * pit_get_ticks - Get the number of timer ticks since boot.
 *
 * Returns: The current tick count (each tick = 1/PIT_DEFAULT_HZ seconds).
 *
 * At 100 Hz, this overflows after ~5.8 billion years, so no worries there.
 */
uint64_t pit_get_ticks(void);

/*
 * pit_handler - Timer interrupt handler (called from ISR).
 *
 * Increments the tick counter. The ISR wrapper handles saving/restoring
 * registers and sending EOI to the PIC.
 */
void pit_handler(void);

#endif /* PIT_H */
