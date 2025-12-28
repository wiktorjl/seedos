#include "limine.h"

// Limine base revision - Limine modifies index 1 to indicate support
volatile uint64_t limine_base_revision[3] __attribute__((section(".limine_requests"))) = {
    0xf9562b2d5c95a6c8,
    0x6a7b384944536bdc,
    3
};

// Framebuffer request
static volatile uint64_t framebuffer_request[6] __attribute__((section(".limine_requests"))) = {
    0xc7b1dd30df4c8b88,  // LIMINE_COMMON_MAGIC[0]
    0x0a82e883a194f07b,  // LIMINE_COMMON_MAGIC[1]
    0x9d5827dcd881dd75,  // LIMINE_FRAMEBUFFER_REQUEST[2]
    0xa3148604f6fab11b,  // LIMINE_FRAMEBUFFER_REQUEST[3]
    0,                   // revision
    0                    // response pointer (Limine fills this)
};

// Get the framebuffer, or NULL if unavailable
struct limine_framebuffer *limine_get_framebuffer(void) {
    struct limine_framebuffer_response *response =
        (struct limine_framebuffer_response *)framebuffer_request[5];

    if (response == 0 || response->framebuffer_count == 0) {
        return 0;
    }

    return response->framebuffers[0];
}
