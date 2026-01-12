// SPDX-License-Identifier: GPL-2.0-only
/*
 * Page Reference Counting
 *
 * Maintains per-page reference counts for Copy-on-Write support.
 */

#include "page.h"
#include "pmm.h"
#include "heap.h"
#include "log.h"
#include "memory.h"

/*
 * Page info array - one entry per physical page
 * Indexed by physical page number (phys >> 12)
 */
static page_info_t *page_info = NULL;
static uint64_t max_pages = 0;

/**
 * page_init - Initialize page tracking
 */
void page_init(uint64_t max_phys)
{
    size_t array_size;

    max_pages = max_phys / PAGE_SIZE;
    array_size = max_pages * sizeof(page_info_t);

    /*
     * Allocate from heap. For a system with 128MB RAM:
     *   128MB / 4KB = 32768 pages
     *   32768 * 1 byte = 32KB
     */
    page_info = kmalloc(array_size);
    if (!page_info) {
        log_panic("PAGE: Failed to allocate page_info array (%lu bytes)", array_size);
        return;
    }

    /* Initialize all pages to refcount 0 (free) */
    for (uint64_t i = 0; i < max_pages; i++) {
        page_info[i].refcount = 0;
    }

    log_debug("PAGE: Tracking %llu pages (%lu KB overhead)",
              max_pages, array_size / 1024);
}

/**
 * page_ref - Increment page reference count
 */
void page_ref(uint64_t phys)
{
    uint64_t page_num;

    if (!page_info) {
        return;
    }

    page_num = phys / PAGE_SIZE;
    if (page_num >= max_pages) {
        log_warn("PAGE: ref out of range: 0x%llx", phys);
        return;
    }

    if (page_info[page_num].refcount < 255) {
        page_info[page_num].refcount++;
    }
    /* 255 = pinned, don't increment further */
}

/**
 * page_unref - Decrement page reference count
 */
void page_unref(uint64_t phys)
{
    uint64_t page_num;

    if (!page_info) {
        return;
    }

    page_num = phys / PAGE_SIZE;
    if (page_num >= max_pages) {
        log_warn("PAGE: unref out of range: 0x%llx", phys);
        return;
    }

    if (page_info[page_num].refcount == 0) {
        log_warn("PAGE: unref on zero refcount: 0x%llx", phys);
        return;
    }

    if (page_info[page_num].refcount == 255) {
        /* Pinned page - don't decrement */
        return;
    }

    page_info[page_num].refcount--;

    /* Free page if no more references */
    if (page_info[page_num].refcount == 0) {
        pmm_free(phys);
    }
}

/**
 * page_get_refcount - Get current reference count
 */
uint8_t page_get_refcount(uint64_t phys)
{
    uint64_t page_num;

    if (!page_info) {
        return 0;
    }

    page_num = phys / PAGE_SIZE;
    if (page_num >= max_pages) {
        return 0;
    }

    return page_info[page_num].refcount;
}

/**
 * page_set_refcount - Set reference count directly
 */
void page_set_refcount(uint64_t phys, uint8_t count)
{
    uint64_t page_num;

    if (!page_info) {
        return;
    }

    page_num = phys / PAGE_SIZE;
    if (page_num >= max_pages) {
        log_warn("PAGE: set_refcount out of range: 0x%llx", phys);
        return;
    }

    page_info[page_num].refcount = count;
}
