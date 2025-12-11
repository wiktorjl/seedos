/*
 * memory.h - Memory address conversion helpers
 *
 * Provides physical-to-virtual and virtual-to-physical address
 * conversion using the Higher Half Direct Map (HHDM).
 *
 * The HHDM maps all physical memory starting at a fixed virtual
 * address offset, allowing the kernel to easily access any
 * physical address by adding this offset.
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

/*
 * HHDM offset - must be set during initialization.
 * Provided by Limine bootloader.
 */
extern uint64_t g_hhdm_offset;

/*
 * Convert a physical address to a virtual address using HHDM.
 */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + g_hhdm_offset);
}

/*
 * Convert a virtual address (in HHDM range) to a physical address.
 */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt - g_hhdm_offset;
}

#endif /* MEMORY_H */
