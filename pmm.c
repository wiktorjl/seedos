/*
 * pmm.c - Physical Memory Manager
 *
 * Bitmap-based allocator for physical page frames.
 */

#include "pmm.h"

/* Global state */
static uint8_t *bitmap;           /* Pointer to bitmap (virtual address) */
static uint64_t bitmap_size;      /* Size of bitmap in bytes */
static uint64_t total_pages;      /* Total number of pages being tracked */
static uint64_t free_pages;       /* Number of free pages */
static uint64_t hhdm;             /* HHDM offset for phys->virt conversion */

/* Convert physical address to virtual address using HHDM */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm);
}

/* Convert virtual address to physical address */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt - hhdm;
}

/* Set a bit in the bitmap (mark page as used) */
static inline void bitmap_set(uint64_t page_index) {
    bitmap[page_index / 8] |= (1 << (page_index % 8));
}

/* Clear a bit in the bitmap (mark page as free) */
static inline void bitmap_clear(uint64_t page_index) {
    bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

/* Test if a bit is set (page is used) */
static inline int bitmap_test(uint64_t page_index) {
    return bitmap[page_index / 8] & (1 << (page_index % 8));
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    hhdm = hhdm_offset;

    /*
     * Step 1: Find the highest physical address to determine
     * how many pages we need to track.
     */
    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t entry_end = entry->base + entry->length;
        if (entry_end > highest_addr) {
            highest_addr = entry_end;
        }
    }

    total_pages = highest_addr / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;  /* Round up to nearest byte */

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
            bitmap_clear(base_page + p);
            free_pages++;
        }
    }

    /*
     * Step 5: Mark the bitmap's own pages as used.
     * We already placed data there, so they're not free!
     */
    uint64_t bitmap_base_page = bitmap_phys / PAGE_SIZE;
    for (uint64_t p = 0; p < bitmap_pages; p++) {
        if (!bitmap_test(bitmap_base_page + p)) {
            bitmap_set(bitmap_base_page + p);
            free_pages--;
        }
    }

    /*
     * Step 6: Mark page 0 as used (never allocate it).
     * NULL pointer dereferences should fault, not succeed.
     */
    if (!bitmap_test(0)) {
        bitmap_set(0);
        free_pages--;
    }
}

uint64_t pmm_alloc(void) {
    /* Linear scan for a free page - not efficient, but simple */
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            return i * PAGE_SIZE;
        }
    }
    return 0;  /* Out of memory */
}

void pmm_free(uint64_t phys_addr) {
    uint64_t page_index = phys_addr / PAGE_SIZE;
    if (page_index < total_pages && bitmap_test(page_index)) {
        bitmap_clear(page_index);
        free_pages++;
    }
}

uint64_t pmm_get_free_pages(void) {
    return free_pages;
}

uint64_t pmm_get_total_pages(void) {
    return total_pages;
}
