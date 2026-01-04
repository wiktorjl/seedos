/*
 * ioapic.c - I/O APIC Driver
 *
 * Configures the I/O APIC to route external device interrupts to CPUs.
 */

#include "ioapic.h"
#include "acpi.h"
#include "vmm.h"
#include "log.h"

/* =============================================================================
 * I/O APIC State
 * =============================================================================
 */

static volatile uint32_t *ioapic_base;  /* Virtual address of I/O APIC registers */
static uint8_t ioapic_max_entry;        /* Maximum redirection entry (usually 23) */

/* =============================================================================
 * I/O APIC Register Access
 * =============================================================================
 */

static uint32_t ioapic_read(uint32_t reg) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_WIN / 4];
}

static void ioapic_write(uint32_t reg, uint32_t value) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_WIN / 4] = value;
}

/*
 * Read a 64-bit redirection entry.
 * Each entry spans two 32-bit registers.
 */
static uint64_t ioapic_read_redir(uint8_t entry) {
    uint32_t lo = ioapic_read(IOAPIC_REDTBL_BASE + entry * 2);
    uint32_t hi = ioapic_read(IOAPIC_REDTBL_BASE + entry * 2 + 1);
    return ((uint64_t)hi << 32) | lo;
}

/*
 * Write a 64-bit redirection entry.
 */
static void ioapic_write_redir(uint8_t entry, uint64_t value) {
    ioapic_write(IOAPIC_REDTBL_BASE + entry * 2, (uint32_t)value);
    ioapic_write(IOAPIC_REDTBL_BASE + entry * 2 + 1, (uint32_t)(value >> 32));
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

void ioapic_init(void) {
    acpi_info_t *acpi = acpi_get_info();

    /*
     * Map the I/O APIC registers into virtual memory.
     */
    uint64_t ioapic_phys = acpi->io_apic_address;
    uint64_t pml4 = vmm_get_kernel_pml4();

    /* Map at a fixed virtual address in kernel space */
    uint64_t ioapic_virt = 0xFFFFFFFD00001000ULL;

    int result = vmm_map_page(pml4, ioapic_virt, ioapic_phys,
                               PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);
    if (result != 0) {
        log_error("IOAPIC: Failed to map registers");
        return;
    }

    ioapic_base = (volatile uint32_t *)ioapic_virt;

    /* Read version register to get max redirection entries */
    uint32_t ver = ioapic_read(IOAPIC_VER);
    ioapic_max_entry = (ver >> 16) & 0xFF;

    log_debug("IOAPIC: Mapped at phys 0x%llx, virt 0x%llx",
              ioapic_phys, ioapic_virt);
    log_debug("IOAPIC: Version 0x%02x, max entries: %d",
              ver & 0xFF, ioapic_max_entry + 1);

    /* Mask all interrupts initially */
    for (int i = 0; i <= ioapic_max_entry; i++) {
        uint64_t entry = ioapic_read_redir(i);
        entry |= IOAPIC_MASKED;
        ioapic_write_redir(i, entry);
    }

    log_info("IOAPIC: %d entries", ioapic_max_entry + 1);
}

/*
 * TODO: This function should translate IRQ to GSI using ACPI overrides.
 *
 * Currently callers pass ISA IRQ numbers directly, assuming GSI = IRQ.
 * This works on most systems but fails when ACPI specifies overrides.
 *
 * Options to fix:
 * 1. Add irq_to_gsi() helper that checks acpi_info.overrides[]
 * 2. Call it here: uint8_t gsi = irq_to_gsi(irq);
 * 3. Also apply polarity/trigger flags from the override entry
 *
 * The flags field in override entries specifies:
 * - Bits 0-1: Polarity (0=default, 1=active high, 3=active low)
 * - Bits 2-3: Trigger (0=default, 1=edge, 3=level)
 */
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t apic_id) {
    if (irq > ioapic_max_entry) {
        log_error("IOAPIC: IRQ %d exceeds max entry %d", irq, ioapic_max_entry);
        return;
    }

    /*
     * Configure redirection entry:
     * - Fixed delivery mode
     * - Physical destination mode
     * - Active high polarity (ISA default)
     * - Edge triggered (ISA default)
     * - Not masked (enabled)
     */
    uint64_t entry = vector
                   | IOAPIC_DELIVERY_FIXED
                   | IOAPIC_DESTMODE_PHYSICAL
                   | IOAPIC_POLARITY_HIGH
                   | IOAPIC_TRIGGER_EDGE
                   | IOAPIC_DEST(apic_id);

    ioapic_write_redir(irq, entry);

    log_debug("IOAPIC: IRQ %d -> vector %d, APIC ID %d", irq, vector, apic_id);
}

void ioapic_mask_irq(uint8_t irq) {
    if (irq > ioapic_max_entry) return;

    uint64_t entry = ioapic_read_redir(irq);
    entry |= IOAPIC_MASKED;
    ioapic_write_redir(irq, entry);
}

void ioapic_unmask_irq(uint8_t irq) {
    if (irq > ioapic_max_entry) return;

    uint64_t entry = ioapic_read_redir(irq);
    entry &= ~IOAPIC_MASKED;
    ioapic_write_redir(irq, entry);
}
