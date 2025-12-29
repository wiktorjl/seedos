/*
 * acpi.h - ACPI Table Definitions
 *
 * Advanced Configuration and Power Interface structures.
 */

#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

/* RSDP (Root System Description Pointer) - ACPI 1.0 */
typedef struct {
    char signature[8];      /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;       /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;
} __attribute__((packed)) rsdp_t;

/* RSDP for ACPI 2.0+ */
typedef struct {
    rsdp_t first_part;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) rsdp2_t;

/* Standard ACPI table header */
typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

/* RSDT (Root System Description Table) - 32-bit pointers */
typedef struct {
    acpi_sdt_header_t header;
    uint32_t tables[];      /* Array of 32-bit physical addresses */
} __attribute__((packed)) rsdt_t;

/* XSDT (Extended System Description Table) - 64-bit pointers */
typedef struct {
    acpi_sdt_header_t header;
    uint64_t tables[];      /* Array of 64-bit physical addresses */
} __attribute__((packed)) xsdt_t;

/* MADT (Multiple APIC Description Table) header */
typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;         /* bit 0: dual 8259 PICs installed */
} __attribute__((packed)) madt_t;

/* MADT entry types */
#define MADT_ENTRY_LOCAL_APIC           0
#define MADT_ENTRY_IO_APIC              1
#define MADT_ENTRY_INTERRUPT_OVERRIDE   2
#define MADT_ENTRY_NMI_SOURCE           3
#define MADT_ENTRY_LOCAL_APIC_NMI       4
#define MADT_ENTRY_LOCAL_APIC_OVERRIDE  5
#define MADT_ENTRY_IO_SAPIC             6
#define MADT_ENTRY_LOCAL_SAPIC          7
#define MADT_ENTRY_PLATFORM_INT         8
#define MADT_ENTRY_LOCAL_X2APIC         9
#define MADT_ENTRY_LOCAL_X2APIC_NMI     10

/* MADT entry header (common to all entries) */
typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) madt_entry_header_t;

/* MADT Local APIC entry (type 0) */
typedef struct {
    madt_entry_header_t header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;         /* bit 0: processor enabled, bit 1: online capable */
} __attribute__((packed)) madt_local_apic_t;

/* MADT I/O APIC entry (type 1) */
typedef struct {
    madt_entry_header_t header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) madt_io_apic_t;

/* MADT Interrupt Source Override entry (type 2) */
typedef struct {
    madt_entry_header_t header;
    uint8_t bus_source;     /* always 0 (ISA) */
    uint8_t irq_source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed)) madt_interrupt_override_t;

/* MADT Local APIC NMI entry (type 4) */
typedef struct {
    madt_entry_header_t header;
    uint8_t acpi_processor_id;  /* 0xFF means all processors */
    uint16_t flags;
    uint8_t lint;               /* LINT# (0 or 1) */
} __attribute__((packed)) madt_local_apic_nmi_t;

/* MADT Local APIC Address Override entry (type 5) */
typedef struct {
    madt_entry_header_t header;
    uint16_t reserved;
    uint64_t local_apic_address;
} __attribute__((packed)) madt_local_apic_override_t;

/* Parsed ACPI information */
typedef struct {
    uint64_t local_apic_address;
    uint64_t io_apic_address;
    uint8_t io_apic_id;
    uint32_t io_apic_gsi_base;
    int cpu_count;
    uint8_t cpu_apic_ids[256];
    int has_pic;
} acpi_info_t;

/* =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * acpi_init - Parse ACPI tables and extract hardware information.
 *
 * Returns: 0 on success, -1 on failure (ACPI not available or parse error).
 *
 * Locates the RSDP, parses the RSDT/XSDT, and extracts Local APIC, I/O APIC,
 * and CPU information from the MADT.
 */
int acpi_init(void);

/*
 * acpi_get_info - Get parsed ACPI information.
 *
 * Returns: Pointer to global acpi_info_t structure.
 *
 * Only valid after acpi_init() returns success.
 */
acpi_info_t *acpi_get_info(void);

#endif /* ACPI_H */
