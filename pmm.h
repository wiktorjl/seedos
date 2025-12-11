/*
 * pmm.h - Physical Memory Manager
 *
 * Bitmap-based allocator for physical page frames.
 * Each bit represents one 4KB page: 0 = free, 1 = used.
 */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

#define PAGE_SIZE 4096

/*
 * Initialize the physical memory manager.
 * Must be called with the memory map and HHDM offset from Limine.
 */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/*
 * Allocate a single physical page.
 * Returns the physical address of the page, or 0 on failure.
 */
uint64_t pmm_alloc(void);

/*
 * Free a previously allocated physical page.
 */
void pmm_free(uint64_t phys_addr);

/*
 * Get statistics about memory usage.
 */
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

#endif /* PMM_H */
