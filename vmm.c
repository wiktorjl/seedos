/*
 * vmm.c - Virtual Memory Manager
 *
 * 4-level paging for x86-64 with per-process address spaces.
 */

#include "vmm.h"
#include "pmm.h"
#include "console.h"

/* Global state */
static uint64_t vmm_hhdm_offset;    /* HHDM offset for phys<->virt conversion */
static uint64_t kernel_pml4_phys;   /* Physical address of kernel's PML4 */

/* Convert physical address to virtual via HHDM */
static inline void *vmm_phys_to_virt(uint64_t phys) {
    return (void *)(phys + vmm_hhdm_offset);
}

/* Convert virtual address to physical */
static inline uint64_t vmm_virt_to_phys(void *virt) {
    return (uint64_t)virt - vmm_hhdm_offset;
}

/*
 * Allocate and zero a page table.
 * Returns physical address of new table, or 0 on failure.
 */
static uint64_t vmm_alloc_table(void) {
    uint64_t phys = pmm_alloc();
    if (phys == 0) {
        return 0;
    }

    /* Zero the page table - critical for safety */
    uint64_t *table = vmm_phys_to_virt(phys);
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }

    return phys;
}

void vmm_init(uint64_t hhdm_offset) {
    vmm_hhdm_offset = hhdm_offset;

    /* Read current CR3 to get kernel's PML4 */
    uint64_t cr3;
    asm volatile ("movq %%cr3, %0" : "=r"(cr3));
    kernel_pml4_phys = cr3 & PTE_ADDR_MASK;

    puts("VMM initialized\n");
    puts("  HHDM offset: ");
    put_hex(vmm_hhdm_offset);
    puts("\n");
    puts("  Kernel PML4: ");
    put_hex(kernel_pml4_phys);
    puts("\n");
}

uint64_t vmm_create_address_space(void) {
    /* Allocate new PML4 */
    uint64_t new_pml4_phys = vmm_alloc_table();
    if (new_pml4_phys == 0) {
        puts("vmm_create_address_space: failed to allocate PML4\n");
        return 0;
    }

    uint64_t *new_pml4 = vmm_phys_to_virt(new_pml4_phys);
    uint64_t *kernel_pml4 = vmm_phys_to_virt(kernel_pml4_phys);

    /* Copy kernel mappings (upper half: entries 256-511) */
    for (int i = KERNEL_PML4_START; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4_phys;
}

int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = vmm_phys_to_virt(pml4_phys);

    /* Extract indices */
    int pml4_idx = PML4_INDEX(virt);
    int pdpt_idx = PDPT_INDEX(virt);
    int pd_idx   = PD_INDEX(virt);
    int pt_idx   = PT_INDEX(virt);

    /* Get or create PDPT */
    uint64_t *pdpt;
    if (pml4[pml4_idx] & PTE_PRESENT) {
        pdpt = vmm_phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pdpt_phys = vmm_alloc_table();
        if (pdpt_phys == 0) return -1;
        /* Set USER on intermediate entry if mapping user page */
        pml4[pml4_idx] = pdpt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pdpt = vmm_phys_to_virt(pdpt_phys);
    }

    /* Get or create PD */
    uint64_t *pd;
    if (pdpt[pdpt_idx] & PTE_PRESENT) {
        pd = vmm_phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pd_phys = vmm_alloc_table();
        if (pd_phys == 0) return -1;
        pdpt[pdpt_idx] = pd_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pd = vmm_phys_to_virt(pd_phys);
    }

    /* Get or create PT */
    uint64_t *pt;
    if (pd[pd_idx] & PTE_PRESENT) {
        pt = vmm_phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pt_phys = vmm_alloc_table();
        if (pt_phys == 0) return -1;
        pd[pd_idx] = pt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pt = vmm_phys_to_virt(pt_phys);
    }

    /* Map the page with requested flags */
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags;

    return 0;
}

void vmm_switch_address_space(uint64_t pml4_phys) {
    asm volatile ("movq %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t vmm_get_kernel_pml4(void) {
    return kernel_pml4_phys;
}
