// SPDX-License-Identifier: GPL-2.0-only
/*
 * Physical memory manager
 *
 * Bitmap allocator for physical pages. Each bit represents a 4KB page:
 * 1 = used, 0 = free.
 */

#include "pmm.h"
#include "limine.h"
#include "log.h"
#include "memory.h"
#include "kprintf.h"

uint64_t g_hhdm_offset;

static uint8_t *bitmap;
static uint64_t bitmap_size;
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t usable_pages;

static inline void bitmap_mark_used(uint64_t page_index)
{
	bitmap[page_index / 8] |= (1 << (page_index % 8));
}

static inline void bitmap_mark_free(uint64_t page_index)
{
	bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

static inline int bitmap_is_used(uint64_t page_index)
{
	return bitmap[page_index / 8] & (1 << (page_index % 8));
}

/**
 * pmm_init - Initialize the physical memory manager
 * @memmap: memory map from bootloader
 * @hhdm_offset: HHDM offset for physical-to-virtual conversion
 */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset)
{
	uint64_t highest_addr = 0;
	uint64_t bitmap_pages;
	uint64_t bitmap_phys = 0;
	int bitmap_found = 0;

	g_hhdm_offset = hhdm_offset;

	/* Find highest physical address */
	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *entry = memmap->entries[i];
		uint64_t entry_end = entry->base + entry->length;

		if (entry_end > highest_addr && entry->type == LIMINE_MEMMAP_USABLE)
			highest_addr = entry_end;
	}

	total_pages = (highest_addr + PAGE_SIZE - 1) / PAGE_SIZE;
	bitmap_size = (total_pages + 7) / 8;

	/* Calculate total usable RAM */
	usable_pages = 0;
	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *entry = memmap->entries[i];

		if (entry->type == LIMINE_MEMMAP_USABLE)
			usable_pages += entry->length / PAGE_SIZE;
	}

	/* Find space for bitmap */
	bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *entry = memmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE)
			continue;
		if (entry->length >= bitmap_pages * PAGE_SIZE) {
			bitmap_phys = entry->base;
			bitmap_found = 1;
			break;
		}
	}

	if (!bitmap_found) {
		while (1) {
			log_panic("PMM init failed: no space for bitmap");
			__asm__ volatile("hlt");
		}
	} else {
		log_info("PMM: Bitmap at 0x%llx (%llu pages)",
			 bitmap_phys, bitmap_pages);
	}

	bitmap = phys_to_virt(bitmap_phys);

	/* Mark all pages used initially */
	for (uint64_t i = 0; i < bitmap_size; i++)
		bitmap[i] = 0xFF;
	free_pages = 0;

	/* Mark usable regions as free */
	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *entry = memmap->entries[i];
		uint64_t base_page;
		uint64_t page_count;

		if (entry->type != LIMINE_MEMMAP_USABLE)
			continue;

		base_page = entry->base / PAGE_SIZE;
		page_count = entry->length / PAGE_SIZE;

		for (uint64_t p = 0; p < page_count; p++) {
			bitmap_mark_free(base_page + p);
			free_pages++;
		}
	}

	/* Reserve bitmap pages */
	for (uint64_t p = 0; p < bitmap_pages; p++) {
		uint64_t idx = bitmap_phys / PAGE_SIZE + p;

		if (!bitmap_is_used(idx)) {
			bitmap_mark_used(idx);
			free_pages--;
		}
	}

	/* Reserve page 0 (NULL) */
	if (!bitmap_is_used(0)) {
		bitmap_mark_used(0);
		free_pages--;
	}
}

/**
 * pmm_alloc - Allocate a single physical page
 *
 * Return: physical address, or PMM_ALLOC_FAILED if no free pages
 */
uint64_t pmm_alloc(void)
{
	for (uint64_t i = 1; i < total_pages; i++) {
		if (!bitmap_is_used(i)) {
			bitmap_mark_used(i);
			free_pages--;
			return i * PAGE_SIZE;
		}
	}
	return PMM_ALLOC_FAILED;
}

/**
 * pmm_free - Free a previously allocated physical page
 * @phys_addr: physical address to free (must be page-aligned)
 */
void pmm_free(uint64_t phys_addr)
{
	uint64_t page_index;

	if (phys_addr & (PAGE_SIZE - 1)) {
		kprintf("ERROR: pmm_free: not page-aligned: 0x%llx\n", phys_addr);
		return;
	}

	page_index = phys_addr / PAGE_SIZE;

	if (page_index >= total_pages) {
		kprintf("ERROR: pmm_free: out of range: 0x%llx\n", phys_addr);
		return;
	}

	if (!bitmap_is_used(page_index)) {
		kprintf("ERROR: pmm_free: double free: 0x%llx\n", phys_addr);
		return;
	}

	bitmap_mark_free(page_index);
	free_pages++;
}

/**
 * pmm_get_free_pages - Get number of free pages
 *
 * Return: count of unallocated pages
 */
uint64_t pmm_get_free_pages(void)
{
	return free_pages;
}

/**
 * pmm_get_total_pages - Get total tracked pages
 *
 * Return: total pages in bitmap
 */
uint64_t pmm_get_total_pages(void)
{
	return total_pages;
}

/**
 * pmm_get_usable_pages - Get total usable RAM in pages
 *
 * Return: sum of usable regions divided by page size
 */
uint64_t pmm_get_usable_pages(void)
{
	return usable_pages;
}
