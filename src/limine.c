// Limine boot protocol requests
//
// Limine scans the kernel binary for magic byte sequences to find requests.
// Each request has: [common_magic (2), request_id (2), revision, response_ptr]
// On boot, Limine fills in response_ptr with a pointer to the response struct.

#include "limine.h"

// Base revision: Limine replaces magic1 with the supported revision number.
// We request revision 3; if magic1 is unchanged after boot, it's unsupported.
volatile uint64_t limine_base_revision[3] __attribute__((section(".limine_requests"))) = {
    0xf9562b2d5c95a6c8,     // magic0
    0x6a7b384944536bdc,     // magic1 (replaced on success)
    3                       // requested revision
};

// Framebuffer request: asks Limine to set up a linear framebuffer.
static volatile uint64_t framebuffer_request[6] __attribute__((section(".limine_requests"))) = {
    0xc7b1dd30df4c8b88,     // common_magic[0]
    0x0a82e883a194f07b,     // common_magic[1]
    0x9d5827dcd881dd75,     // framebuffer_request_id[0]
    0xa3148604f6fab11b,     // framebuffer_request_id[1]
    0,                      // revision
    0                       // response (filled by Limine)
};

struct limine_framebuffer *limine_get_framebuffer(void) {
    struct limine_framebuffer_response *response =
        (struct limine_framebuffer_response *)framebuffer_request[5];

    if (response == 0 || response->framebuffer_count == 0)
        return 0;

    return response->framebuffers[0];
}
