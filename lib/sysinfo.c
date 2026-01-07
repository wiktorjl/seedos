#include "sysinfo.h"
#include "acpi.h"
#include "pmm.h"
#include "heap.h"
#include "kprintf.h"

static sysinfo_t sysinfo;

static void format_bytes(uint64_t bytes, uint64_t *value, const char **unit) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        *value = bytes / (1024ULL * 1024 * 1024);
        *unit = "GB";
    } else if (bytes >= 1024ULL * 1024) {
        *value = bytes / (1024ULL * 1024);
        *unit = "MB";
    } else if (bytes >= 1024) {
        *value = bytes / 1024;
        *unit = "KB";
    } else {
        *value = bytes;
        *unit = "B";
    }
}

void sysinfo_init(void) {
    acpi_info_t *acpi = acpi_get_info();

    sysinfo.cpu_count = acpi->cpu_count;
    sysinfo.total_memory_bytes = pmm_get_usable_pages() * PAGE_SIZE;
    sysinfo.free_memory_bytes = pmm_get_free_pages() * PAGE_SIZE;
    sysinfo.heap_free_bytes = kheap_get_free();
}

sysinfo_t *sysinfo_get(void) {
    return &sysinfo;
}

void sysinfo_print_summary(void) {
    uint64_t total_val, free_val, heap_val;
    const char *total_unit, *free_unit, *heap_unit;

    format_bytes(sysinfo.total_memory_bytes, &total_val, &total_unit);
    format_bytes(sysinfo.free_memory_bytes, &free_val, &free_unit);
    format_bytes(sysinfo.heap_free_bytes, &heap_val, &heap_unit);

    kprintf("\n");
    kprintf("================================================================================\n");
    kprintf("                            SeedOS System Summary\n");
    kprintf("================================================================================\n");
    kprintf("  CPU:  %d processor(s)\n", sysinfo.cpu_count);
    kprintf("  RAM:  %llu %s total, %llu %s free\n", total_val, total_unit, free_val, free_unit);
    kprintf("  Heap: %llu %s available\n", heap_val, heap_unit);
    kprintf("================================================================================\n");
    kprintf("\n");
}
