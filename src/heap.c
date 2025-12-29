/*
 * heap.c - Kernel Heap Allocator
 *
 * Implements a simple free-list allocator with the following features:
 *   - First-fit allocation strategy
 *   - Block splitting when allocations are smaller than free blocks
 *   - Block coalescing on free to reduce fragmentation
 *   - Automatic heap expansion by requesting pages from PMM
 *
 * Memory Layout:
 *
 *   Each allocated block has a header:
 *   ┌──────────────────────────────────────────┐
 *   │ block_header (size + flags)              │
 *   ├──────────────────────────────────────────┤
 *   │ User data (returned by kmalloc)          │
 *   │ ...                                      │
 *   └──────────────────────────────────────────┘
 *
 *   Free blocks also store next/prev pointers in the user data area:
 *   ┌──────────────────────────────────────────┐
 *   │ block_header (size + FREE flag)          │
 *   ├──────────────────────────────────────────┤
 *   │ next_free (pointer to next free block)   │
 *   │ prev_free (pointer to prev free block)   │
 *   │ (remaining space unused)                 │
 *   └──────────────────────────────────────────┘
 */

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "memory.h"
#include "log.h"

/* Minimum allocation size (must fit free list pointers) */
#define MIN_ALLOC_SIZE  (sizeof(void *) * 2)

/* Alignment for all allocations (16 bytes for SSE compatibility) */
#define HEAP_ALIGNMENT  16

/* Initial heap size in pages */
#define INITIAL_HEAP_PAGES  16

/* Heap grows in chunks of this many pages */
#define HEAP_GROW_PAGES     16

/* Block header flags (use lower 4 bits since alignment is 16) */
#define BLOCK_FREE      0x1
#define BLOCK_PREV_FREE 0x2  /* Previous block is free (for coalescing) */
#define BLOCK_FLAGS_MASK 0xF /* All flag bits (bits 0-3) */

/* Mask to extract size from header (clear flag bits) */
#define SIZE_MASK       (~(uint64_t)BLOCK_FLAGS_MASK)

/*
 * Block header structure.
 * Stored immediately before each allocation.
 * Size includes the header itself and is always aligned.
 */
typedef struct block_header {
    size_t size_and_flags;  /* Size in upper bits, flags in lower bits */
} block_header_t;

/*
 * Free block structure.
 * When a block is free, we reuse its data area to store list pointers.
 */
typedef struct free_block {
    block_header_t header;
    struct free_block *next;
    struct free_block *prev;
} free_block_t;

/* =============================================================================
 * Heap State
 * =============================================================================
 */

static uint64_t heap_start;       /* Virtual address where heap begins */
static uint64_t heap_end;         /* Current end of heap (grows upward) */
static free_block_t *free_list;   /* Head of free block list */
static size_t total_allocated;    /* Bytes currently allocated */
static size_t total_free;         /* Bytes currently free */

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

static inline size_t block_size(block_header_t *block) {
    return block->size_and_flags & SIZE_MASK;
}

static inline int block_is_free(block_header_t *block) {
    return block->size_and_flags & BLOCK_FREE;
}

static inline void block_set_free(block_header_t *block) {
    block->size_and_flags |= BLOCK_FREE;
}

static inline void block_set_used(block_header_t *block) {
    block->size_and_flags &= ~BLOCK_FREE;
}

static inline void *block_data(block_header_t *block) {
    return (void *)((uint8_t *)block + sizeof(block_header_t));
}

static inline block_header_t *data_to_block(void *ptr) {
    return (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
}

static inline block_header_t *next_block(block_header_t *block) {
    return (block_header_t *)((uint8_t *)block + block_size(block));
}

/* Align size up to HEAP_ALIGNMENT boundary */
static inline size_t align_up(size_t size) {
    return (size + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
}

/* =============================================================================
 * Free List Management
 * =============================================================================
 */

static void free_list_insert(free_block_t *block) {
    block->next = free_list;
    block->prev = NULL;
    if (free_list) {
        free_list->prev = block;
    }
    free_list = block;
}

static void free_list_remove(free_block_t *block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        free_list = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
}

/* =============================================================================
 * Heap Expansion
 * =============================================================================
 */

/*
 * Expand the heap by allocating more pages.
 * Returns pointer to the new free block, or NULL on failure.
 */
static free_block_t *heap_expand(size_t min_size) {
    /* Calculate how many pages we need */
    size_t pages_needed = (min_size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed < HEAP_GROW_PAGES) {
        pages_needed = HEAP_GROW_PAGES;
    }

    uint64_t pml4 = vmm_get_kernel_pml4();
    uint64_t new_region = heap_end;

    /* Track allocated physical pages for cleanup on failure */
    uint64_t allocated_phys[HEAP_GROW_PAGES > 64 ? 64 : HEAP_GROW_PAGES];
    size_t pages_allocated = 0;

    /* Allocate and map pages */
    for (size_t i = 0; i < pages_needed; i++) {
        uint64_t phys = pmm_alloc();
        if (phys == PMM_ALLOC_FAILED) {
            log_error("kheap: failed to allocate page for expansion");
            goto cleanup;
        }

        int result = vmm_map_page(pml4, heap_end, phys,
                                   PTE_PRESENT | PTE_WRITABLE);
        if (result != 0) {
            pmm_free(phys);
            log_error("kheap: failed to map page for expansion");
            goto cleanup;
        }

        allocated_phys[pages_allocated++] = phys;
        heap_end += PAGE_SIZE;
    }

    /* Create a free block spanning the new region */
    free_block_t *new_block = (free_block_t *)new_region;
    size_t blk_size = pages_needed * PAGE_SIZE;
    new_block->header.size_and_flags = blk_size | BLOCK_FREE;

    total_free += blk_size - sizeof(block_header_t);

    free_list_insert(new_block);

    return new_block;

cleanup:
    /* Rollback: unmap and free all pages we allocated */
    for (size_t i = 0; i < pages_allocated; i++) {
        uint64_t virt = new_region + i * PAGE_SIZE;
        vmm_unmap_page(pml4, virt);
        pmm_free(allocated_phys[i]);
    }
    heap_end = new_region;  /* Reset heap_end to original position */
    return NULL;
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

void kheap_init(void) {
    /*
     * Place heap in kernel space, after the HHDM region.
     * We'll use a fixed virtual address range for simplicity.
     * Start at 0xFFFFFFFF00000000 (kernel heap region)
     */
    heap_start = 0xFFFFFFFF00000000ULL;
    heap_end = heap_start;
    free_list = NULL;
    total_allocated = 0;
    total_free = 0;

    /* Allocate initial heap pages */
    uint64_t pml4 = vmm_get_kernel_pml4();

    for (size_t i = 0; i < INITIAL_HEAP_PAGES; i++) {
        uint64_t phys = pmm_alloc();
        if (phys == PMM_ALLOC_FAILED) {
            log_panic("kheap_init: failed to allocate initial heap pages");
            return;
        }

        int result = vmm_map_page(pml4, heap_end, phys,
                                   PTE_PRESENT | PTE_WRITABLE);
        if (result != 0) {
            log_panic("kheap_init: failed to map initial heap pages");
            return;
        }

        heap_end += PAGE_SIZE;
    }

    /* Create initial free block spanning entire heap */
    free_block_t *initial = (free_block_t *)heap_start;
    size_t initial_size = INITIAL_HEAP_PAGES * PAGE_SIZE;
    initial->header.size_and_flags = initial_size | BLOCK_FREE;
    initial->next = NULL;
    initial->prev = NULL;
    free_list = initial;

    total_free = initial_size - sizeof(block_header_t);

    log_info("HEAP: Heap allocated at 0x%llx, size %llu KB",
             heap_start, initial_size / 1024);
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    /* Calculate total size needed (header + data, aligned) */
    size_t total_size = align_up(sizeof(block_header_t) + size);
    if (total_size < sizeof(free_block_t)) {
        total_size = sizeof(free_block_t);  /* Minimum block size */
    }

    /* First-fit search through free list */
    free_block_t *current = free_list;
    while (current != NULL) {
        size_t current_size = block_size(&current->header);

        if (current_size >= total_size) {
            /* Found a suitable block */
            free_list_remove(current);

            /* Split block if there's enough remaining space */
            size_t remaining = current_size - total_size;
            if (remaining >= sizeof(free_block_t) + MIN_ALLOC_SIZE) {
                /* Create new free block from remaining space */
                free_block_t *new_free = (free_block_t *)
                    ((uint8_t *)current + total_size);
                new_free->header.size_and_flags = remaining | BLOCK_FREE;
                free_list_insert(new_free);

                /* Adjust current block size */
                current->header.size_and_flags = total_size;

                /*
                 * Account for: the allocated portion + the new header we created.
                 * The split creates a new block_header, reducing usable free space.
                 */
                total_free -= total_size + sizeof(block_header_t);
            } else {
                /* Use entire block */
                total_size = current_size;
                total_free -= total_size - sizeof(block_header_t);
            }

            block_set_used(&current->header);
            total_allocated += total_size - sizeof(block_header_t);

            return block_data(&current->header);
        }

        current = current->next;
    }

    /* No suitable block found, expand heap */
    free_block_t *new_block = heap_expand(total_size);
    if (new_block == NULL) {
        return NULL;
    }

    /*
     * Use the newly expanded block directly instead of recursing.
     * This avoids potential infinite recursion if something goes wrong.
     */
    free_list_remove(new_block);
    size_t new_block_size = block_size(&new_block->header);

    /* Split block if there's enough remaining space */
    size_t remaining = new_block_size - total_size;
    if (remaining >= sizeof(free_block_t) + MIN_ALLOC_SIZE) {
        /* Create new free block from remaining space */
        free_block_t *split_free = (free_block_t *)
            ((uint8_t *)new_block + total_size);
        split_free->header.size_and_flags = remaining | BLOCK_FREE;
        free_list_insert(split_free);

        /* Adjust block size */
        new_block->header.size_and_flags = total_size;

        total_free -= total_size + sizeof(block_header_t);
    } else {
        /* Use entire block */
        total_size = new_block_size;
        total_free -= total_size - sizeof(block_header_t);
    }

    block_set_used(&new_block->header);
    total_allocated += total_size - sizeof(block_header_t);

    return block_data(&new_block->header);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr != NULL) {
        uint8_t *bytes = (uint8_t *)ptr;
        for (size_t i = 0; i < size; i++) {
            bytes[i] = 0;
        }
    }
    return ptr;
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    block_header_t *block = data_to_block(ptr);
    size_t size = block_size(block);

    /* Validate block is in heap range */
    uint64_t addr = (uint64_t)block;
    if (addr < heap_start || addr >= heap_end) {
        log_error("kfree: pointer 0x%llx outside heap range", (uint64_t)ptr);
        return;
    }

    /* Check for double-free */
    if (block_is_free(block)) {
        log_error("kfree: double free detected at 0x%llx", (uint64_t)ptr);
        return;
    }

    total_allocated -= size - sizeof(block_header_t);
    total_free += size - sizeof(block_header_t);

    /* Try to coalesce with next block */
    block_header_t *next = next_block(block);
    if ((uint64_t)next < heap_end && block_is_free(next)) {
        /* Remove next from free list and merge */
        free_list_remove((free_block_t *)next);
        size += block_size(next);
        block->size_and_flags = size;  /* Flags will be set below */
    }

    /*
     * Note: Coalescing with previous block would require storing
     * footer/boundary tags. For simplicity, we only coalesce forward.
     */

    /* Mark as free and add to free list */
    block_set_free(block);
    free_list_insert((free_block_t *)block);
}

void *krealloc(void *ptr, size_t size) {
    /* NULL ptr: equivalent to kmalloc */
    if (ptr == NULL) {
        return kmalloc(size);
    }

    /* Size 0: equivalent to kfree */
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    block_header_t *block = data_to_block(ptr);
    size_t old_size = block_size(block) - sizeof(block_header_t);

    /* If new size fits in current block, return same pointer */
    size_t total_needed = align_up(sizeof(block_header_t) + size);
    if (total_needed <= block_size(block)) {
        return ptr;
    }

    /* Allocate new block */
    void *new_ptr = kmalloc(size);
    if (new_ptr == NULL) {
        return NULL;  /* Original pointer remains valid */
    }

    /* Copy old data */
    size_t copy_size = (old_size < size) ? old_size : size;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }

    kfree(ptr);
    return new_ptr;
}

size_t kheap_get_used(void) {
    return total_allocated;
}

size_t kheap_get_free(void) {
    return total_free;
}
