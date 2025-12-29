#ifndef PMM_H
#define PMM_H

#include "limine.h"
#include "types.h"

#define PAGE_SIZE 4096
#define CODE_PAGE_SIZE PAGE_SIZE

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

uint64_t pmm_alloc(void);

void pmm_free(uint64_t phys_addr);

uint64_t pmm_get_free_pages(void);

uint64_t pmm_get_total_pages(void);

uint64_t pmm_get_usable_pages(void);

uint64_t vmm_get_physical(uint64_t pml4_phys, uint64_t virt);

#endif /* PMM_H */
