/*
 * acpi.c - ACPI Table Parser
 *
 * Parses ACPI tables to discover hardware configuration including
 * CPU topology, APIC addresses, and interrupt routing.
 */

/* =============================================================================
 * Includes
 * =============================================================================
 */

#include "acpi.h"
#include "limine.h"
#include "log.h"
#include "vmm.h"
#include "pmm.h"

/* =============================================================================
 * Module State
 * =============================================================================
 */

static acpi_info_t acpi_info;

/*
 * Virtual address region for ACPI mappings.
 * We use a fixed region in kernel space for mapping ACPI tables.
 * This is separate from the kernel heap to avoid conflicts.
 */
#define ACPI_VIRT_BASE  0xFFFFFFFE00000000ULL
static uint64_t acpi_virt_next = ACPI_VIRT_BASE;

/* =============================================================================
 * Internal Helper Functions
 * =============================================================================
 */

/*
 * Map a physical memory region to kernel virtual address space.
 * Used for ACPI tables that aren't in the HHDM with Limine revision 3.
 *
 * @phys: Physical address to map (will be page-aligned down)
 * @size: Size of the region in bytes
 *
 * Returns: Virtual address corresponding to the physical address,
 *          or NULL on failure.
 */
static void *acpi_map_region(uint64_t phys, size_t size) {
    uint64_t pml4 = vmm_get_kernel_pml4();

    /* Calculate page-aligned boundaries */
    uint64_t phys_base = phys & ~(PAGE_SIZE - 1);
    uint64_t offset = phys - phys_base;
    size_t pages_needed = (offset + size + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Allocate virtual address range */
    uint64_t virt_base = acpi_virt_next;
    acpi_virt_next += pages_needed * PAGE_SIZE;

    /* Map each page */
    for (size_t i = 0; i < pages_needed; i++) {
        uint64_t virt = virt_base + i * PAGE_SIZE;
        uint64_t phys_page = phys_base + i * PAGE_SIZE;

        int result = vmm_map_page(pml4, virt, phys_page,
                                   PTE_PRESENT | PTE_WRITABLE);
        if (result != 0) {
            log_error("ACPI: Failed to map physical 0x%llx", phys_page);
            return NULL;
        }
    }

    /* Return virtual address with original offset preserved */
    return (void *)(virt_base + offset);
}

static int acpi_checksum(void *ptr, uint32_t length) {
    uint8_t sum = 0;
    uint8_t *bytes = (uint8_t *)ptr;
    for (uint32_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

static int sig_match(const char *a, const char *b) {
    return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]);
}

static void parse_madt(madt_t *madt) {
    acpi_info.local_apic_address = madt->local_apic_address;
    acpi_info.has_pic = madt->flags & 1;

    log_debug("MADT: Local APIC at 0x%08x, PIC present: %d",
              madt->local_apic_address, acpi_info.has_pic);

    /* Parse MADT entries */
    uint8_t *entry_ptr = (uint8_t *)(madt + 1);
    uint8_t *end = (uint8_t *)madt + madt->header.length;

    while (entry_ptr < end) {
        madt_entry_header_t *header = (madt_entry_header_t *)entry_ptr;

        switch (header->type) {
            case MADT_ENTRY_LOCAL_APIC: {
                madt_local_apic_t *lapic = (madt_local_apic_t *)entry_ptr;
                if (lapic->flags & 1) {  /* Processor enabled */
                    if (acpi_info.cpu_count < 256) {
                        acpi_info.cpu_apic_ids[acpi_info.cpu_count++] = lapic->apic_id;
                        log_debug("  CPU %d: APIC ID %d",
                                  acpi_info.cpu_count - 1, lapic->apic_id);
                    }
                }
                break;
            }
            case MADT_ENTRY_IO_APIC: {
                madt_io_apic_t *ioapic = (madt_io_apic_t *)entry_ptr;
                acpi_info.io_apic_address = ioapic->io_apic_address;
                acpi_info.io_apic_id = ioapic->io_apic_id;
                acpi_info.io_apic_gsi_base = ioapic->global_system_interrupt_base;
                log_debug("  I/O APIC: ID %d at 0x%08x, GSI base %d",
                          ioapic->io_apic_id, ioapic->io_apic_address,
                          ioapic->global_system_interrupt_base);
                break;
            }
            case MADT_ENTRY_INTERRUPT_OVERRIDE: {
                madt_interrupt_override_t *ovr = (madt_interrupt_override_t *)entry_ptr;
                log_debug("  IRQ Override: IRQ %d -> GSI %d",
                          ovr->irq_source, ovr->global_system_interrupt);
                break;
            }
            case MADT_ENTRY_LOCAL_APIC_NMI: {
                madt_local_apic_nmi_t *nmi = (madt_local_apic_nmi_t *)entry_ptr;
                log_debug("  Local APIC NMI: processor %d, LINT%d",
                          nmi->acpi_processor_id, nmi->lint);
                break;
            }
            case MADT_ENTRY_LOCAL_APIC_OVERRIDE: {
                madt_local_apic_override_t *ovr = (madt_local_apic_override_t *)entry_ptr;
                acpi_info.local_apic_address = ovr->local_apic_address;
                log_debug("  Local APIC Override: 0x%016llx", ovr->local_apic_address);
                break;
            }
        }

        entry_ptr += header->length;
        if (header->length == 0) break;  /* Prevent infinite loop */
    }
}

static acpi_sdt_header_t *find_table(void *root_sdt, const char *signature, int use_xsdt) {
    if (use_xsdt) {
        xsdt_t *xsdt = (xsdt_t *)root_sdt;
        int entries = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / sizeof(uint64_t);
        for (int i = 0; i < entries; i++) {
            uint64_t table_phys = xsdt->tables[i];
            if (table_phys == 0) continue;  /* Skip invalid entries */

            /* Map header first to check signature and get length */
            acpi_sdt_header_t *header = (acpi_sdt_header_t *)acpi_map_region(
                table_phys, sizeof(acpi_sdt_header_t));
            if (header == NULL) continue;

            if (sig_match(header->signature, signature)) {
                /* Map the full table */
                return (acpi_sdt_header_t *)acpi_map_region(table_phys, header->length);
            }
        }
    } else {
        rsdt_t *rsdt = (rsdt_t *)root_sdt;
        int entries = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / sizeof(uint32_t);
        for (int i = 0; i < entries; i++) {
            uint32_t table_phys = rsdt->tables[i];
            if (table_phys == 0) continue;  /* Skip invalid entries */

            /* Map header first to check signature and get length */
            acpi_sdt_header_t *header = (acpi_sdt_header_t *)acpi_map_region(
                table_phys, sizeof(acpi_sdt_header_t));
            if (header == NULL) continue;

            if (sig_match(header->signature, signature)) {
                /* Map the full table */
                return (acpi_sdt_header_t *)acpi_map_region(table_phys, header->length);
            }
        }
    }
    return NULL;
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

int acpi_init(void) {
    /*
     * Get RSDP from Limine.
     * Note: Limine base revision 3 returns a physical address.
     * With revision 3, ACPI regions are NOT in the HHDM, so we must
     * manually map them before access.
     */
    void *rsdp_phys = limine_get_rsdp();
    log_debug("ACPI: RSDP physical address: %p", rsdp_phys);
    if (rsdp_phys == NULL) {
        log_error("ACPI: RSDP not found");
        return -1;
    }

    /* Map the RSDP (need enough for rsdp2_t which is larger) */
    rsdp_t *rsdp = (rsdp_t *)acpi_map_region((uint64_t)rsdp_phys, sizeof(rsdp2_t));
    if (rsdp == NULL) {
        log_error("ACPI: Failed to map RSDP");
        return -1;
    }

    /* Verify RSDP signature */
    if (rsdp->signature[0] != 'R' || rsdp->signature[1] != 'S' ||
        rsdp->signature[2] != 'D' || rsdp->signature[3] != ' ' ||
        rsdp->signature[4] != 'P' || rsdp->signature[5] != 'T' ||
        rsdp->signature[6] != 'R' || rsdp->signature[7] != ' ') {
        log_error("ACPI: Invalid RSDP signature");
        return -1;
    }

    /*
     * Verify RSDP checksum.
     * ACPI 1.0 RSDP checksum covers exactly the first 20 bytes.
     */
    if (!acpi_checksum(rsdp, 20)) {
        log_error("ACPI: RSDP checksum failed");
        return -1;
    }

    log_debug("ACPI: RSDP found, revision %d", rsdp->revision);

    void *root_sdt;
    int use_xsdt = 0;
    uint64_t root_sdt_phys;

    if (rsdp->revision >= 2) {
        /* ACPI 2.0+ - use XSDT */
        rsdp2_t *rsdp2 = (rsdp2_t *)rsdp;
        if (!acpi_checksum(rsdp2, rsdp2->length)) {
            log_error("ACPI: Extended RSDP checksum failed");
            return -1;
        }
        root_sdt_phys = rsdp2->xsdt_address;
        use_xsdt = 1;
        log_debug("ACPI: Using XSDT at 0x%016llx", rsdp2->xsdt_address);
    } else {
        /* ACPI 1.0 - use RSDT */
        root_sdt_phys = rsdp->rsdt_address;
        log_debug("ACPI: Using RSDT at 0x%08x", rsdp->rsdt_address);
    }

    /*
     * First map just the header to read the table length,
     * then map the full table.
     */
    acpi_sdt_header_t *root_header = (acpi_sdt_header_t *)acpi_map_region(
        root_sdt_phys, sizeof(acpi_sdt_header_t));
    if (root_header == NULL) {
        log_error("ACPI: Failed to map %s header", use_xsdt ? "XSDT" : "RSDT");
        return -1;
    }

    /* Now map the full table based on length from header */
    root_sdt = acpi_map_region(root_sdt_phys, root_header->length);
    if (root_sdt == NULL) {
        log_error("ACPI: Failed to map full %s", use_xsdt ? "XSDT" : "RSDT");
        return -1;
    }
    root_header = (acpi_sdt_header_t *)root_sdt;

    if (!acpi_checksum(root_sdt, root_header->length)) {
        log_error("ACPI: %s checksum failed", use_xsdt ? "XSDT" : "RSDT");
        return -1;
    }

    /* Find and parse MADT - find_table returns mapped virtual address */
    madt_t *madt = (madt_t *)find_table(root_sdt, "APIC", use_xsdt);
    if (madt == NULL) {
        log_error("ACPI: MADT not found");
        return -1;
    }

    if (!acpi_checksum(madt, madt->header.length)) {
        log_error("ACPI: MADT checksum failed");
        return -1;
    }

    parse_madt(madt);

    log_info("ACPI: Found %d CPU(s), Local APIC at 0x%llx, I/O APIC at 0x%llx",
             acpi_info.cpu_count, acpi_info.local_apic_address,
             acpi_info.io_apic_address);

    return 0;
}

acpi_info_t *acpi_get_info(void) {
    return &acpi_info;
}
