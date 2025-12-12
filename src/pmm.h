/*
 * pmm.h - Physical Memory Manager (PMM)
 *
 * The PMM manages physical RAM using a bitmap allocator. It tracks which
 * 4KB pages of physical memory are free or in use.
 *
 * Key concepts:
 *   - Physical address: The actual hardware address in RAM
 *   - Page frame: A 4KB-aligned block of physical memory
 *   - Bitmap: Data structure where each bit represents one page (0=free, 1=used)
 *
 * Why we need a PMM:
 *   The CPU and processes work with virtual addresses, but we need actual
 *   physical RAM to back those addresses. The PMM keeps track of which
 *   physical pages are available so the VMM can map them into address spaces.
 *
 * Usage flow:
 *   1. pmm_init() - Called once at boot with the memory map from bootloader
 *   2. pmm_alloc() - Returns a free physical page (e.g., for a new page table)
 *   3. vmm_map_page() - Maps that physical page to a virtual address
 *   4. pmm_free() - Returns the page to the free pool when no longer needed
 */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

/* Page size in bytes (4KB). This is the smallest unit we allocate. */
#define PAGE_SIZE 4096

/*
 * pmm_init - Initialize the physical memory manager.
 *
 * @memmap:      Memory map from Limine describing usable/reserved regions
 * @hhdm_offset: Higher Half Direct Map offset for physical memory access
 *
 * This function:
 *   1. Scans the memory map to find total physical memory
 *   2. Allocates a bitmap to track all pages
 *   3. Marks usable regions as free, everything else as used
 *   4. Reserves page 0 (so NULL dereferences fault)
 */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/*
 * pmm_alloc - Allocate a single physical page (4KB).
 *
 * Returns: Physical address of the allocated page, or 0 if out of memory.
 *
 * The returned address is page-aligned (multiple of 4096).
 * The page contents are NOT zeroed - caller must zero if needed.
 */
uint64_t pmm_alloc(void);

/*
 * pmm_free - Return a physical page to the free pool.
 *
 * @phys_addr: Physical address of the page to free (must be page-aligned)
 *
 * Double-free is silently ignored. Freeing page 0 is ignored.
 */
void pmm_free(uint64_t phys_addr);

/*
 * pmm_get_free_pages - Get the count of free physical pages.
 */
uint64_t pmm_get_free_pages(void);

/*
 * pmm_get_total_pages - Get the total count of tracked physical pages.
 * Note: This is the bitmap size, not actual RAM (includes holes for MMIO).
 */
uint64_t pmm_get_total_pages(void);

/*
 * pmm_get_usable_pages - Get the total count of usable RAM pages.
 * This is the sum of all USABLE regions from the memory map.
 */
uint64_t pmm_get_usable_pages(void);

#endif /* PMM_H */
