// SPDX-License-Identifier: GPL-2.0-only
/*
 * Limine boot protocol interface
 *
 * Bootloader requests and accessor functions for memory map,
 * HHDM offset, framebuffer, and RSDP.
 */

#include "limine.h"

/*
 * Limine base revision 3: ACPI/reserved regions not in HHDM,
 * kernel must map them explicitly before access.
 */
volatile uint64_t limine_base_revision[3] __attribute__((section(".limine_requests"))) = {
    0xf9562b2d5c95a6c8,     /* magic0 */
    0x6a7b384944536bdc,     /* magic1 (replaced on success) */
    3                       /* requested revision */
};

LIMINE_FRAMEBUFFER_REQUEST;
LIMINE_HHDM_REQUEST;
LIMINE_MEMMAP_REQUEST;
LIMINE_RSDP_REQUEST;

/**
 * limine_get_memmap - Get memory map from bootloader
 *
 * Return: Pointer to memory map response, or NULL if unavailable
 */
struct limine_memmap_response *limine_get_memmap(void)
{
    if (memmap_request.response == NULL)
        return 0;
    return memmap_request.response;
}

/**
 * limine_get_hhdm_offset - Get HHDM virtual address offset
 *
 * Return: HHDM offset, or 0 if unavailable
 */
uint64_t limine_get_hhdm_offset(void)
{
    if (hhdm_request.response == NULL)
        return 0;
    return hhdm_request.response->offset;
}

/**
 * limine_get_framebuffer - Get primary framebuffer
 *
 * Return: Pointer to framebuffer structure, or NULL if unavailable
 */
struct limine_framebuffer *limine_get_framebuffer(void)
{
    if (framebuffer_request.response == 0 ||
        framebuffer_request.response->framebuffer_count == 0)
        return 0;

    return framebuffer_request.response->framebuffers[0];
}

/**
 * limine_get_rsdp - Get ACPI RSDP physical address
 *
 * Return: Physical address of RSDP, or NULL if unavailable
 */
void *limine_get_rsdp(void)
{
    if (rsdp_request.response == NULL)
        return 0;
    return rsdp_request.response->address;
}
