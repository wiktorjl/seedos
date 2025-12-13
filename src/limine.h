/*
 * limine.h - Limine boot protocol structures
 *
 * This defines the binary interface between Limine and our kernel.
 * Limine scans the loaded kernel for magic byte patterns to find
 * request structures, then fills in response pointers before jumping
 * to the entry point.
 */

#ifndef LIMINE_H
#define LIMINE_H

#include <stdint.h>

/*
 * Base revision - protocol version handshake
 *
 * Limine checks this to ensure it supports the protocol version
 * we're requesting. If it doesn't recognize our revision, it will
 * refuse to boot.
 */
#define LIMINE_BASE_REVISION 3

/*
 * Magic number construction
 *
 * Each request type has a unique 256-bit identifier (four 64-bit values).
 * The first two are common to all requests, the last two identify the
 * specific request type.
 */
#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

/*
 * Memory map request
 *
 * Asks Limine for a map of physical memory regions, indicating
 * which areas are usable, reserved, ACPI, etc.
 */
#define LIMINE_MEMMAP_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62
#define LIMINE_MODULE_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x3e7e279702be32af, 0xca1c4f3bd1280cee

/* Memory region types */
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


#define LIMINE_FRAMEBUFFER_REQUEST \
    __attribute__((used, section(".limine_requests"))) \
    static volatile struct limine_framebuffer_request fb_request = { \
        .id = { LIMINE_FRAMEBUFFER_REQUEST_MAGIC }, \
        .revision = 0, \
        .response = (void *)0 \
    }
    
/*
 * HHDM (Higher Half Direct Map) request
 *
 * Limine maps all physical memory starting at a high virtual address.
 * This offset lets us convert physical addresses to virtual addresses:
 *     virtual = physical + hhdm_offset
 */
#define LIMINE_HHDM_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b

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

/*
 * Base revision declaration
 * Must be present for Limine to recognize this as a valid Limine kernel.
 */
#define LIMINE_BASE_REVISION_DECLARATION \
    __attribute__((used, section(".limine_requests"))) \
    static volatile uint64_t limine_base_revision[3] = { \
        0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, \
        LIMINE_BASE_REVISION \
    }

/*
 * Framebuffer request
 *
 * Asks Limine for a linear framebuffer for graphics output.
 */
#define LIMINE_FRAMEBUFFER_REQUEST_MAGIC LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b

struct limine_framebuffer {
    void *address;           /* Virtual address of framebuffer memory */
    uint64_t width;          /* Width in pixels */
    uint64_t height;         /* Height in pixels */
    uint64_t pitch;          /* Bytes per row (may include padding) */
    uint16_t bpp;            /* Bits per pixel */
    uint8_t memory_model;    /* 1 = RGB */
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
    /* Mode list follows in newer revisions */
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

struct limine_file {
    uint64_t revision;  /* Revision of this structure */
    void *address;      /* Virtual address where module is loaded */
    uint64_t size;      /* Size in bytes */
    char *path;         /* Null-terminated path string */
};
struct limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    struct limine_file **modules;
};
struct limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_module_response *response;
};

#define LIMINE_MODULE_REQUEST \
    __attribute__((used, section(".limine_requests"))) \
    static volatile struct limine_module_request module_request = { \
        .id = { LIMINE_MODULE_REQUEST_MAGIC }, \
        .revision = 0, \
        .response = (void *)0 \
    }

#endif /* LIMINE_H */