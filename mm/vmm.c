// SPDX-License-Identifier: GPL-2.0-only
/*
 * Virtual memory manager
 *
 * Implements 4-level paging for x86-64 with per-process address spaces.
 */

#include "vmm.h"
#include "log.h"
#include "pmm.h"
#include "page.h"
#include "memory.h"
#include <stdint.h>

#define PAGE_TABLE_ENTRIES	512
#define PAGE_TABLE_ENTRIES_USER	256

static uint64_t kernel_pml4_phys;

static uint64_t alloc_page_table(void)
{
	uint64_t phys = pmm_alloc();
	uint64_t *table;

	if (phys == PMM_ALLOC_FAILED)
		return 0;

	table = phys_to_virt(phys);
	for (int i = 0; i < PAGE_TABLE_ENTRIES; i++)
		table[i] = 0;

	return phys;
}

/**
 * vmm_validate_user_range - Validate a user-space pointer range
 * @ptr: pointer to validate
 * @len: length of range
 *
 * Return: true if range is valid user space, false otherwise
 */
bool vmm_validate_user_range(const void *ptr, size_t len)
{
	uint64_t addr = (uint64_t)ptr;
	uint64_t end;

	if (addr == 0)
		return false;
	if (addr >= USER_SPACE_TOP)
		return false;
	if (len == 0)
		return false;

	end = addr + len;
	if (end < addr || end > USER_SPACE_TOP)
		return false;

	return true;
}

/*
 * Internal helper: check each page in [start, start+len) satisfies the
 * required mask of PTE flags (besides PRESENT|USER).
 */
static bool user_range_check(uint64_t pml4_phys, const void *ptr,
                             size_t len, uint64_t require, uint64_t alt)
{
	uint64_t start = (uint64_t)ptr;
	uint64_t end;

	if (!vmm_validate_user_range(ptr, len))
		return false;

	end = start + len;
	for (uint64_t va = start & ~(VMM_PAGE_SIZE - 1ULL);
	     va < end;
	     va += VMM_PAGE_SIZE) {
		uint64_t flags = vmm_get_pte_flags(pml4_phys, va);

		if (!(flags & PTE_PRESENT))
			return false;
		if (!(flags & PTE_USER))
			return false;
		if (require && !(flags & require) && !(alt && (flags & alt)))
			return false;
	}
	return true;
}

bool vmm_user_range_readable(uint64_t pml4_phys, const void *ptr, size_t len)
{
	return user_range_check(pml4_phys, ptr, len, 0, 0);
}

bool vmm_user_range_writable(uint64_t pml4_phys, const void *ptr, size_t len)
{
	/* Writable or COW; a write to a COW page is recoverable. */
	return user_range_check(pml4_phys, ptr, len, PTE_WRITABLE, PTE_COW);
}

/**
 * vmm_free_user_address_space - Free all user-space mappings
 * @pml4_phys: physical address of PML4 to free
 */
void vmm_free_user_address_space(uint64_t pml4_phys)
{
	uint64_t *pml4 = phys_to_virt(pml4_phys);

	for (int i = 0; i < PAGE_TABLE_ENTRIES_USER; i++) {
		uint64_t *pdpt;

		if (!(pml4[i] & PTE_PRESENT))
			continue;

		pdpt = phys_to_virt(pml4[i] & PTE_ADDR_MASK);
		for (int j = 0; j < PAGE_TABLE_ENTRIES; j++) {
			uint64_t *pd;

			if (!(pdpt[j] & PTE_PRESENT))
				continue;

			pd = phys_to_virt(pdpt[j] & PTE_ADDR_MASK);
			for (int k = 0; k < PAGE_TABLE_ENTRIES; k++) {
				uint64_t *pt;

				if (!(pd[k] & PTE_PRESENT))
					continue;

				pt = phys_to_virt(pd[k] & PTE_ADDR_MASK);
				for (int l = 0; l < PAGE_TABLE_ENTRIES; l++) {
					if (pt[l] & PTE_PRESENT)
						pmm_free(pt[l] & PTE_ADDR_MASK);
				}
				pmm_free(pd[k] & PTE_ADDR_MASK);
			}
			pmm_free(pdpt[j] & PTE_ADDR_MASK);
		}
		pmm_free(pml4[i] & PTE_ADDR_MASK);
	}
	pmm_free(pml4_phys);
}

/**
 * vmm_init - Initialize the virtual memory manager
 * @hhdm_offset: HHDM offset (already set by PMM)
 */
void vmm_init(uint64_t hhdm_offset)
{
	uint64_t cr3;

	(void)hhdm_offset;

	__asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
	kernel_pml4_phys = cr3 & PTE_ADDR_MASK;
}

/**
 * vmm_create_address_space - Create a new address space
 *
 * Return: physical address of new PML4, or 0 on failure
 */
uint64_t vmm_create_address_space(void)
{
	uint64_t new_pml4_phys;
	uint64_t *new_pml4;
	uint64_t *kernel_pml4;

	new_pml4_phys = alloc_page_table();
	if (new_pml4_phys == 0) {
		log_panic("vmm_create_address_space: failed to allocate PML4");
		return 0;
	}

	new_pml4 = phys_to_virt(new_pml4_phys);
	kernel_pml4 = phys_to_virt(kernel_pml4_phys);

	for (int i = KERNEL_PML4_START; i < PAGE_TABLE_ENTRIES; i++)
		new_pml4[i] = kernel_pml4[i];

	return new_pml4_phys;
}

/**
 * vmm_map_page - Map a virtual address to a physical address
 * @pml4_phys: physical address of PML4
 * @virt: virtual address to map
 * @phys: physical address to map to
 * @flags: page flags
 *
 * Return: 0 on success, -1 on failure
 */
int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags)
{
	uint64_t *pml4 = phys_to_virt(pml4_phys);
	int pml4_idx = PML4_INDEX(virt);
	int pdpt_idx = PDPT_INDEX(virt);
	int pd_idx = PD_INDEX(virt);
	int pt_idx = PT_INDEX(virt);
	uint64_t intermediate_flags;
	uint64_t new_pdpt_phys = 0;
	uint64_t new_pd_phys = 0;
	uint64_t new_pt_phys = 0;
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	intermediate_flags = PTE_PRESENT | PTE_WRITABLE;
	if (flags & PTE_USER)
		intermediate_flags |= PTE_USER;

	if (pml4[pml4_idx] & PTE_PRESENT) {
		pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);
		if ((flags & PTE_USER) && !(pml4[pml4_idx] & PTE_USER))
			pml4[pml4_idx] |= PTE_USER;
	} else {
		new_pdpt_phys = alloc_page_table();
		if (new_pdpt_phys == 0)
			return -1;
		pml4[pml4_idx] = new_pdpt_phys | intermediate_flags;
		pdpt = phys_to_virt(new_pdpt_phys);
	}

	if (pdpt[pdpt_idx] & PTE_PRESENT) {
		pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);
		if ((flags & PTE_USER) && !(pdpt[pdpt_idx] & PTE_USER))
			pdpt[pdpt_idx] |= PTE_USER;
	} else {
		new_pd_phys = alloc_page_table();
		if (new_pd_phys == 0) {
			if (new_pdpt_phys) {
				pml4[pml4_idx] = 0;
				pmm_free(new_pdpt_phys);
			}
			return -1;
		}
		pdpt[pdpt_idx] = new_pd_phys | intermediate_flags;
		pd = phys_to_virt(new_pd_phys);
	}

	if (pd[pd_idx] & PTE_PRESENT) {
		pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);
		if ((flags & PTE_USER) && !(pd[pd_idx] & PTE_USER))
			pd[pd_idx] |= PTE_USER;
	} else {
		new_pt_phys = alloc_page_table();
		if (new_pt_phys == 0) {
			if (new_pd_phys) {
				pdpt[pdpt_idx] = 0;
				pmm_free(new_pd_phys);
			}
			if (new_pdpt_phys) {
				pml4[pml4_idx] = 0;
				pmm_free(new_pdpt_phys);
			}
			return -1;
		}
		pd[pd_idx] = new_pt_phys | intermediate_flags;
		pt = phys_to_virt(new_pt_phys);
	}

	pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags;
	return 0;
}

/**
 * vmm_unmap_page - Remove a page mapping
 * @pml4_phys: physical address of PML4
 * @virt: virtual address to unmap
 *
 * Return: 0 on success, -1 if not mapped
 */
int vmm_unmap_page(uint64_t pml4_phys, uint64_t virt)
{
	uint64_t *pml4 = phys_to_virt(pml4_phys);
	int pml4_idx = PML4_INDEX(virt);
	int pdpt_idx = PDPT_INDEX(virt);
	int pd_idx = PD_INDEX(virt);
	int pt_idx = PT_INDEX(virt);
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	if (!(pml4[pml4_idx] & PTE_PRESENT))
		return -1;
	pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);

	if (!(pdpt[pdpt_idx] & PTE_PRESENT))
		return -1;
	pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

	if (!(pd[pd_idx] & PTE_PRESENT))
		return -1;
	pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

	pt[pt_idx] = 0;
	__asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");

	return 0;
}

/**
 * vmm_switch_address_space - Switch to a different address space
 * @pml4_phys: physical address of PML4 to activate
 */
void vmm_switch_address_space(uint64_t pml4_phys)
{
	__asm__ volatile("movq %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

/**
 * vmm_get_kernel_pml4 - Get the kernel's PML4 physical address
 *
 * Return: physical address of kernel PML4
 */
uint64_t vmm_get_kernel_pml4(void)
{
	return kernel_pml4_phys;
}

/**
 * vmm_get_physical - Get physical address for a virtual address
 * @pml4_phys: physical address of PML4 to query
 * @virt: virtual address to translate
 *
 * Return: physical address, or 0 if not mapped
 */
uint64_t vmm_get_physical(uint64_t pml4_phys, uint64_t virt)
{
	uint64_t *pml4 = phys_to_virt(pml4_phys);
	int pml4_idx = PML4_INDEX(virt);
	int pdpt_idx = PDPT_INDEX(virt);
	int pd_idx = PD_INDEX(virt);
	int pt_idx = PT_INDEX(virt);
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	if (!(pml4[pml4_idx] & PTE_PRESENT))
		return 0;
	pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);

	if (!(pdpt[pdpt_idx] & PTE_PRESENT))
		return 0;
	pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

	if (!(pd[pd_idx] & PTE_PRESENT))
		return 0;
	pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

	if (!(pt[pt_idx] & PTE_PRESENT))
		return 0;

	return pt[pt_idx] & PTE_ADDR_MASK;
}

/**
 * vmm_get_pte_flags - Get PTE flags for a virtual address
 */
uint64_t vmm_get_pte_flags(uint64_t pml4_phys, uint64_t virt)
{
	uint64_t *pml4 = phys_to_virt(pml4_phys);
	int pml4_idx = PML4_INDEX(virt);
	int pdpt_idx = PDPT_INDEX(virt);
	int pd_idx = PD_INDEX(virt);
	int pt_idx = PT_INDEX(virt);
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	if (!(pml4[pml4_idx] & PTE_PRESENT))
		return 0;
	pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);

	if (!(pdpt[pdpt_idx] & PTE_PRESENT))
		return 0;
	pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

	if (!(pd[pd_idx] & PTE_PRESENT))
		return 0;
	pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

	if (!(pt[pt_idx] & PTE_PRESENT))
		return 0;

	return pt[pt_idx] & ~PTE_ADDR_MASK;
}

/**
 * vmm_set_pte_flags - Set PTE flags for a virtual address
 */
int vmm_set_pte_flags(uint64_t pml4_phys, uint64_t virt, uint64_t flags)
{
	uint64_t *pml4 = phys_to_virt(pml4_phys);
	int pml4_idx = PML4_INDEX(virt);
	int pdpt_idx = PDPT_INDEX(virt);
	int pd_idx = PD_INDEX(virt);
	int pt_idx = PT_INDEX(virt);
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;
	uint64_t phys;

	if (!(pml4[pml4_idx] & PTE_PRESENT))
		return -1;
	pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);

	if (!(pdpt[pdpt_idx] & PTE_PRESENT))
		return -1;
	pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

	if (!(pd[pd_idx] & PTE_PRESENT))
		return -1;
	pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

	if (!(pt[pt_idx] & PTE_PRESENT))
		return -1;

	/* Keep physical address, replace flags */
	phys = pt[pt_idx] & PTE_ADDR_MASK;
	pt[pt_idx] = phys | flags;

	/* Invalidate TLB for this address */
	__asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");

	return 0;
}

/**
 * vmm_copy_address_space_cow - Copy address space with Copy-on-Write
 *
 * Deep copies all 4 levels of page tables, but shares the actual
 * physical pages with COW semantics.
 */
uint64_t vmm_copy_address_space_cow(uint64_t src_pml4_phys)
{
	uint64_t *src_pml4 = phys_to_virt(src_pml4_phys);
	uint64_t dst_pml4_phys;
	uint64_t *dst_pml4;

	/* Allocate new PML4 */
	dst_pml4_phys = alloc_page_table();
	if (dst_pml4_phys == 0) {
		log_error("VMM: COW copy failed to allocate PML4");
		return 0;
	}
	dst_pml4 = phys_to_virt(dst_pml4_phys);

	/* Copy kernel mappings (shared, not COW) */
	for (int i = KERNEL_PML4_START; i < PAGE_TABLE_ENTRIES; i++) {
		dst_pml4[i] = src_pml4[i];
	}

	/* Deep copy user space with COW */
	for (int i = 0; i < PAGE_TABLE_ENTRIES_USER; i++) {
		uint64_t *src_pdpt, *dst_pdpt;
		uint64_t dst_pdpt_phys;

		if (!(src_pml4[i] & PTE_PRESENT))
			continue;

		/* Allocate PDPT */
		dst_pdpt_phys = alloc_page_table();
		if (dst_pdpt_phys == 0)
			goto fail;
		dst_pml4[i] = dst_pdpt_phys | (src_pml4[i] & ~PTE_ADDR_MASK);

		src_pdpt = phys_to_virt(src_pml4[i] & PTE_ADDR_MASK);
		dst_pdpt = phys_to_virt(dst_pdpt_phys);

		for (int j = 0; j < PAGE_TABLE_ENTRIES; j++) {
			uint64_t *src_pd, *dst_pd;
			uint64_t dst_pd_phys;

			if (!(src_pdpt[j] & PTE_PRESENT))
				continue;

			/* Allocate PD */
			dst_pd_phys = alloc_page_table();
			if (dst_pd_phys == 0)
				goto fail;
			dst_pdpt[j] = dst_pd_phys | (src_pdpt[j] & ~PTE_ADDR_MASK);

			src_pd = phys_to_virt(src_pdpt[j] & PTE_ADDR_MASK);
			dst_pd = phys_to_virt(dst_pd_phys);

			for (int k = 0; k < PAGE_TABLE_ENTRIES; k++) {
				uint64_t *src_pt, *dst_pt;
				uint64_t dst_pt_phys;

				if (!(src_pd[k] & PTE_PRESENT))
					continue;

				/* Allocate PT */
				dst_pt_phys = alloc_page_table();
				if (dst_pt_phys == 0)
					goto fail;
				dst_pd[k] = dst_pt_phys | (src_pd[k] & ~PTE_ADDR_MASK);

				src_pt = phys_to_virt(src_pd[k] & PTE_ADDR_MASK);
				dst_pt = phys_to_virt(dst_pt_phys);

				for (int l = 0; l < PAGE_TABLE_ENTRIES; l++) {
					uint64_t pte = src_pt[l];
					uint64_t phys_addr;
					uint64_t new_flags;

					if (!(pte & PTE_PRESENT))
						continue;

					phys_addr = pte & PTE_ADDR_MASK;

					/*
					 * COW setup:
					 * - If page was writable, mark as COW + read-only
					 * - Copy flags but clear writable, set COW
					 * - Increment reference count
					 */
					new_flags = pte & ~PTE_ADDR_MASK;
					if (new_flags & PTE_WRITABLE) {
						new_flags &= ~PTE_WRITABLE;
						new_flags |= PTE_COW;
					}

					/* Update source PTE to COW as well */
					src_pt[l] = phys_addr | new_flags;

					/* Set destination PTE */
					dst_pt[l] = phys_addr | new_flags;

					/* Increment page reference count */
					page_ref(phys_addr);
				}
			}
		}
	}

	/* Flush TLB for source address space */
	__asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");

	log_debug("VMM: COW copied address space 0x%llx -> 0x%llx",
	          src_pml4_phys, dst_pml4_phys);
	return dst_pml4_phys;

fail:
	/* On failure, free partially allocated tables */
	vmm_free_user_address_space(dst_pml4_phys);
	return 0;
}
