/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Local APIC definitions and timer interface
 */

#ifndef _APIC_H
#define _APIC_H

#include "types.h"

/* Local APIC register offsets (16-byte aligned) */
#define LAPIC_ID            0x020
#define LAPIC_VERSION       0x030
#define LAPIC_TPR           0x080
#define LAPIC_APR           0x090
#define LAPIC_PPR           0x0A0
#define LAPIC_EOI           0x0B0
#define LAPIC_RRD           0x0C0
#define LAPIC_LDR           0x0D0
#define LAPIC_DFR           0x0E0
#define LAPIC_SVR           0x0F0
#define LAPIC_ISR           0x100
#define LAPIC_TMR           0x180
#define LAPIC_IRR           0x200
#define LAPIC_ESR           0x280
#define LAPIC_ICR_LOW       0x300
#define LAPIC_ICR_HIGH      0x310
#define LAPIC_LVT_TIMER     0x320
#define LAPIC_LVT_THERMAL   0x330
#define LAPIC_LVT_PERF      0x340
#define LAPIC_LVT_LINT0     0x350
#define LAPIC_LVT_LINT1     0x360
#define LAPIC_LVT_ERROR     0x370
#define LAPIC_TIMER_ICR     0x380
#define LAPIC_TIMER_CCR     0x390
#define LAPIC_TIMER_DCR     0x3E0

/* SVR bits */
#define LAPIC_SVR_ENABLE    (1 << 8)
#define LAPIC_SVR_VECTOR    0xFF

/* LVT timer bits */
#define LAPIC_TIMER_MASKED      (1 << 16)
#define LAPIC_TIMER_PERIODIC    (1 << 17)
#define LAPIC_TIMER_TSC_DEADLINE (2 << 17)

/* Timer divider values */
#define LAPIC_TIMER_DIV_1       0xB
#define LAPIC_TIMER_DIV_2       0x0
#define LAPIC_TIMER_DIV_4       0x1
#define LAPIC_TIMER_DIV_8       0x2
#define LAPIC_TIMER_DIV_16      0x3
#define LAPIC_TIMER_DIV_32      0x8
#define LAPIC_TIMER_DIV_64      0x9
#define LAPIC_TIMER_DIV_128     0xA

/* Interrupt vectors */
#define IRQ_TIMER           32
#define IRQ_SPURIOUS        255

#define TIMER_FREQUENCY_HZ  100

/**
 * apic_init - Initialize Local APIC and start timer
 *
 * Must be called after acpi_init(). Calibrates timer against PIT
 * and configures periodic interrupts at TIMER_FREQUENCY_HZ.
 */
void apic_init(void);

/**
 * apic_eoi - Signal End of Interrupt to Local APIC
 */
void apic_eoi(void);

/**
 * apic_timer_handler - Handle APIC timer interrupt
 */
void apic_timer_handler(void);

/**
 * apic_get_ticks - Get tick count since boot
 *
 * Return: Number of timer interrupts
 */
uint64_t apic_get_ticks(void);

/**
 * apic_get_id - Get Local APIC ID of current CPU
 *
 * Return: APIC ID
 */
uint32_t apic_get_id(void);

#endif /* _APIC_H */
