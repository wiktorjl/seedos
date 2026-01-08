# SeedOS Memory Management Subsystem

## Overview

| Layer | Component | Purpose |
|-------|-----------|---------|
| Low | PMM | Manages physical RAM pages |
| Mid | VMM | Manages page tables and address spaces |
| High | Heap | Provides kmalloc/kfree |

### Initialization Order

```c
pmm_init(memmap, hhdm_offset);  // First: physical memory
vmm_init(hhdm_offset);          // Second: virtual memory
kheap_init();                   // Third: kernel heap
```

## Higher Half Direct Map (HHDM)

```c
extern uint64_t g_hhdm_offset;
void *phys_to_virt(uint64_t phys) { return (void *)(phys + g_hhdm_offset); }
uint64_t virt_to_phys(void *virt) { return (uint64_t)virt - g_hhdm_offset; }
```

## Physical Memory Manager (PMM)

Bitmap allocator with first-fit linear scan:

```c
uint64_t pmm_alloc(void);       // Allocate one 4KB page
void pmm_free(uint64_t addr);   // Free a page
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_usable_pages(void);
```

## Virtual Memory Manager (VMM)

x86-64 4-level paging (PML4 -> PDPT -> PD -> PT):

```c
int vmm_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_unmap_page(uint64_t pml4, uint64_t virt);
uint64_t vmm_create_address_space(void);
```

### Page Table Entry Flags

```c
#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITABLE  (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_NOCACHE   (1ULL << 4)
#define PTE_NX        (1ULL << 63)
```

## Kernel Heap

Free-list allocator with first-fit, block splitting, and forward coalescing:

```c
void *kmalloc(size_t size);
void *kzalloc(size_t size);    // Zero-initialized
void kfree(void *ptr);
void *krealloc(void *ptr, size_t size);
```

## Memory Layout

| Region | Address |
|--------|---------|
| User Code | `0x400000` |
| User Stack | `0x7FFFF0000` - `0x800000000` |
| HHDM | `0xFFFF800000000000` |
| Kernel Heap | `0xFFFFFFFF00000000` |
| Kernel | `0xFFFFFFFF80000000` |
