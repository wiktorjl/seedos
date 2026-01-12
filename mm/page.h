/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Page Reference Counting
 *
 * Tracks reference counts for physical pages to support Copy-on-Write.
 * Each physical page has an associated refcount:
 *   - 0: Page is free (managed by PMM)
 *   - 1: Single owner (can write directly)
 *   - >1: Shared (COW - must copy before write)
 *
 * The refcount array uses ~0.2% memory overhead (1 byte per 4KB page).
 */

#ifndef _PAGE_H
#define _PAGE_H

#include "types.h"

/*
 * Page info structure (one per physical page)
 *
 * Currently just a refcount, but can be extended for:
 * - Page flags (dirty, accessed, locked)
 * - LRU list pointers for swapping
 * - Page cache backpointers
 */
typedef struct {
    uint8_t refcount;   /* Reference count (0 = free, 255 = pinned) */
} page_info_t;

/**
 * page_init - Initialize page tracking
 * @max_phys: Highest physical address to track
 *
 * Allocates the page_info array. Must be called after PMM init.
 */
void page_init(uint64_t max_phys);

/**
 * page_ref - Increment page reference count
 * @phys: Physical address of page
 *
 * Called when a new PTE points to this page (e.g., during fork COW setup).
 */
void page_ref(uint64_t phys);

/**
 * page_unref - Decrement page reference count
 * @phys: Physical address of page
 *
 * If refcount reaches 0, the page is freed via pmm_free().
 * Called when unmapping a page or during COW copy.
 */
void page_unref(uint64_t phys);

/**
 * page_get_refcount - Get current reference count
 * @phys: Physical address of page
 *
 * Return: Current refcount (0 if untracked)
 */
uint8_t page_get_refcount(uint64_t phys);

/**
 * page_set_refcount - Set reference count directly
 * @phys: Physical address of page
 * @count: New reference count
 *
 * Used by PMM to initialize newly allocated pages to refcount 1.
 */
void page_set_refcount(uint64_t phys, uint8_t count);

#endif /* _PAGE_H */
