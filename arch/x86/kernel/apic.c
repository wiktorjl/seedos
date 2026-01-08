// SPDX-License-Identifier: GPL-2.0-only
/*
 * Local APIC timer driver
 */

#include "apic.h"
#include "acpi.h"
#include "vmm.h"
#include "log.h"
#include "idt.h"
#include "console.h"
#include "io.h"
#include "config.h"
#include "kthread.h"

/* PIT constants for APIC timer calibration */
#define PIT_CHANNEL0_DATA   0x40
#define PIT_COMMAND         0x43
#define PIT_FREQUENCY       1193182

static volatile uint32_t *lapic_base;
static volatile uint64_t tick_count;
static uint32_t timer_initial_count;

static void timer_irq_handler(interrupt_frame_t *frame)
{
	(void)frame;  /* Unused for now */
	apic_timer_handler();
}

static inline uint32_t lapic_read(uint32_t reg)
{
	return lapic_base[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t value)
{
	lapic_base[reg / 4] = value;
	/* Read back to ensure write completes (memory barrier) */
	(void)lapic_base[LAPIC_ID / 4];
}

/* Busy-wait using PIT for timer calibration */
static void pit_sleep_ms(uint32_t ms)
{
	/* PIT channel 0, mode 0 (terminal count), binary: 0x30 */
	uint32_t count = (PIT_FREQUENCY * ms) / 1000;
	if (count > 65535) count = 65535;  /* Max 16-bit count */

	outb(PIT_COMMAND, 0x30);
	outb(PIT_CHANNEL0_DATA, count & 0xFF);
	outb(PIT_CHANNEL0_DATA, (count >> 8) & 0xFF);

	/* Poll until count reaches 0 */
	while (1) {
		outb(PIT_COMMAND, 0x00);  /* Latch count */
		uint8_t lo = inb(PIT_CHANNEL0_DATA);
		uint8_t hi = inb(PIT_CHANNEL0_DATA);
		uint16_t current = lo | (hi << 8);
		if (current == 0 || current > count) break;  /* Count reached 0 or wrapped */
	}
}

/* Calibrate APIC timer against PIT reference */
static uint32_t calibrate_timer(void)
{
	/* One-shot mode, divide-by-16, max initial count */
	lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
	lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_MASKED);  /* Masked during calibration */
	lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);          /* Maximum initial count */

	/* Wait 10ms using PIT */
	pit_sleep_ms(10);

	/* Read how many ticks elapsed */
	uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);

	lapic_write(LAPIC_TIMER_ICR, 0);

	/* Scale 10ms measurement to target period */
	uint32_t ticks_per_period = (elapsed * 100) / TIMER_FREQUENCY_HZ;

	log_debug("APIC: Timer calibration: %u ticks in 10ms", elapsed);
	log_debug("APIC: Ticks per %dHz period: %u", TIMER_FREQUENCY_HZ, ticks_per_period);

	return ticks_per_period;
}

/* Disable 8259 PIC to avoid conflicts with APIC */
static void disable_pic(void)
{
	/*
	 * Remap PIC to vectors 32-47 first (even though we're disabling it).
	 * This prevents spurious interrupts from causing exceptions.
	 */

	/* ICW1: Start initialization sequence */
	outb(0x20, 0x11);  /* Master PIC command */
	outb(0xA0, 0x11);  /* Slave PIC command */

	/* ICW2: Vector offsets */
	outb(0x21, 0x20);  /* Master: IRQ 0-7 -> vectors 32-39 */
	outb(0xA1, 0x28);  /* Slave: IRQ 8-15 -> vectors 40-47 */

	/* ICW3: Cascade configuration */
	outb(0x21, 0x04);  /* Master: slave on IRQ2 */
	outb(0xA1, 0x02);  /* Slave: cascade identity */

	/* ICW4: 8086 mode */
	outb(0x21, 0x01);
	outb(0xA1, 0x01);

	/* Mask all interrupts on both PICs */
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);

	log_debug("APIC: Legacy PIC disabled");
}

/**
 * apic_init - Initialize Local APIC and start timer
 *
 * Must be called after acpi_init(). Calibrates timer against PIT
 * and configures periodic interrupts at TIMER_FREQUENCY_HZ.
 */
void apic_init(void)
{
	acpi_info_t *acpi = acpi_get_info();
	uint64_t lapic_phys = acpi->local_apic_address;
	uint64_t pml4 = vmm_get_kernel_pml4();

	/* Map LAPIC MMIO registers (uncached) */
	uint64_t lapic_virt = 0xFFFFFFFD00000000ULL;

	int result = vmm_map_page(pml4, lapic_virt, lapic_phys,
				   PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);
	if (result != 0) {
		log_error("APIC: Failed to map LAPIC registers");
		return;
	}

	lapic_base = (volatile uint32_t *)lapic_virt;
	log_debug("APIC: Local APIC at phys 0x%llx, virt 0x%llx", lapic_phys, lapic_virt);

	disable_pic();

	/* Enable LAPIC with spurious vector */
	lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | IRQ_SPURIOUS);

	/* Clear any pending errors */
	lapic_write(LAPIC_ESR, 0);
	lapic_write(LAPIC_ESR, 0);

	/* Calibrate the timer */
	timer_initial_count = calibrate_timer();
	if (timer_initial_count == 0) {
		log_error("APIC: Timer calibration failed");
		return;
	}

	idt_register_irq(IRQ_TIMER, timer_irq_handler);

	/* Configure periodic timer on vector IRQ_TIMER */
	lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
	lapic_write(LAPIC_LVT_TIMER, IRQ_TIMER | LAPIC_TIMER_PERIODIC);
	lapic_write(LAPIC_TIMER_ICR, timer_initial_count);

	tick_count = 0;

	log_info("APIC: Timer running at %d Hz (vector %d)", TIMER_FREQUENCY_HZ, IRQ_TIMER);
}

/**
 * apic_eoi - Signal End of Interrupt to Local APIC
 */
void apic_eoi(void)
{
	lapic_write(LAPIC_EOI, 0);
}

/**
 * apic_timer_handler - Handle APIC timer interrupt
 *
 * Updates tick count, blinks cursor, wakes sleeping threads,
 * and triggers preemptive scheduling if enabled.
 */
void apic_timer_handler(void)
{
	tick_count++;

	/* Update blinking cursor */
	console_update_cursor(tick_count);

	/* Signal end of interrupt BEFORE any context switch */
	apic_eoi();

	/* Only do threading operations if threading is initialized */
	if (kthread_current() != NULL) {
		/* Wake any sleeping threads whose time has expired */
		kthread_wake_sleepers();

#if CONFIG_KTHREAD_PREEMPTIVE
		/* Preemptive mode: force a context switch */
		kthread_schedule();
#endif
	}
}

/**
 * apic_get_ticks - Get tick count since boot
 *
 * Return: Number of timer interrupts (10ms per tick at 100Hz)
 */
uint64_t apic_get_ticks(void)
{
	return tick_count;
}

/**
 * apic_get_id - Get Local APIC ID of current CPU
 *
 * Return: APIC ID for multi-processor identification
 */
uint32_t apic_get_id(void)
{
	return lapic_read(LAPIC_ID) >> 24;
}
