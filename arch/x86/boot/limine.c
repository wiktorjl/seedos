// Limine boot protocol requests

#include "limine.h"

/*
 * Base revision: Limine replaces magic1 with the supported revision number.
 * We use revision 3 (current), which only maps Usable, Bootloader reclaimable,
 * Modules, and Framebuffer memory to the HHDM. ACPI regions must be manually
 * mapped by the kernel before accessing them.
 */
volatile uint64_t limine_base_revision[3] __attribute__((section(".limine_requests"))) = {
    0xf9562b2d5c95a6c8,     // magic0
    0x6a7b384944536bdc,     // magic1 (replaced on success)
    3                       // requested revision
};

LIMINE_FRAMEBUFFER_REQUEST;
LIMINE_HHDM_REQUEST;
LIMINE_MEMMAP_REQUEST;
LIMINE_RSDP_REQUEST;

struct limine_memmap_response *limine_get_memmap(void) {
    if (memmap_request.response == NULL)
        return 0;
    return memmap_request.response;
}

uint64_t limine_get_hhdm_offset(void) {
    if (hhdm_request.response == NULL)
        return 0;
    return hhdm_request.response->offset;
}

struct limine_framebuffer *limine_get_framebuffer(void) {
    if (framebuffer_request.response == 0 ||
        framebuffer_request.response->framebuffer_count == 0)
        return 0;

    return framebuffer_request.response->framebuffers[0];
}

void *limine_get_rsdp(void) {
    if (rsdp_request.response == NULL)
        return 0;
    return rsdp_request.response->address;
}
