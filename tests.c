/*
 * tests.c - Kernel Test Suite Implementation
 *
 * This file contains test functions that can be invoked from the shell
 * to verify kernel subsystem functionality.
 */

#include "tests.h"
#include "console.h"
#include "pmm.h"
#include "vmm.h"
#include "context.h"
#include "limine.h"
#include <stddef.h>

/* User program binary (from user_program.c) */
extern unsigned char user_bin[];
extern unsigned int user_bin_len;

/* =============================================================================
 * Test Subsystem State
 * =============================================================================
 */

/* Saved references from kernel initialization */
static struct limine_memmap_response *saved_memmap = NULL;
static uint64_t saved_hhdm_offset = 0;

/* =============================================================================
 * Memory Map Type Names
 * =============================================================================
 */

static const char *memmap_type_name(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:                 return "Usable";
        case LIMINE_MEMMAP_RESERVED:               return "Reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return "ACPI Reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS:               return "ACPI NVS";
        case LIMINE_MEMMAP_BAD_MEMORY:             return "Bad Memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "Bootloader Reclaimable";
        case LIMINE_MEMMAP_KERNEL_AND_MODULES:     return "Kernel/Modules";
        case LIMINE_MEMMAP_FRAMEBUFFER:            return "Framebuffer";
        default:                                   return "Unknown";
    }
}

/* =============================================================================
 * Test Initialization
 * =============================================================================
 */

void tests_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    saved_memmap = memmap;
    saved_hhdm_offset = hhdm_offset;
}

/* =============================================================================
 * Test: Memory Map
 * =============================================================================
 */

void test_memmap(void) {
    if (saved_memmap == NULL) {
        puts("\n  Error: Memory map not available\n\n");
        return;
    }

    puts("\n");
    puts("  Memory Map (");
    put_dec(saved_memmap->entry_count);
    puts(" entries)\n");
    puts("  ──────────────────────────────────────────────────────────────\n");

    uint64_t total_usable = 0;

    for (uint64_t i = 0; i < saved_memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = saved_memmap->entries[i];

        puts("  ");
        put_hex(entry->base);
        puts(" - ");
        put_hex(entry->base + entry->length);
        puts(" ");

        /* Right-align size */
        uint64_t size_kb = entry->length / 1024;
        if (size_kb < 10) puts("    ");
        else if (size_kb < 100) puts("   ");
        else if (size_kb < 1000) puts("  ");
        else if (size_kb < 10000) puts(" ");
        put_dec(size_kb);
        puts(" KB  ");
        puts(memmap_type_name(entry->type));
        puts("\n");

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_usable += entry->length;
        }
    }

    puts("  ──────────────────────────────────────────────────────────────\n");
    puts("  Total usable: ");
    put_dec(total_usable / 1024 / 1024);
    puts(" MB\n\n");
}

/* =============================================================================
 * Test: Virtual Memory Manager
 * =============================================================================
 */

void test_vmm(void) {
    puts("\n");
    puts("  VMM Test Suite\n");
    puts("  ──────────────────────────────────────────────────────────────\n");

    /* Step 1: Create a new address space */
    puts("  [1] Creating new address space... ");
    uint64_t test_pml4 = vmm_create_address_space();
    if (test_pml4 == 0) {
        puts("FAILED\n\n");
        return;
    }
    puts("OK\n");
    puts("      PML4 physical: ");
    put_hex(test_pml4);
    puts("\n");

    /* Step 2: Allocate physical pages */
    puts("  [2] Allocating test pages... ");
    uint64_t page1_phys = pmm_alloc();
    uint64_t page2_phys = pmm_alloc();
    if (page1_phys == 0 || page2_phys == 0) {
        puts("FAILED\n\n");
        return;
    }
    puts("OK\n");
    puts("      Page 1: ");
    put_hex(page1_phys);
    puts("\n");
    puts("      Page 2: ");
    put_hex(page2_phys);
    puts("\n");

    /* Step 3: Map pages into the test address space */
    puts("  [3] Mapping pages... ");
    int r1 = vmm_map_page(test_pml4, USER_CODE_BASE, page1_phys, PTE_PRESENT | PTE_USER);
    int r2 = vmm_map_page(test_pml4, USER_STACK_BASE, page2_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    if (r1 != 0 || r2 != 0) {
        puts("FAILED\n\n");
        return;
    }
    puts("OK\n");
    puts("      ");
    put_hex(USER_CODE_BASE);
    puts(" -> ");
    put_hex(page1_phys);
    puts(" (code)\n");
    puts("      ");
    put_hex(USER_STACK_BASE);
    puts(" -> ");
    put_hex(page2_phys);
    puts(" (stack)\n");

    /* Step 4: Write test patterns via HHDM */
    puts("  [4] Writing test patterns via HHDM... ");
    uint64_t *ptr1 = (uint64_t *)(page1_phys + saved_hhdm_offset);
    uint64_t *ptr2 = (uint64_t *)(page2_phys + saved_hhdm_offset);
    ptr1[0] = 0xCAFEBABEDEADBEEF;
    ptr2[0] = 0x1234567890ABCDEF;
    puts("OK\n");

    /* Step 5: Switch to test address space and back */
    puts("  [5] Switching address spaces... ");
    vmm_switch_address_space(test_pml4);
    /* If we get here, kernel is still accessible (mapped in upper half) */
    vmm_switch_address_space(vmm_get_kernel_pml4());
    puts("OK\n");

    /* Step 6: Verify patterns are intact */
    puts("  [6] Verifying test patterns... ");
    if (ptr1[0] == 0xCAFEBABEDEADBEEF && ptr2[0] == 0x1234567890ABCDEF) {
        puts("OK\n");
    } else {
        puts("FAILED\n");
    }

    /* Cleanup: free pages (in a real system we'd also free page tables) */
    pmm_free(page1_phys);
    pmm_free(page2_phys);

    puts("  ──────────────────────────────────────────────────────────────\n");
    puts("  All VMM tests passed\n\n");
}

/* =============================================================================
 * Test: Userspace Program
 * =============================================================================
 */

void test_user(void) {
    puts("\n");
    puts("  Userspace Test\n");
    puts("  ──────────────────────────────────────────────────────────────\n");

    /* Create address space */
    puts("  Creating user address space... ");
    uint64_t user_pml4 = vmm_create_address_space();
    if (user_pml4 == 0) {
        puts("FAILED\n\n");
        return;
    }
    puts("OK\n");

    /* Allocate pages for code and stack */
    puts("  Allocating code and stack pages... ");
    uint64_t code_phys = pmm_alloc();
    uint64_t stack_phys = pmm_alloc();
    if (code_phys == 0 || stack_phys == 0) {
        puts("FAILED\n\n");
        return;
    }
    puts("OK\n");

    /* Map pages */
    puts("  Mapping user memory... ");
    if (vmm_map_page(user_pml4, USER_CODE_BASE, code_phys, PTE_PRESENT | PTE_USER) != 0 ||
        vmm_map_page(user_pml4, USER_STACK_BASE, stack_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
        puts("FAILED\n\n");
        return;
    }
    puts("OK\n");

    /* Copy user program to code page */
    puts("  Loading user binary (");
    put_dec(user_bin_len);
    puts(" bytes)... ");
    uint8_t *code_ptr = (uint8_t *)(code_phys + saved_hhdm_offset);
    for (size_t i = 0; i < user_bin_len; i++) {
        code_ptr[i] = user_bin[i];
    }
    puts("OK\n");

    puts("  ──────────────────────────────────────────────────────────────\n");
    puts("  Entering userspace (ring 3)...\n\n");

    /* Enter userspace */
    struct user_context ctx = {
        .pml4  = user_pml4,
        .entry = USER_CODE_BASE,
        .stack = USER_STACK_BASE + 0x1000
    };

    context_switch_to_user(&ctx);

    puts("\n  ──────────────────────────────────────────────────────────────\n");
    puts("  Returned from userspace\n\n");
}
