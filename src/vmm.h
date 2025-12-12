/*
 * vmm.h - Virtual Memory Manager (VMM)
 *
 * The VMM manages virtual address spaces using x86-64's 4-level paging.
 * It creates per-process address spaces and maps virtual pages to physical pages.
 *
 * x86-64 4-Level Paging Overview:
 *
 *   A 64-bit virtual address is split into indices for each level:
 *
 *   63    48 47    39 38    30 29    21 20    12 11     0
 *   +-------+--------+--------+--------+--------+--------+
 *   | Sign  | PML4   | PDPT   |   PD   |   PT   | Offset |
 *   +-------+--------+--------+--------+--------+--------+
 *     16 bits  9 bits   9 bits   9 bits   9 bits  12 bits
 *
 *   - CR3 register points to the PML4 (Page Map Level 4) table
 *   - Each level has 512 entries (9 bits = 512)
 *   - Each entry is 8 bytes (64 bits)
 *   - Page tables are 4KB each (512 * 8 = 4096)
 *   - Final page size is 4KB (12-bit offset)
 *
 * Address Space Layout:
 *
 *   0xFFFFFFFFFFFFFFFF ┐
 *                      │ Kernel space (shared in all processes)
 *   0xFFFF800000000000 ┤ <- HHDM (physical memory direct map)
 *                      │
 *   (canonical hole)   │ Bits 48-63 must match bit 47
 *                      │
 *   0x00007FFFFFFFFFFF ┤ <- Highest user address
 *                      │ User space (per-process)
 *   0x0000000000000000 ┘
 *
 * The kernel is mapped in PML4 entries 256-511 (upper half).
 * User code/data is in entries 0-255 (lower half).
 */

#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Page size - must match PAGE_SIZE in pmm.h */
#define VMM_PAGE_SIZE 4096

/* =============================================================================
 * Page Table Entry (PTE) Flags
 *
 * These are the permission and status bits in each page table entry.
 * The CPU checks these when translating addresses.
 * =============================================================================
 */
#define PTE_PRESENT     (1ULL << 0)   /* P: Page is present/valid */
#define PTE_WRITABLE    (1ULL << 1)   /* R/W: 0=read-only, 1=read-write */
#define PTE_USER        (1ULL << 2)   /* U/S: 0=kernel-only, 1=user accessible */
#define PTE_WRITETHROUGH (1ULL << 3)  /* PWT: Write-through caching */
#define PTE_NOCACHE     (1ULL << 4)   /* PCD: Cache disabled */
#define PTE_ACCESSED    (1ULL << 5)   /* A: CPU sets on read/write (for LRU) */
#define PTE_DIRTY       (1ULL << 6)   /* D: CPU sets on write (for swapping) */
#define PTE_HUGE        (1ULL << 7)   /* PS: Large page (2MB in PD, 1GB in PDPT) */
#define PTE_GLOBAL      (1ULL << 8)   /* G: Don't invalidate on CR3 switch */
#define PTE_NX          (1ULL << 63)  /* NX: No-execute (if EFER.NXE=1) */

/*
 * Address mask for extracting physical address from PTE.
 * Bits 12-51 contain the physical page frame number.
 * Bits 0-11 are flags, bits 52-62 are available, bit 63 is NX.
 */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

/* =============================================================================
 * Virtual Address Index Extraction Macros
 *
 * Extract the 9-bit index for each level of the page table hierarchy.
 * Each index selects one of 512 entries in that level's table.
 * =============================================================================
 */
#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)  /* Bits 47-39: PML4 index */
#define PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)  /* Bits 38-30: PDPT index */
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)  /* Bits 29-21: PD index */
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)  /* Bits 20-12: PT index */

/*
 * Kernel space starts at PML4 entry 256.
 * Entries 256-511 map the upper half of the address space.
 * These are shared across all processes so kernel code is always accessible.
 */
#define KERNEL_PML4_START 256

/*
 * User address space layout.
 * These are typical Unix-like addresses for code and stack.
 * Code at low address, stack at high address (grows down).
 */
#define USER_CODE_BASE   0x400000ULL       /* Where user code is loaded */
#define USER_STACK_BASE  0x7FFFFF000ULL    /* Base of user stack page */
#define USER_HEAP_BASE   0x500000ULL       /* Where user heap starts */
/* =============================================================================
 * VMM API Functions
 * =============================================================================
 */


bool vmm_validate_user_range(const void *ptr, size_t len);


/*
 * vmm_init - Initialize the Virtual Memory Manager.
 *
 * @hhdm_offset: The HHDM offset (passed for compatibility, already set by PMM)
 *
 * Reads CR3 to capture the kernel's PML4 physical address.
 * Must be called after pmm_init().
 */
void vmm_init(uint64_t hhdm_offset);

/*
 * vmm_create_address_space - Create a new address space for a process.
 *
 * Returns: Physical address of the new PML4, or 0 on failure.
 *
 * Allocates a new PML4 table and copies the kernel mappings (entries 256-511)
 * from the kernel's PML4. This ensures the kernel is always accessible
 * regardless of which process is running.
 */
uint64_t vmm_create_address_space(void);

/*
 * vmm_map_page - Map a virtual address to a physical address.
 *
 * @pml4_phys: Physical address of the PML4 (address space to modify)
 * @virt:      Virtual address to map (must be page-aligned)
 * @phys:      Physical address to map to (must be page-aligned)
 * @flags:     Page flags (PTE_PRESENT, PTE_WRITABLE, PTE_USER, etc.)
 *
 * Returns: 0 on success, -1 on failure (out of memory for page tables)
 *
 * Walks the 4-level page table hierarchy, creating intermediate tables
 * as needed (PDPT, PD, PT), then sets the final PT entry.
 */
int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);


/*
 * vmm_unmap_page - Remove a page mapping.
 *
 * @pml4_phys: Physical address of the PML4 (address space to modify)
 * @virt:      Virtual address to unmap (must be page-aligned)
 *
 * Returns: 0 on success, -1 if the page was not mapped
 *
 * Walks the page table hierarchy to find and clear the final PT entry.
 * Frees intermediate tables if they become empty.
 */
int vmm_unmap_page(uint64_t pml4_phys, uint64_t virt);


/*
 * vmm_switch_address_space - Switch to a different address space.
 *
 * @pml4_phys: Physical address of the new PML4 to activate
 *
 * Writes to CR3, which:
 *   1. Activates the new page tables
 *   2. Flushes the TLB (except for global pages)
 */
void vmm_switch_address_space(uint64_t pml4_phys);

/*
 * vmm_get_kernel_pml4 - Get the kernel's PML4 physical address.
 *
 * Returns: Physical address of the kernel's PML4.
 *
 * Used when returning from userspace to switch back to kernel context.
 */
uint64_t vmm_get_kernel_pml4(void);

#endif /* VMM_H */
