/*
 * pmm.h - Physical Memory Manager
 *
 * Manages physical page allocation using a bitmap allocator.
 * Each bit represents a 4KB page: 1 = used, 0 = free.
 */

#ifndef PMM_H
#define PMM_H

#include "limine.h"
#include "types.h"

#define PAGE_SIZE 4096
#define CODE_PAGE_SIZE PAGE_SIZE

#define PMM_ALLOC_FAILED 0  /* Returned when allocation fails (OOM) */

/*
 * pmm_init - Initialize the physical memory manager.
 *
 * @memmap:      Memory map from bootloader describing usable regions.
 * @hhdm_offset: Higher Half Direct Map offset for physical-to-virtual conversion.
 *
 * Allocates a bitmap to track all physical pages and marks usable regions
 * as free. Must be called before any other PMM functions.
 */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/*
 * pmm_alloc - Allocate a single physical page.
 *
 * Returns: Physical address of the allocated page, or PMM_ALLOC_FAILED (0)
 *          if no free pages are available.
 *
 * Uses linear scan (first-fit). The returned page is not zeroed.
 */
uint64_t pmm_alloc(void);

/*
 * pmm_free - Free a previously allocated physical page.
 *
 * @phys_addr: Physical address of the page to free (must be page-aligned).
 *
 * Double-free and invalid addresses are detected and logged.
 */
void pmm_free(uint64_t phys_addr);

/*
 * pmm_get_free_pages - Get the number of currently free pages.
 *
 * Returns: Count of unallocated pages.
 */
uint64_t pmm_get_free_pages(void);

/*
 * pmm_get_total_pages - Get the total number of tracked pages.
 *
 * Returns: Total pages in the bitmap (includes unusable regions).
 */
uint64_t pmm_get_total_pages(void);

/*
 * pmm_get_usable_pages - Get the total usable RAM in pages.
 *
 * Returns: Sum of all usable memory regions divided by page size.
 */
uint64_t pmm_get_usable_pages(void);

#endif /* PMM_H */
