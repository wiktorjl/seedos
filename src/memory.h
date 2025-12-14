/*
 * memory.h - Physical/Virtual Address Conversion via HHDM
 *
 * This header provides helper functions for converting between physical
 * and virtual addresses using the Higher Half Direct Map (HHDM).
 *
 * What is HHDM?
 *
 *   The Higher Half Direct Map is a region of virtual address space where
 *   ALL of physical memory is linearly mapped. If the HHDM starts at
 *   virtual address 0xFFFF800000000000, then:
 *
 *     - Physical address 0x0000 maps to virtual 0xFFFF800000000000
 *     - Physical address 0x1000 maps to virtual 0xFFFF800000001000
 *     - Physical address P maps to virtual (P + HHDM_offset)
 *
 *   This is set up by the bootloader (Limine) before the kernel starts.
 *
 * Why HHDM Matters:
 *
 *   Page tables, memory management structures, and device memory all have
 *   PHYSICAL addresses. But the CPU operates in virtual addressing mode.
 *   To read or modify physical memory, the kernel needs a virtual address.
 *
 *   Without HHDM, you'd need to temporarily map each physical page before
 *   accessing it. With HHDM, any physical address P is always accessible
 *   at virtual address (P + g_hhdm_offset).
 *
 * Memory Layout:
 *
 *   Virtual Address Space:
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │  0xFFFFFFFFFFFFFFFF                                         │
 *   │  ├── Kernel Code/Data                                       │
 *   │  │   (higher-half kernel, typically 0xFFFFFFFF80000000)     │
 *   │  │                                                          │
 *   │  ├── HHDM Region                                            │
 *   │  │   (maps all physical RAM, typically 0xFFFF800000000000)  │
 *   │  │   phys_to_virt(P) = P + g_hhdm_offset                    │
 *   │  │                                                          │
 *   │  ├── (canonical hole - invalid addresses)                   │
 *   │  │                                                          │
 *   │  └── User Space (lower half, 0x0000000000000000)            │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * Usage Example:
 *
 *   // Allocate a physical page
 *   uint64_t phys_page = pmm_alloc();
 *
 *   // Get a pointer we can actually use
 *   uint64_t *virt_ptr = phys_to_virt(phys_page);
 *
 *   // Now we can read/write the page
 *   virt_ptr[0] = 0x12345678;
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

/* =============================================================================
 * System Limits
 * =============================================================================
 */

#define PATH_MAX 256            /* Maximum path length (Linux uses 4096) */
#define USER_SPACE_TOP 0x0000800000000000ULL

/* =============================================================================
 * HHDM Offset
 *
 * This global variable holds the virtual address where physical address 0
 * is mapped. It's set during PMM initialization from Limine's HHDM response.
 * =============================================================================
 */

/*
 * g_hhdm_offset - Base virtual address of the Higher Half Direct Map.
 *
 * Defined in pmm.c and set during pmm_init().
 * Typical value: 0xFFFF800000000000 (Limine default)
 */
extern uint64_t g_hhdm_offset;

/* =============================================================================
 * Address Conversion Functions
 * =============================================================================
 */

/*
 * phys_to_virt - Convert a physical address to a virtual address.
 *
 * @phys: Physical address to convert.
 *
 * Returns: Virtual address that maps to the given physical address.
 *
 * Use this when you have a physical address (from page tables, PMM, etc.)
 * and need a pointer you can dereference.
 */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + g_hhdm_offset);
}

/*
 * virt_to_phys - Convert a virtual address (in HHDM) to physical.
 *
 * @virt: Virtual address within the HHDM region.
 *
 * Returns: Corresponding physical address.
 *
 * Note: This only works for addresses in the HHDM region!
 * It won't work for kernel code/data or user-space addresses.
 */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt - g_hhdm_offset;
}

#endif /* MEMORY_H */
