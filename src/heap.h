/*
 * heap.h - Kernel Heap Allocator
 *
 * Provides dynamic memory allocation for the kernel via kmalloc/kfree.
 * Uses a simple free-list allocator with block splitting and coalescing.
 *
 * The heap grows by requesting pages from the PMM and mapping them
 * into the kernel's virtual address space.
 */

#ifndef HEAP_H
#define HEAP_H

#include "types.h"

/*
 * kheap_init - Initialize the kernel heap.
 *
 * Must be called after pmm_init() and vmm_init().
 * Sets up the initial heap region.
 */
void kheap_init(void);

/*
 * kmalloc - Allocate memory from the kernel heap.
 *
 * @size: Number of bytes to allocate.
 *
 * Returns: Pointer to allocated memory, or NULL on failure.
 *
 * The returned memory is not zeroed. Use kzalloc() if you need
 * zero-initialized memory.
 */
void *kmalloc(size_t size);

/*
 * kzalloc - Allocate zero-initialized memory from the kernel heap.
 *
 * @size: Number of bytes to allocate.
 *
 * Returns: Pointer to zero-initialized memory, or NULL on failure.
 */
void *kzalloc(size_t size);

/*
 * kfree - Free memory allocated by kmalloc/kzalloc.
 *
 * @ptr: Pointer to memory to free. NULL is safely ignored.
 *
 * Freeing the same pointer twice is undefined behavior.
 */
void kfree(void *ptr);

/*
 * krealloc - Resize an allocation.
 *
 * @ptr:  Pointer to existing allocation, or NULL for new allocation.
 * @size: New size in bytes. If 0 and ptr is non-NULL, frees ptr.
 *
 * Returns: Pointer to resized memory, or NULL on failure.
 *
 * If reallocation fails, the original pointer remains valid.
 * The contents are preserved up to the minimum of old and new sizes.
 */
void *krealloc(void *ptr, size_t size);

/*
 * kheap_get_used - Get the amount of memory currently allocated.
 *
 * Returns: Total bytes allocated (not including allocator overhead).
 */
size_t kheap_get_used(void);

/*
 * kheap_get_free - Get the amount of free memory in the heap.
 *
 * Returns: Total bytes available for allocation.
 */
size_t kheap_get_free(void);

#endif /* HEAP_H */
