/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Virtual memory manager
 *
 * Implements x86-64 4-level paging with per-process address spaces.
 * Kernel mapped in PML4 entries 256-511, user space in 0-255.
 */

#ifndef _VMM_H
#define _VMM_H

#include "types.h"

#define VMM_PAGE_SIZE	4096

/* Page table entry flags */
#define PTE_PRESENT	(1ULL << 0)
#define PTE_WRITABLE	(1ULL << 1)
#define PTE_USER	(1ULL << 2)
#define PTE_WRITETHROUGH (1ULL << 3)
#define PTE_NOCACHE	(1ULL << 4)
#define PTE_ACCESSED	(1ULL << 5)
#define PTE_DIRTY	(1ULL << 6)
#define PTE_HUGE	(1ULL << 7)
#define PTE_GLOBAL	(1ULL << 8)
#define PTE_COW		(1ULL << 9)  /* Software: Copy-on-Write */
#define PTE_NX		(1ULL << 63)

/* Physical address mask (bits 12-51) */
#define PTE_ADDR_MASK	0x000FFFFFFFFFF000ULL

/* Virtual address index extraction */
#define PML4_INDEX(va)	(((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va)	(((va) >> 30) & 0x1FF)
#define PD_INDEX(va)	(((va) >> 21) & 0x1FF)
#define PT_INDEX(va)	(((va) >> 12) & 0x1FF)

#define KERNEL_PML4_START	256

/* User address space layout */
#define USER_CODE_BASE		0x400000ULL
#define USER_STACK_BASE		0x7FFFF0000ULL
#define USER_STACK_TOP		0x800000000ULL
#define USER_HEAP_BASE		0x500000ULL

/**
 * vmm_validate_user_range - Validate a user-space pointer range
 * @ptr: pointer to validate
 * @len: length of range
 *
 * Return: true if range is valid user space, false otherwise
 */
bool vmm_validate_user_range(const void *ptr, size_t len);

/**
 * vmm_user_range_readable - Check that every page in [ptr, ptr+len) is
 * present and accessible from user mode in @pml4_phys.
 *
 * Use before kernel code dereferences a user pointer to avoid taking a
 * kernel page-fault on an unmapped-but-in-range user page. TOCTOU
 * caveat: a concurrent unmap could invalidate the result; with no
 * preemption inside syscalls and a single CPU this is safe today.
 */
bool vmm_user_range_readable(uint64_t pml4_phys, const void *ptr, size_t len);

/**
 * vmm_user_range_writable - Like _readable, but also requires each
 * page be either WRITABLE or COW (a subsequent write fault will be
 * handled by the COW path).
 */
bool vmm_user_range_writable(uint64_t pml4_phys, const void *ptr, size_t len);

/**
 * vmm_free_user_address_space - Free all user-space mappings
 * @pml4_phys: physical address of PML4 to free
 */
void vmm_free_user_address_space(uint64_t pml4_phys);

/**
 * vmm_init - Initialize the virtual memory manager
 * @hhdm_offset: HHDM offset (already set by PMM)
 */
void vmm_init(uint64_t hhdm_offset);

/**
 * vmm_create_address_space - Create a new address space
 *
 * Return: physical address of new PML4, or 0 on failure
 */
uint64_t vmm_create_address_space(void);

/**
 * vmm_map_page - Map a virtual address to a physical address
 * @pml4_phys: physical address of PML4
 * @virt: virtual address to map
 * @phys: physical address to map to
 * @flags: page flags
 *
 * Return: 0 on success, -1 on failure
 */
int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

/**
 * vmm_unmap_page - Remove a page mapping
 * @pml4_phys: physical address of PML4
 * @virt: virtual address to unmap
 *
 * Return: 0 on success, -1 if not mapped
 */
int vmm_unmap_page(uint64_t pml4_phys, uint64_t virt);

/**
 * vmm_switch_address_space - Switch to a different address space
 * @pml4_phys: physical address of PML4 to activate
 */
void vmm_switch_address_space(uint64_t pml4_phys);

/**
 * vmm_get_kernel_pml4 - Get the kernel's PML4 physical address
 *
 * Return: physical address of kernel PML4
 */
uint64_t vmm_get_kernel_pml4(void);

/**
 * vmm_get_physical - Get physical address for a virtual address
 * @pml4_phys: physical address of PML4 to query
 * @virt: virtual address to translate
 *
 * Return: physical address, or 0 if not mapped
 */
uint64_t vmm_get_physical(uint64_t pml4_phys, uint64_t virt);

/**
 * vmm_get_pte_flags - Get PTE flags for a virtual address
 * @pml4_phys: physical address of PML4 to query
 * @virt: virtual address to query
 *
 * Return: PTE flags, or 0 if not mapped
 */
uint64_t vmm_get_pte_flags(uint64_t pml4_phys, uint64_t virt);

/**
 * vmm_set_pte_flags - Set PTE flags for a virtual address
 * @pml4_phys: physical address of PML4
 * @virt: virtual address to modify
 * @flags: new flags to set (replaces existing flags, keeps physical address)
 *
 * Return: 0 on success, -1 if not mapped
 */
int vmm_set_pte_flags(uint64_t pml4_phys, uint64_t virt, uint64_t flags);

/**
 * vmm_copy_address_space_cow - Copy address space with Copy-on-Write
 * @src_pml4: physical address of source PML4
 *
 * Creates a new address space that shares all user pages with the source
 * using COW semantics. Both source and destination PTEs are marked read-only
 * with COW bit set. Reference counts are incremented for shared pages.
 *
 * Return: physical address of new PML4, or 0 on failure
 */
uint64_t vmm_copy_address_space_cow(uint64_t src_pml4);

#endif /* _VMM_H */
