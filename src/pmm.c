/*
 * pmm.c - Physical Memory Manager
 *
 * Manages physical memory allocation using a bitmap allocator.
 * Each bit represents a 4KB page: 1 = used, 0 = free.
 */

/* =============================================================================
 * Includes
 * =============================================================================
 */

#include "pmm.h"
#include "limine.h"
#include "log.h"
#include "memory.h"
#include "kprintf.h"

/* =============================================================================
 * Module State
 * =============================================================================
 */

uint64_t g_hhdm_offset;

static uint8_t *bitmap;           /* Pointer to bitmap array (virtual address) */
static uint64_t bitmap_size;      /* Size of bitmap in bytes */
static uint64_t total_pages;      /* Total number of pages being tracked */
static uint64_t free_pages;       /* Number of currently free pages */
static uint64_t usable_pages;     /* Total usable RAM (sum of usable regions) */

/* =============================================================================
 * Bitmap Helper Functions
 * =============================================================================
 */

static inline void bitmap_mark_used(uint64_t page_index) {
    bitmap[page_index / 8] |= (1 << (page_index % 8));
}


static inline void bitmap_mark_free(uint64_t page_index) {
    bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

static inline int bitmap_is_used(uint64_t page_index) {
    return bitmap[page_index / 8] & (1 << (page_index % 8));
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * pmm_init - Initialize the physical memory manager.
 *
 * @memmap:      Memory map from bootloader describing usable regions.
 * @hhdm_offset: Higher Half Direct Map offset for physical-to-virtual conversion.
 *
 * Allocates a bitmap to track all physical pages, marks usable regions as free,
 * and reserves page 0 (NULL page) and the bitmap's own pages.
 */
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
    int bitmap_found = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        if (entry->length >= bitmap_pages * PAGE_SIZE) {
            bitmap_phys = entry->base;
            bitmap_found = 1;
            break;  /* Use first suitable region */
        }
    }

    /* If we couldn't find space, we're in trouble */
    if (!bitmap_found) {
        /* Can't do much without serial here - just hang */
        while (1) {
            log_panic("PMM init failed: no space for bitmap");
            __asm__ volatile ("hlt");
        }
    } else {
        log_info("PMM: Bitmap allocated at physical address 0x%llx (%llu pages)", 
                 bitmap_phys, bitmap_pages);
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

/*
 * pmm_alloc - Allocate a single physical page.
 *
 * Returns: Physical address of the allocated page, or PMM_ALLOC_FAILED (0)
 *          if no free pages are available.
 */
uint64_t pmm_alloc(void) {
    /*
     * Linear scan for a free page.
     * This is O(n) which is inefficient for large memory, but simple.
     * A production allocator would use a free list or buddy system.
     *
     * TODO: Cache last allocation position or use free-list for O(1) alloc.
     *
     * We start from page 1 to ensure page 0 is never returned, since
     * returning 0 would be indistinguishable from PMM_ALLOC_FAILED.
     */
    for (uint64_t i = 1; i < total_pages; i++) {
        if (!bitmap_is_used(i)) {
            bitmap_mark_used(i);
            free_pages--;
            return i * PAGE_SIZE;
        }
    }
    return PMM_ALLOC_FAILED;  /* Out of memory - no free pages found */
}

/*
 * pmm_free - Free a previously allocated physical page.
 *
 * @phys_addr: Physical address of the page to free (must be page-aligned).
 *
 * Validates alignment, range, and detects double-free errors.
 */
void pmm_free(uint64_t phys_addr) {
    /* Validate: must be page-aligned */
    if (phys_addr & (PAGE_SIZE - 1)) {
        kprintf("ERROR: pmm_free: address not page-aligned: 0x%llx\n", phys_addr);
        return;
    }

    uint64_t page_index = phys_addr / PAGE_SIZE;

    /* Validate: must be in range and currently allocated */
    if (page_index >= total_pages) {
        kprintf("ERROR: pmm_free: address out of range: 0x%llx\n", phys_addr);
        return;
    }

    if (!bitmap_is_used(page_index)) {
        kprintf("ERROR: pmm_free: double free detected: 0x%llx\n", phys_addr);
        return;
    }

    bitmap_mark_free(page_index);
    free_pages++;
}

/*
 * pmm_get_free_pages - Get the number of currently free pages.
 *
 * Returns: Count of unallocated pages.
 */
uint64_t pmm_get_free_pages(void) {
    return free_pages;
}

/*
 * pmm_get_total_pages - Get the total number of tracked pages.
 *
 * Returns: Total pages in the bitmap (includes unusable regions).
 */
uint64_t pmm_get_total_pages(void) {
    return total_pages;
}

/*
 * pmm_get_usable_pages - Get the total usable RAM in pages.
 *
 * Returns: Sum of all usable memory regions divided by page size.
 */
uint64_t pmm_get_usable_pages(void) {
    return usable_pages;
}
