// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel heap allocator
 *
 * Free-list allocator with first-fit strategy, block splitting,
 * forward coalescing, and automatic expansion from PMM.
 */

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "memory.h"
#include "log.h"

#define MIN_ALLOC_SIZE		(sizeof(void *) * 2)
#define HEAP_ALIGNMENT		16
#define INITIAL_HEAP_PAGES	16
#define HEAP_GROW_PAGES		16

#define BLOCK_FREE		0x1
#define BLOCK_PREV_FREE		0x2
#define BLOCK_FLAGS_MASK	0xF
#define SIZE_MASK		(~(uint64_t)BLOCK_FLAGS_MASK)

typedef struct block_header {
	size_t size_and_flags;
} block_header_t;

typedef struct free_block {
	block_header_t header;
	struct free_block *next;
	struct free_block *prev;
} free_block_t;

static uint64_t heap_start;
static uint64_t heap_end;
static free_block_t *free_list;
static size_t total_allocated;
static size_t total_free;

static inline size_t block_size(block_header_t *block)
{
	return block->size_and_flags & SIZE_MASK;
}

static inline int block_is_free(block_header_t *block)
{
	return block->size_and_flags & BLOCK_FREE;
}

static inline void block_set_free(block_header_t *block)
{
	block->size_and_flags |= BLOCK_FREE;
}

static inline void block_set_used(block_header_t *block)
{
	block->size_and_flags &= ~BLOCK_FREE;
}

static inline void *block_data(block_header_t *block)
{
	return (void *)((uint8_t *)block + sizeof(block_header_t));
}

static inline block_header_t *data_to_block(void *ptr)
{
	return (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
}

static inline block_header_t *next_block(block_header_t *block)
{
	return (block_header_t *)((uint8_t *)block + block_size(block));
}

static inline size_t align_up(size_t size)
{
	return (size + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
}

static void free_list_insert(free_block_t *block)
{
	block->next = free_list;
	block->prev = NULL;
	if (free_list)
		free_list->prev = block;
	free_list = block;
}

static void free_list_remove(free_block_t *block)
{
	if (block->prev)
		block->prev->next = block->next;
	else
		free_list = block->next;
	if (block->next)
		block->next->prev = block->prev;
}

static free_block_t *heap_expand(size_t min_size)
{
	size_t pages_needed = (min_size + PAGE_SIZE - 1) / PAGE_SIZE;
	uint64_t pml4;
	uint64_t new_region;
	uint64_t allocated_phys[64];
	size_t pages_allocated = 0;
	free_block_t *new_block;
	size_t blk_size;

	if (pages_needed < HEAP_GROW_PAGES)
		pages_needed = HEAP_GROW_PAGES;

	if (pages_needed > 64) {
		log_error("kheap: expansion too large (%zu pages)", pages_needed);
		return NULL;
	}

	pml4 = vmm_get_kernel_pml4();
	new_region = heap_end;

	for (size_t i = 0; i < pages_needed; i++) {
		uint64_t phys = pmm_alloc();
		int result;

		if (phys == PMM_ALLOC_FAILED) {
			log_error("kheap: failed to allocate page");
			goto cleanup;
		}

		result = vmm_map_page(pml4, heap_end, phys,
				      PTE_PRESENT | PTE_WRITABLE);
		if (result != 0) {
			pmm_free(phys);
			log_error("kheap: failed to map page");
			goto cleanup;
		}

		allocated_phys[pages_allocated++] = phys;
		heap_end += PAGE_SIZE;
	}

	new_block = (free_block_t *)new_region;
	blk_size = pages_needed * PAGE_SIZE;
	new_block->header.size_and_flags = blk_size | BLOCK_FREE;

	total_free += blk_size - sizeof(block_header_t);
	free_list_insert(new_block);

	return new_block;

cleanup:
	for (size_t i = 0; i < pages_allocated; i++) {
		uint64_t virt = new_region + i * PAGE_SIZE;
		vmm_unmap_page(pml4, virt);
		pmm_free(allocated_phys[i]);
	}
	heap_end = new_region;
	return NULL;
}

/**
 * kheap_init - Initialize the kernel heap
 */
void kheap_init(void)
{
	uint64_t pml4;
	free_block_t *initial;
	size_t initial_size;

	heap_start = 0xFFFFFFFF00000000ULL;
	heap_end = heap_start;
	free_list = NULL;
	total_allocated = 0;
	total_free = 0;

	pml4 = vmm_get_kernel_pml4();

	for (size_t i = 0; i < INITIAL_HEAP_PAGES; i++) {
		uint64_t phys = pmm_alloc();
		int result;

		if (phys == PMM_ALLOC_FAILED) {
			log_panic("kheap_init: failed to allocate pages");
			return;
		}

		result = vmm_map_page(pml4, heap_end, phys,
				      PTE_PRESENT | PTE_WRITABLE);
		if (result != 0) {
			log_panic("kheap_init: failed to map pages");
			return;
		}

		heap_end += PAGE_SIZE;
	}

	initial = (free_block_t *)heap_start;
	initial_size = INITIAL_HEAP_PAGES * PAGE_SIZE;
	initial->header.size_and_flags = initial_size | BLOCK_FREE;
	initial->next = NULL;
	initial->prev = NULL;
	free_list = initial;

	total_free = initial_size - sizeof(block_header_t);

	log_info("HEAP: at 0x%llx, size %llu KB", heap_start, initial_size / 1024);
}

/**
 * kmalloc - Allocate kernel memory
 * @size: number of bytes to allocate
 *
 * Return: pointer to allocated memory, or NULL on failure
 */
void *kmalloc(size_t size)
{
	size_t total_size;
	free_block_t *current;

	if (size == 0)
		return NULL;

	total_size = align_up(sizeof(block_header_t) + size);
	if (total_size < sizeof(free_block_t))
		total_size = sizeof(free_block_t);

	current = free_list;
	while (current != NULL) {
		size_t current_size = block_size(&current->header);

		if (current_size >= total_size) {
			size_t remaining;

			free_list_remove(current);

			remaining = current_size - total_size;
			if (remaining >= sizeof(free_block_t) + MIN_ALLOC_SIZE) {
				free_block_t *new_free = (free_block_t *)
					((uint8_t *)current + total_size);
				new_free->header.size_and_flags = remaining | BLOCK_FREE;
				free_list_insert(new_free);
				current->header.size_and_flags = total_size;
				total_free -= total_size - sizeof(block_header_t);
			} else {
				total_size = current_size;
				total_free -= total_size - sizeof(block_header_t);
			}

			block_set_used(&current->header);
			total_allocated += total_size - sizeof(block_header_t);
			return block_data(&current->header);
		}

		current = current->next;
	}

	/* Expand heap */
	{
		free_block_t *new_block = heap_expand(total_size);
		size_t new_block_size;
		size_t remaining;

		if (new_block == NULL)
			return NULL;

		free_list_remove(new_block);
		new_block_size = block_size(&new_block->header);

		remaining = new_block_size - total_size;
		if (remaining >= sizeof(free_block_t) + MIN_ALLOC_SIZE) {
			free_block_t *split = (free_block_t *)
				((uint8_t *)new_block + total_size);
			split->header.size_and_flags = remaining | BLOCK_FREE;
			free_list_insert(split);
			new_block->header.size_and_flags = total_size;
			total_free -= total_size - sizeof(block_header_t);
		} else {
			total_size = new_block_size;
			total_free -= total_size - sizeof(block_header_t);
		}

		block_set_used(&new_block->header);
		total_allocated += total_size - sizeof(block_header_t);
		return block_data(&new_block->header);
	}
}

/**
 * kzalloc - Allocate zeroed kernel memory
 * @size: number of bytes to allocate
 *
 * Return: pointer to zeroed memory, or NULL on failure
 */
void *kzalloc(size_t size)
{
	void *ptr = kmalloc(size);

	if (ptr != NULL) {
		uint8_t *bytes = (uint8_t *)ptr;

		for (size_t i = 0; i < size; i++)
			bytes[i] = 0;
	}
	return ptr;
}

/**
 * kfree - Free kernel memory
 * @ptr: pointer to memory to free
 */
void kfree(void *ptr)
{
	block_header_t *block;
	block_header_t *next;
	size_t size;
	uint64_t addr;

	if (ptr == NULL)
		return;

	block = data_to_block(ptr);
	size = block_size(block);

	addr = (uint64_t)block;
	if (addr < heap_start || addr >= heap_end) {
		log_error("kfree: 0x%llx outside heap", (uint64_t)ptr);
		return;
	}

	if (block_is_free(block)) {
		log_error("kfree: double free at 0x%llx", (uint64_t)ptr);
		return;
	}

	total_allocated -= size - sizeof(block_header_t);
	total_free += size - sizeof(block_header_t);

	/* Coalesce with next block */
	next = next_block(block);
	if ((uint64_t)next < heap_end && block_is_free(next)) {
		free_list_remove((free_block_t *)next);
		size += block_size(next);
		block->size_and_flags = size;
	}

	block_set_free(block);
	free_list_insert((free_block_t *)block);
}

/**
 * krealloc - Reallocate kernel memory
 * @ptr: pointer to existing memory (or NULL)
 * @size: new size
 *
 * Return: pointer to reallocated memory, or NULL on failure
 */
void *krealloc(void *ptr, size_t size)
{
	block_header_t *block;
	size_t old_size;
	size_t total_needed;
	void *new_ptr;
	size_t copy_size;
	uint8_t *src;
	uint8_t *dst;

	if (ptr == NULL)
		return kmalloc(size);

	if (size == 0) {
		kfree(ptr);
		return NULL;
	}

	block = data_to_block(ptr);
	old_size = block_size(block) - sizeof(block_header_t);

	total_needed = align_up(sizeof(block_header_t) + size);
	if (total_needed <= block_size(block))
		return ptr;

	new_ptr = kmalloc(size);
	if (new_ptr == NULL)
		return NULL;

	copy_size = (old_size < size) ? old_size : size;
	src = (uint8_t *)ptr;
	dst = (uint8_t *)new_ptr;
	for (size_t i = 0; i < copy_size; i++)
		dst[i] = src[i];

	kfree(ptr);
	return new_ptr;
}

/**
 * kheap_get_used - Get bytes currently allocated
 *
 * Return: bytes allocated
 */
size_t kheap_get_used(void)
{
	return total_allocated;
}

/**
 * kheap_get_free - Get bytes currently free
 *
 * Return: bytes free
 */
size_t kheap_get_free(void)
{
	return total_free;
}
