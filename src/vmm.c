/*
 * vmm.c - Virtual Memory Manager
 *
 * Implements 4-level paging for x86-64 with per-process address spaces.
 *
 * Page Table Walk Example:
 *
 *   To translate virtual address 0x0000000000401000:
 *
 *   1. Extract indices:
 *      PML4 index = (0x401000 >> 39) & 0x1FF = 0
 *      PDPT index = (0x401000 >> 30) & 0x1FF = 0
 *      PD index   = (0x401000 >> 21) & 0x1FF = 2
 *      PT index   = (0x401000 >> 12) & 0x1FF = 1
 *
 *   2. Walk the hierarchy:
 *      CR3 -> PML4[0] -> PDPT[0] -> PD[2] -> PT[1] -> Physical Page
 *
 *   3. Final address = (PT[1] & ADDR_MASK) | (0x401000 & 0xFFF)
 */

#include "vmm.h"
#include "pmm.h"
#include "console.h"
#include "memory.h"
#include <stdint.h>

/* Number of entries in each page table level (512 entries * 8 bytes = 4KB) */
#define PAGE_TABLE_ENTRIES 512
#define PAGE_TABLE_ENTRIES_USER 256

/* =============================================================================
 * VMM Global State
 * =============================================================================
 */

/* Physical address of the kernel's PML4 (captured during vmm_init) */
static uint64_t kernel_pml4_phys;

/* =============================================================================
 * Internal Helper Functions
 * =============================================================================
 */

/*
 * alloc_page_table - Allocate and zero-initialize a new page table.
 *
 * Returns: Physical address of the new table, or 0 on failure.
 *
 * Page tables must be zeroed because:
 *   - Non-zero entries might be interpreted as present
 *   - Could cause page faults or security issues
 */
static uint64_t alloc_page_table(void) {
    uint64_t phys = pmm_alloc();
    if (phys == 0) {
        return 0;
    }

    /* Zero all 512 entries via HHDM */
    uint64_t *table = phys_to_virt(phys);
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        table[i] = 0;
    }

    return phys;
}

bool vmm_validate_user_range(const void *ptr, size_t len) {
    uint64_t addr = (uint64_t)ptr;
    // Check pointer is in user space
    if (addr >= USER_SPACE_TOP) return false;
    // Check end doesn't overflow
    if (len > USER_SPACE_TOP - addr) return false;
    return true;
}


void vmm_free_user_address_space(uint64_t pml4_phys) {
    uint64_t *pml4 = phys_to_virt(pml4_phys);
    
    // Only walk user-space (entries 0-255)
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & PTE_PRESENT)) continue;
        
        uint64_t *pdpt = phys_to_virt(pml4[i] & PTE_ADDR_MASK);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & PTE_PRESENT)) continue;
            
            uint64_t *pd = phys_to_virt(pdpt[j] & PTE_ADDR_MASK);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PTE_PRESENT)) continue;
                
                uint64_t *pt = phys_to_virt(pd[k] & PTE_ADDR_MASK);
                for (int l = 0; l < 512; l++) {
                    if (pt[l] & PTE_PRESENT) {
                        pmm_free(pt[l] & PTE_ADDR_MASK);  // Free data page
                    }
                }
                pmm_free(pd[k] & PTE_ADDR_MASK);  // Free PT
            }
            pmm_free(pdpt[j] & PTE_ADDR_MASK);  // Free PD
        }
        pmm_free(pml4[i] & PTE_ADDR_MASK);  // Free PDPT
    }
    pmm_free(pml4_phys);  // Free PML4 itself
}

/* =============================================================================
 * VMM Public API Implementation
 * =============================================================================
 */

void vmm_init(uint64_t hhdm_offset) {
    (void)hhdm_offset;  /* HHDM already set by pmm_init via g_hhdm_offset */

    /*
     * Read CR3 to capture the kernel's PML4 physical address.
     * CR3 contains the physical address of the current PML4, plus some flags.
     * We mask out the flags (bits 0-11) to get just the address.
     */
    uint64_t cr3;
    asm volatile ("movq %%cr3, %0" : "=r"(cr3));
    kernel_pml4_phys = cr3 & PTE_ADDR_MASK;
}

uint64_t vmm_create_address_space(void) {
    /* Allocate new PML4 */
    uint64_t new_pml4_phys = alloc_page_table();
    if (new_pml4_phys == 0) {
        puts("vmm_create_address_space: failed to allocate PML4\n");
        return 0;
    }

    uint64_t *new_pml4 = phys_to_virt(new_pml4_phys);
    uint64_t *kernel_pml4 = phys_to_virt(kernel_pml4_phys);

    /* Copy kernel mappings (upper half: entries 256-511) */
    for (int i = KERNEL_PML4_START; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4_phys;
}

int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = phys_to_virt(pml4_phys);

    /* Extract indices */
    int pml4_idx = PML4_INDEX(virt);
    int pdpt_idx = PDPT_INDEX(virt);
    int pd_idx   = PD_INDEX(virt);
    int pt_idx   = PT_INDEX(virt);

    /* Get or create PDPT */
    uint64_t *pdpt;
    if (pml4[pml4_idx] & PTE_PRESENT) {
        pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pdpt_phys = alloc_page_table();
        if (pdpt_phys == 0) return -1;
        /* Set USER on intermediate entry if mapping user page */
        pml4[pml4_idx] = pdpt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pdpt = phys_to_virt(pdpt_phys);
    }

    /* Get or create PD */
    uint64_t *pd;
    if (pdpt[pdpt_idx] & PTE_PRESENT) {
        pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pd_phys = alloc_page_table();
        if (pd_phys == 0) return -1;
        pdpt[pdpt_idx] = pd_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pd = phys_to_virt(pd_phys);
    }

    /* Get or create PT */
    uint64_t *pt;
    if (pd[pd_idx] & PTE_PRESENT) {
        pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pt_phys = alloc_page_table();
        if (pt_phys == 0) return -1;
        pd[pd_idx] = pt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pt = phys_to_virt(pt_phys);
    }

    /* Map the page with requested flags */
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags;

    return 0;
}

int vmm_unmap_page(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = phys_to_virt(pml4_phys);

    /* Extract indices */
    int pml4_idx = PML4_INDEX(virt);
    int pdpt_idx = PDPT_INDEX(virt);
    int pd_idx   = PD_INDEX(virt);
    int pt_idx   = PT_INDEX(virt);

    /* Get PDPT */
    uint64_t *pdpt;
    if (pml4[pml4_idx] & PTE_PRESENT) {
        pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);
    } else {
        return -1;  /* PDPT not present */
    }

    /* Get PD */
    uint64_t *pd;
    if (pdpt[pdpt_idx] & PTE_PRESENT) {
        pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    } else {
        return -1;  /* PD not present */
    }

    /* Get PT */
    uint64_t *pt;
    if (pd[pd_idx] & PTE_PRESENT) {
        pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);
    } else {
        return -1;  /* PT not present */
    }

    /* Unmap the page */
    pt[pt_idx] = 0;

    return 0;
}

void vmm_switch_address_space(uint64_t pml4_phys) {
    asm volatile ("movq %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t vmm_get_kernel_pml4(void) {
    return kernel_pml4_phys;
}


uint64_t vmm_get_physical(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = phys_to_virt(pml4_phys);

    /* Extract indices */
    int pml4_idx = PML4_INDEX(virt);
    int pdpt_idx = PDPT_INDEX(virt);
    int pd_idx   = PD_INDEX(virt);
    int pt_idx   = PT_INDEX(virt);

    /* Get PDPT */
    uint64_t *pdpt;
    if (pml4[pml4_idx] & PTE_PRESENT) {
        pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);
    } else {
        return 0;  /* Not mapped */
    }

    /* Get PD */
    uint64_t *pd;
    if (pdpt[pdpt_idx] & PTE_PRESENT) {
        pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    } else {
        return 0;  /* Not mapped */
    }

    /* Get PT */
    uint64_t *pt;
    if (pd[pd_idx] & PTE_PRESENT) {
        pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);
    } else {
        return 0;  /* Not mapped */
    }

    /* Get final physical address */
    if (pt[pt_idx] & PTE_PRESENT) {
        return pt[pt_idx] & PTE_ADDR_MASK;
    } else {
        return 0;  /* Not mapped */
    }
}