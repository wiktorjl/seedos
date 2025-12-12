/*
 * pmm.c - Physical Memory Manager
 *
 * Implements a bitmap-based allocator for physical page frames.
 *
 * Bitmap allocator explained:
 *   We use a large array of bits where each bit represents one 4KB page.
 *   - Bit = 0: Page is free and can be allocated
 *   - Bit = 1: Page is in use (allocated or reserved)
 *
 *   For example, with 4GB of RAM:
 *     - 4GB / 4KB = 1,048,576 pages to track
 *     - 1,048,576 bits / 8 = 131,072 bytes (128KB) for the bitmap
 *
 *   To find which bit represents physical address P:
 *     page_index = P / PAGE_SIZE
 *     byte_index = page_index / 8
 *     bit_index  = page_index % 8
 *
 * Trade-offs:
 *   + Simple to implement and understand
 *   + Constant memory overhead regardless of fragmentation
 *   - Linear scan for allocation is O(n) - could use free list instead
 *   - No support for allocating contiguous ranges efficiently
 */

#include "pmm.h"
#include "memory.h"

/*
 * Global HHDM (Higher Half Direct Map) offset.
 *
 * This is THE key value for physical memory access in a higher-half kernel.
 * To access physical address P, we use virtual address (P + g_hhdm_offset).
 *
 * Set during pmm_init() and used by phys_to_virt() in memory.h.
 * Declared extern in memory.h so all modules can use it.
 */
uint64_t g_hhdm_offset;

/* =============================================================================
 * PMM Global State
 * =============================================================================
 */

static uint8_t *bitmap;           /* Pointer to bitmap array (virtual address) */
static uint64_t bitmap_size;      /* Size of bitmap in bytes */
static uint64_t total_pages;      /* Total number of pages being tracked */
static uint64_t free_pages;       /* Number of currently free pages */
static uint64_t usable_pages;     /* Total usable RAM (sum of usable regions) */

/* =============================================================================
 * Bitmap Manipulation Helpers
 *
 * These operate on individual bits within the bitmap array.
 * page_index / 8 = which byte
 * page_index % 8 = which bit within that byte
 * =============================================================================
 */

/*
 * bitmap_mark_used - Set a bit to 1 (mark page as allocated/reserved).
 */
static inline void bitmap_mark_used(uint64_t page_index) {
    bitmap[page_index / 8] |= (1 << (page_index % 8));
}

/*
 * bitmap_mark_free - Clear a bit to 0 (mark page as available).
 */
static inline void bitmap_mark_free(uint64_t page_index) {
    bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

/*
 * bitmap_is_used - Test if a page is currently in use.
 * Returns: Non-zero if used, 0 if free.
 */
static inline int bitmap_is_used(uint64_t page_index) {
    return bitmap[page_index / 8] & (1 << (page_index % 8));
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    g_hhdm_offset = hhdm_offset;

    /*
     * Step 1: Find the highest physical address to determine
     * how many pages we need to track.
     */
    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t entry_end = entry->base + entry->length;
        if (entry_end > highest_addr && entry->type == LIMINE_MEMMAP_USABLE) {
            highest_addr = entry_end;
        }
    }

    total_pages = (highest_addr + PAGE_SIZE - 1) / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;  /* Round up to nearest byte */

    /*
     * Calculate total usable RAM by summing all usable regions.
     * This is different from total_pages, which is just the tracking range.
     */
    usable_pages = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_pages += entry->length / PAGE_SIZE;
        }
    }

    /*
     * Step 2: Find a usable memory region large enough for the bitmap.
     * We need bitmap_size bytes, rounded up to a page boundary.
     */
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t bitmap_phys = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        if (entry->length >= bitmap_pages * PAGE_SIZE) {
            bitmap_phys = entry->base;
            break;
        }
    }

    /* If we couldn't find space, we're in trouble */
    if (bitmap_phys == 0) {
        /* Can't do much without serial here - just hang */
        while (1) {
            asm volatile ("hlt");
        }
    }

    /* Convert to virtual address via HHDM */
    bitmap = phys_to_virt(bitmap_phys);

    /*
     * Step 3: Initialize bitmap - mark ALL pages as used first.
     * This is safer: anything we don't explicitly mark free is used.
     */
    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;  /* All bits set = all pages used */
    }
    free_pages = 0;

    /*
     * Step 4: Walk the memory map and mark usable regions as free.
     */
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        /* Mark each page in this region as free */
        uint64_t base_page = entry->base / PAGE_SIZE;
        uint64_t page_count = entry->length / PAGE_SIZE;

        for (uint64_t p = 0; p < page_count; p++) {
            bitmap_mark_free(base_page + p);
            free_pages++;
        }
    }

    /*
     * Step 5: Mark the bitmap's own pages as used.
     * We already placed data there, so they're not free!
     */
    uint64_t bitmap_base_page = bitmap_phys / PAGE_SIZE;
    for (uint64_t p = 0; p < bitmap_pages; p++) {
        if (!bitmap_is_used(bitmap_base_page + p)) {
            bitmap_mark_used(bitmap_base_page + p);
            free_pages--;
        }
    }

    /*
     * Step 6: Mark page 0 as used (never allocate it).
     * NULL pointer dereferences should fault, not succeed.
     */
    if (!bitmap_is_used(0)) {
        bitmap_mark_used(0);
        free_pages--;
    }
}

uint64_t pmm_alloc(void) {
    /*
     * Linear scan for a free page.
     * This is O(n) which is inefficient for large memory, but simple.
     * A production allocator would use a free list or buddy system.
     */
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_is_used(i)) {
            bitmap_mark_used(i);
            free_pages--;
            return i * PAGE_SIZE;
        }
    }
    return 0;  /* Out of memory - no free pages found */
}

void pmm_free(uint64_t phys_addr) {
    uint64_t page_index = phys_addr / PAGE_SIZE;

    /* Validate: must be in range and currently allocated */
    if (page_index < total_pages && bitmap_is_used(page_index)) {
        bitmap_mark_free(page_index);
        free_pages++;
    }
}

uint64_t pmm_get_free_pages(void) {
    return free_pages;
}

uint64_t pmm_get_total_pages(void) {
    return total_pages;
}

uint64_t pmm_get_usable_pages(void) {
    return usable_pages;
}
