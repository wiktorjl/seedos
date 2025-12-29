/*
 * apic.c - Local APIC Timer Implementation
 *
 * Implements a periodic timer using the Local APIC's built-in timer.
 * The timer is calibrated against a known time source (PIT) to determine
 * the bus frequency, then configured to generate periodic interrupts.
 */

#include "apic.h"
#include "acpi.h"
#include "vmm.h"
#include "log.h"
#include "idt.h"
#include "console.h"

/* =============================================================================
 * PIT (Programmable Interval Timer) for Calibration
 *
 * We use the PIT briefly during calibration because it runs at a known
 * frequency (1.193182 MHz). After calibration, the PIT is not used.
 * =============================================================================
 */

#define PIT_CHANNEL0_DATA   0x40
#define PIT_COMMAND         0x43
#define PIT_FREQUENCY       1193182     /* PIT runs at 1.193182 MHz */

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* =============================================================================
 * Local APIC State
 * =============================================================================
 */

static volatile uint32_t *lapic_base;   /* Virtual address of LAPIC registers */
static volatile uint64_t tick_count;    /* Number of timer interrupts since boot */
static uint32_t timer_initial_count;    /* Calibrated initial count for timer */

/*
 * Timer interrupt handler wrapper.
 * Called from the IDT dispatch when vector IRQ_TIMER fires.
 */
static void timer_irq_handler(interrupt_frame_t *frame) {
    (void)frame;  /* Unused for now */
    apic_timer_handler();
}

/* =============================================================================
 * LAPIC Register Access
 * =============================================================================
 */

static inline uint32_t lapic_read(uint32_t reg) {
    return lapic_base[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    lapic_base[reg / 4] = value;
    /* Read back to ensure write completes (memory barrier) */
    (void)lapic_base[LAPIC_ID / 4];
}

/* =============================================================================
 * PIT Sleep for Calibration
 *
 * Sleeps for approximately the given number of milliseconds using the PIT.
 * Used only during timer calibration.
 * =============================================================================
 */

static void pit_sleep_ms(uint32_t ms) {
    /*
     * Configure PIT channel 0 in mode 0 (interrupt on terminal count).
     * We'll poll the count to wait for the specified time.
     *
     * Command byte: 0x30 = channel 0, lobyte/hibyte, mode 0, binary
     */
    uint32_t count = (PIT_FREQUENCY * ms) / 1000;
    if (count > 65535) count = 65535;  /* Max 16-bit count */

    outb(PIT_COMMAND, 0x30);
    outb(PIT_CHANNEL0_DATA, count & 0xFF);
    outb(PIT_CHANNEL0_DATA, (count >> 8) & 0xFF);

    /*
     * Poll the PIT count. When it reaches 0, the time has elapsed.
     * Read command: 0x00 = latch count for channel 0
     */
    while (1) {
        outb(PIT_COMMAND, 0x00);  /* Latch count */
        uint8_t lo = inb(PIT_CHANNEL0_DATA);
        uint8_t hi = inb(PIT_CHANNEL0_DATA);
        uint16_t current = lo | (hi << 8);
        if (current == 0 || current > count) break;  /* Count reached 0 or wrapped */
    }
}

/* =============================================================================
 * Timer Calibration
 *
 * Determines how many APIC timer ticks occur per millisecond.
 * We run the timer for a known duration (measured by PIT) and count ticks.
 * =============================================================================
 */

static uint32_t calibrate_timer(void) {
    /*
     * Set up the APIC timer:
     * - Divide by 16 (reasonable resolution without overflow)
     * - One-shot mode (not periodic yet)
     * - Start with maximum count
     */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_MASKED);  /* Masked during calibration */
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);          /* Maximum initial count */

    /* Wait 10ms using PIT */
    pit_sleep_ms(10);

    /* Read how many ticks elapsed */
    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);

    /* Stop the timer */
    lapic_write(LAPIC_TIMER_ICR, 0);

    /*
     * Calculate ticks per target period.
     * elapsed = ticks in 10ms
     * We want TIMER_FREQUENCY_HZ interrupts per second.
     * Period = 1000ms / TIMER_FREQUENCY_HZ
     *
     * ticks_per_period = (elapsed * 100) / TIMER_FREQUENCY_HZ
     *                  = elapsed * (1000 / 10) / TIMER_FREQUENCY_HZ
     *                  = elapsed * 100 / TIMER_FREQUENCY_HZ
     */
    uint32_t ticks_per_period = (elapsed * 100) / TIMER_FREQUENCY_HZ;

    log_debug("APIC: Timer calibration: %u ticks in 10ms", elapsed);
    log_debug("APIC: Ticks per %dHz period: %u", TIMER_FREQUENCY_HZ, ticks_per_period);

    return ticks_per_period;
}

/* =============================================================================
 * Disable Legacy PIC
 *
 * The 8259 PIC must be disabled when using the APIC to avoid conflicts.
 * We mask all interrupts on both PICs.
 * =============================================================================
 */

static void disable_pic(void) {
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

/* =============================================================================
 * Public API
 * =============================================================================
 */

void apic_init(void) {
    acpi_info_t *acpi = acpi_get_info();

    /*
     * Map the Local APIC registers into virtual memory.
     * The APIC is at a fixed physical address (usually 0xFEE00000).
     */
    uint64_t lapic_phys = acpi->local_apic_address;
    uint64_t pml4 = vmm_get_kernel_pml4();

    /*
     * Map LAPIC at a fixed virtual address in kernel space.
     * Use uncacheable memory (PTE_NOCACHE) for MMIO.
     */
    uint64_t lapic_virt = 0xFFFFFFFD00000000ULL;  /* Fixed virtual address for LAPIC */

    int result = vmm_map_page(pml4, lapic_virt, lapic_phys,
                               PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);
    if (result != 0) {
        log_error("APIC: Failed to map LAPIC registers");
        return;
    }

    lapic_base = (volatile uint32_t *)lapic_virt;
    log_debug("APIC: Local APIC at phys 0x%llx, virt 0x%llx", lapic_phys, lapic_virt);

    /* Disable the legacy 8259 PIC */
    disable_pic();

    /*
     * Enable the Local APIC.
     * Set the spurious interrupt vector and enable bit.
     */
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

    /* Register the timer interrupt handler */
    idt_register_irq(IRQ_TIMER, timer_irq_handler);

    /*
     * Configure the timer for periodic mode.
     * - Vector: IRQ_TIMER (32)
     * - Mode: Periodic
     * - Not masked
     */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, IRQ_TIMER | LAPIC_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_ICR, timer_initial_count);

    tick_count = 0;

    log_info("APIC: Timer running at %d Hz (vector %d)", TIMER_FREQUENCY_HZ, IRQ_TIMER);
}

void apic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

void apic_timer_handler(void) {
    tick_count++;

    /* Update blinking cursor */
    console_update_cursor(tick_count);

    /* Signal end of interrupt */
    apic_eoi();
}

uint64_t apic_get_ticks(void) {
    return tick_count;
}

uint32_t apic_get_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}
