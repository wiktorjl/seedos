/*
 * vmm.h - Virtual Memory Manager
 *
 * Provides 4-level paging and per-process address spaces.
 * Kernel is mapped in upper half (entries 256-511 of PML4).
 */

#ifndef VMM_H
#define VMM_H

#include <stdint.h>

/* Page size (must match PMM) */
#define VMM_PAGE_SIZE 4096

/* Page table entry flags */
#define PTE_PRESENT     (1ULL << 0)   /* Page is present in memory */
#define PTE_WRITABLE    (1ULL << 1)   /* Page is writable */
#define PTE_USER        (1ULL << 2)   /* Page accessible from ring 3 */
#define PTE_WRITETHROUGH (1ULL << 3)  /* Write-through caching */
#define PTE_NOCACHE     (1ULL << 4)   /* Disable caching */
#define PTE_ACCESSED    (1ULL << 5)   /* CPU sets when accessed */
#define PTE_DIRTY       (1ULL << 6)   /* CPU sets when written */
#define PTE_HUGE        (1ULL << 7)   /* Huge page (2MB or 1GB) */
#define PTE_GLOBAL      (1ULL << 8)   /* Don't flush on CR3 switch */
#define PTE_NX          (1ULL << 63)  /* No execute */

/* Address mask for page table entries (bits 12-51) */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

/* Extract page table indices from virtual address */
#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)

/* Kernel space starts at PML4 entry 256 (upper half) */
#define KERNEL_PML4_START 256

/* User address space layout */
#define USER_CODE_BASE   0x400000ULL
#define USER_STACK_BASE  0x7FFFFF000ULL

/*
 * Initialize the VMM.
 * Captures kernel's PML4 and stores HHDM offset.
 * Must be called after PMM is initialized.
 */
void vmm_init(uint64_t hhdm_offset);

/*
 * Create a new address space.
 * Allocates a PML4 and copies kernel mappings (entries 256-511).
 * Returns physical address of new PML4, or 0 on failure.
 */
uint64_t vmm_create_address_space(void);

/*
 * Map a virtual page to a physical page.
 * Creates intermediate page table levels as needed.
 * Returns 0 on success, -1 on failure (out of memory).
 */
int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

/*
 * Switch to a different address space.
 * Loads CR3 with the new PML4 physical address.
 */
void vmm_switch_address_space(uint64_t pml4_phys);

/*
 * Get the kernel's PML4 physical address.
 */
uint64_t vmm_get_kernel_pml4(void);

#endif /* VMM_H */
