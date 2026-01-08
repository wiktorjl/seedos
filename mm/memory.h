/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Physical/virtual address conversion via HHDM
 *
 * The Higher Half Direct Map (HHDM) linearly maps all physical memory
 * into a region of virtual address space. Physical address P is accessible
 * at virtual address (P + g_hhdm_offset).
 */

#ifndef _MEMORY_H
#define _MEMORY_H

#include "types.h"

#define PATH_MAX	256
#define USER_SPACE_TOP	0x0000800000000000ULL

/**
 * g_hhdm_offset - Base virtual address of the HHDM region
 *
 * Set during pmm_init() from Limine's HHDM response.
 * Typical value: 0xFFFF800000000000
 */
extern uint64_t g_hhdm_offset;

/**
 * phys_to_virt - Convert physical address to virtual
 * @phys: physical address to convert
 *
 * Return: virtual address that maps to the given physical address
 */
static inline void *phys_to_virt(uint64_t phys)
{
	return (void *)(phys + g_hhdm_offset);
}

/**
 * virt_to_phys - Convert virtual address to physical
 * @virt: virtual address within the HHDM region
 *
 * Return: corresponding physical address
 *
 * Note: Only works for addresses in the HHDM region.
 */
static inline uint64_t virt_to_phys(void *virt)
{
	return (uint64_t)virt - g_hhdm_offset;
}

#endif /* _MEMORY_H */
