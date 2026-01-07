/*
 * limine.h - Limine Boot Protocol Types
 *
 * Subset for framebuffer support.
 */

#ifndef LIMINE_H
#define LIMINE_H

#include "types.h"

#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

#define LIMINE_HHDM_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b

#define LIMINE_MEMMAP_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62
#define LIMINE_MODULE_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x3e7e279702be32af, 0xca1c4f3bd1280cee

#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES     6
#define LIMINE_MEMMAP_FRAMEBUFFER            7


struct limine_memmap_entry {
    uint64_t base;      /* Physical address of region start */
    uint64_t length;    /* Size in bytes */
    uint64_t type;      /* One of the LIMINE_MEMMAP_* constants */
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;  /* Array of pointers to entries */
};

struct limine_memmap_request {
    uint64_t id[4];                         /* Magic identifier */
    uint64_t revision;                      /* Request revision (0) */
    struct limine_memmap_response *response; /* Filled by Limine */
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;    /* Add this to physical address to get virtual address */
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

#define LIMINE_HHDM_REQUEST \
    __attribute__((used, section(".limine_requests"))) \
    static volatile struct limine_hhdm_request hhdm_request = { \
        .id = { LIMINE_HHDM_REQUEST_MAGIC }, \
        .revision = 0, \
        .response = (void *)0 \
    }

/*
 * Macro to declare a memory map request.
 * The 'used' attribute prevents the compiler from optimizing it away.
 * The 'section' attribute puts it in a known location (not .bss).
 */
#define LIMINE_MEMMAP_REQUEST \
    __attribute__((used, section(".limine_requests"))) \
    static volatile struct limine_memmap_request memmap_request = { \
        .id = { LIMINE_MEMMAP_REQUEST_MAGIC }, \
        .revision = 0, \
        .response = (void *)0 \
    }

#define LIMINE_FRAMEBUFFER_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b

struct limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

#define LIMINE_FRAMEBUFFER_REQUEST \
    __attribute__((used, section(".limine_requests"))) \
    static volatile struct limine_framebuffer_request framebuffer_request = { \
        .id = { LIMINE_FRAMEBUFFER_REQUEST_MAGIC }, \
        .revision = 0, \
        .response = (void *)0 \
    }

/* RSDP (Root System Description Pointer) request for ACPI */
#define LIMINE_RSDP_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0xc5e77b6b397e7b43, 0x27637845accdcf3c

struct limine_rsdp_response {
    uint64_t revision;
    void *address;  /* Physical address of the RSDP */
};

struct limine_rsdp_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_rsdp_response *response;
};

#define LIMINE_RSDP_REQUEST \
    __attribute__((used, section(".limine_requests"))) \
    static volatile struct limine_rsdp_request rsdp_request = { \
        .id = { LIMINE_RSDP_REQUEST_MAGIC }, \
        .revision = 0, \
        .response = (void *)0 \
    }

struct limine_framebuffer *limine_get_framebuffer(void);
struct limine_memmap_response *limine_get_memmap(void);
uint64_t limine_get_hhdm_offset(void);
void *limine_get_rsdp(void);

#endif /* LIMINE_H */
