/*
 * apic.h - Local APIC (Advanced Programmable Interrupt Controller)
 *
 * The Local APIC is the per-CPU interrupt controller in modern x86 systems.
 * Each CPU has its own Local APIC that handles:
 *   - Local interrupts (timer, thermal, performance counters)
 *   - Inter-processor interrupts (IPIs)
 *   - Interrupt delivery from the I/O APIC
 *
 * The APIC timer is a per-CPU timer that can generate periodic or one-shot
 * interrupts. It's essential for preemptive multitasking and timekeeping.
 */

#ifndef APIC_H
#define APIC_H

#include "types.h"

/* =============================================================================
 * Local APIC Register Offsets
 *
 * The Local APIC is memory-mapped starting at the address from MADT.
 * Each register is 32 bits wide but aligned on 16-byte boundaries.
 * =============================================================================
 */

#define LAPIC_ID            0x020   /* Local APIC ID */
#define LAPIC_VERSION       0x030   /* Local APIC Version */
#define LAPIC_TPR           0x080   /* Task Priority Register */
#define LAPIC_APR           0x090   /* Arbitration Priority Register */
#define LAPIC_PPR           0x0A0   /* Processor Priority Register */
#define LAPIC_EOI           0x0B0   /* End of Interrupt */
#define LAPIC_RRD           0x0C0   /* Remote Read Register */
#define LAPIC_LDR           0x0D0   /* Logical Destination Register */
#define LAPIC_DFR           0x0E0   /* Destination Format Register */
#define LAPIC_SVR           0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ISR           0x100   /* In-Service Register (8 x 32-bit) */
#define LAPIC_TMR           0x180   /* Trigger Mode Register (8 x 32-bit) */
#define LAPIC_IRR           0x200   /* Interrupt Request Register (8 x 32-bit) */
#define LAPIC_ESR           0x280   /* Error Status Register */
#define LAPIC_ICR_LOW       0x300   /* Interrupt Command Register (low) */
#define LAPIC_ICR_HIGH      0x310   /* Interrupt Command Register (high) */
#define LAPIC_LVT_TIMER     0x320   /* LVT Timer Register */
#define LAPIC_LVT_THERMAL   0x330   /* LVT Thermal Sensor Register */
#define LAPIC_LVT_PERF      0x340   /* LVT Performance Counter Register */
#define LAPIC_LVT_LINT0     0x350   /* LVT LINT0 Register */
#define LAPIC_LVT_LINT1     0x360   /* LVT LINT1 Register */
#define LAPIC_LVT_ERROR     0x370   /* LVT Error Register */
#define LAPIC_TIMER_ICR     0x380   /* Timer Initial Count Register */
#define LAPIC_TIMER_CCR     0x390   /* Timer Current Count Register */
#define LAPIC_TIMER_DCR     0x3E0   /* Timer Divide Configuration Register */

/* =============================================================================
 * Spurious Vector Register (SVR) bits
 * =============================================================================
 */

#define LAPIC_SVR_ENABLE    (1 << 8)    /* APIC Software Enable */
#define LAPIC_SVR_VECTOR    0xFF        /* Spurious interrupt vector (usually 0xFF) */

/* =============================================================================
 * LVT Timer Register bits
 * =============================================================================
 */

#define LAPIC_TIMER_MASKED      (1 << 16)   /* Interrupt masked */
#define LAPIC_TIMER_PERIODIC    (1 << 17)   /* Periodic mode (vs one-shot) */
#define LAPIC_TIMER_TSC_DEADLINE (2 << 17)  /* TSC-deadline mode */

/* =============================================================================
 * Timer Divide Configuration Register values
 *
 * The timer counts down from ICR at a rate of (bus clock / divider).
 * These values set the divider.
 * =============================================================================
 */

#define LAPIC_TIMER_DIV_1       0xB     /* Divide by 1 */
#define LAPIC_TIMER_DIV_2       0x0     /* Divide by 2 */
#define LAPIC_TIMER_DIV_4       0x1     /* Divide by 4 */
#define LAPIC_TIMER_DIV_8       0x2     /* Divide by 8 */
#define LAPIC_TIMER_DIV_16      0x3     /* Divide by 16 */
#define LAPIC_TIMER_DIV_32      0x8     /* Divide by 32 */
#define LAPIC_TIMER_DIV_64      0x9     /* Divide by 64 */
#define LAPIC_TIMER_DIV_128     0xA     /* Divide by 128 */

/* =============================================================================
 * Interrupt Vector Assignments
 *
 * Vectors 0-31 are reserved for CPU exceptions.
 * Vectors 32-255 are available for hardware interrupts.
 * =============================================================================
 */

#define IRQ_TIMER           32      /* APIC timer interrupt vector */
#define IRQ_SPURIOUS        255     /* Spurious interrupt vector */

/* =============================================================================
 * Timer Configuration
 * =============================================================================
 */

#define TIMER_FREQUENCY_HZ  100     /* Target timer frequency (100 Hz = 10ms ticks) */

/* =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * apic_init - Initialize the Local APIC and timer.
 *
 * Must be called after ACPI init (needs APIC base address from MADT).
 * Enables the APIC, calibrates the timer, and starts periodic interrupts.
 */
void apic_init(void);

/*
 * apic_eoi - Signal End of Interrupt to the Local APIC.
 *
 * Must be called at the end of every APIC interrupt handler.
 * Writing any value to the EOI register signals completion.
 */
void apic_eoi(void);

/*
 * apic_timer_handler - Handle APIC timer interrupt.
 *
 * Called from the interrupt handler when vector IRQ_TIMER fires.
 * Updates tick count and performs any periodic tasks.
 */
void apic_timer_handler(void);

/*
 * apic_get_ticks - Get the current tick count.
 *
 * Returns the number of timer interrupts since boot.
 * At 100 Hz, each tick is 10ms.
 */
uint64_t apic_get_ticks(void);

/*
 * apic_get_id - Get the Local APIC ID of the current CPU.
 *
 * Returns the APIC ID, which identifies the CPU in multi-processor systems.
 */
uint32_t apic_get_id(void);

#endif /* APIC_H */
