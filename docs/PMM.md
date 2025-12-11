# Physical Memory Management

This document explains how operating systems manage physical RAM, surveys approaches used by major operating systems, and provides a detailed walkthrough of Seed OS's bitmap-based physical memory manager.

## Table of Contents

1. [What is Physical Memory Management?](#what-is-physical-memory-management)
2. [The Fundamental Problem](#the-fundamental-problem)
3. [Common Allocation Strategies](#common-allocation-strategies)
4. [How Major OSes Do It](#how-major-oses-do-it)
5. [Our Design Decisions](#our-design-decisions)
6. [Implementation Walkthrough](#implementation-walkthrough)
7. [Usage Examples](#usage-examples)
8. [Limitations and Future Work](#limitations-and-future-work)

---

## What is Physical Memory Management?

Physical memory management is the kernel's responsibility to track which parts of physical RAM are in use and which are available. This is distinct from *virtual* memory management, which deals with address spaces and page tables.

### Physical vs Virtual Memory

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           VIRTUAL ADDRESS SPACE                             │
│                                                                             │
│  Process A sees:          Process B sees:          Kernel sees:             │
│  0x400000 → code          0x400000 → code          0xFFFF...80000000 → code │
│  0x7FF... → stack         0x7FF... → stack                                  │
│                                                                             │
│  (Each process thinks     (Same addresses,         (Kernel has its own      │
│   it has all memory)       different data!)         address space)          │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Page tables translate
                                    │ virtual → physical
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHYSICAL MEMORY (RAM)                             │
│                                                                             │
│  0x00000000 ┬─────────────────────────────────────────────────────────────┐ │
│             │ Reserved (BIOS, ACPI, etc.)                                 │ │
│  0x00100000 ├─────────────────────────────────────────────────────────────┤ │
│             │ Kernel code and data                                        │ │
│  0x00400000 ├─────────────────────────────────────────────────────────────┤ │
│             │ Free pages (managed by PMM)                                 │ │
│             │   Page at 0x00500000 → Process A's code                     │ │
│             │   Page at 0x00501000 → Process B's stack                    │ │
│             │   Page at 0x00502000 → Free                                 │ │
│             │   Page at 0x00503000 → Kernel heap                          │ │
│             │   ...                                                       │ │
│  0x???????? └─────────────────────────────────────────────────────────────┘ │
│             (Top of physical RAM)                                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

**The PMM's job:** Track those "free pages" and hand them out when the kernel or VMM needs physical memory.

### Why Pages?

Memory is managed in fixed-size chunks called **pages** (typically 4KB on x86-64). Why not allocate arbitrary byte ranges?

1. **Hardware support**: The MMU (Memory Management Unit) works in pages
2. **Reduced fragmentation**: Fixed sizes prevent small gaps that can't be used
3. **Simpler bookkeeping**: Track N pages instead of arbitrary ranges
4. **Efficient page tables**: Virtual memory maps pages to frames

```
Physical RAM divided into 4KB page frames:
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│ Frame  │ Frame  │ Frame  │ Frame  │ Frame  │ Frame  │ Frame  │
│   0    │   1    │   2    │   3    │   4    │   5    │   6    │ ...
│ 0x0000 │ 0x1000 │ 0x2000 │ 0x3000 │ 0x4000 │ 0x5000 │ 0x6000 │
└────────┴────────┴────────┴────────┴────────┴────────┴────────┘
  4KB      4KB      4KB      4KB      4KB      4KB      4KB
```

---

## The Fundamental Problem

At boot, the kernel receives a **memory map** from the bootloader (in our case, Limine). This map describes physical memory regions:

```
Memory Map from Limine:
  0x0000000000000000 - 0x000000000009FC00 (639 KB)  Usable
  0x000000000009FC00 - 0x00000000000A0000 (1 KB)    Reserved
  0x00000000000F0000 - 0x0000000000100000 (64 KB)   Reserved
  0x0000000000100000 - 0x000000003FEF0000 (1021 MB) Usable
  0x000000003FEF0000 - 0x000000003FF00000 (64 KB)   ACPI Reclaimable
  0x00000000FFFC0000 - 0x0000000100000000 (256 KB)  Reserved
```

The PMM must:
1. Parse this map to understand available memory
2. Create a data structure to track page allocation status
3. Provide fast allocation and deallocation operations
4. Never allocate reserved regions (would crash or corrupt data)

---

## Common Allocation Strategies

### 1. Bitmap Allocator

**Concept:** One bit per page. Bit = 0 means free, bit = 1 means used.

```
Bitmap for 32 pages (4 bytes):
Byte 0:    Byte 1:    Byte 2:    Byte 3:
11110000   00001111   00000000   11111111
│││││││└── Page 0: used (reserved)
││││││└─── Page 1: used
│││││└──── Page 2: used
││││└───── Page 3: used
│││└────── Page 4: free ←── Next allocation returns this
...
                              └── Pages 24-31: all used
```

**Pros:**
- Simple to implement
- Constant memory overhead: N pages requires N/8 bytes
- Easy to visualize and debug

**Cons:**
- O(n) allocation time (must scan for free bit)
- No efficient contiguous allocation
- Cache-unfriendly for large bitmaps

### 2. Free List

**Concept:** Maintain a linked list of free pages. Each free page's memory stores a pointer to the next free page.

```
Free list structure:

head ──► [Page at 0x5000] ──► [Page at 0x8000] ──► [Page at 0x12000] ──► NULL
              │                     │                     │
              └── next ptr          └── next ptr          └── next ptr
                  stored in             stored in             stored in
                  page itself           page itself           page itself
```

**Pros:**
- O(1) allocation and deallocation
- Zero additional memory (uses free pages themselves)

**Cons:**
- Must touch memory on every operation (cache misses)
- No efficient way to find contiguous ranges
- Corruption of free pages breaks the allocator

### 3. Buddy Allocator

**Concept:** Divide memory into power-of-two sized blocks. When allocating, split larger blocks. When freeing, merge adjacent "buddy" blocks.

```
Initial: One 16-page block
┌───────────────────────────────────────────────────────────────┐
│                         16 pages                              │
└───────────────────────────────────────────────────────────────┘

After allocating 2 pages (splits down):
┌───────────────────────────────┬───────────────────────────────┐
│           8 pages             │           8 pages             │
├───────────────┬───────────────┤               (free)          │
│   4 pages     │   4 pages     │                               │
├───────┬───────┤   (free)      │                               │
│2 pages│2 pages│               │                               │
│(alloc)│(free) │               │                               │
└───────┴───────┴───────────────┴───────────────────────────────┘

Free lists: order-1 (2pg): [0x2000]
            order-2 (4pg): [0x4000]
            order-3 (8pg): [0x8000]
```

**Pros:**
- O(log n) allocation and deallocation
- Efficient contiguous allocation (up to power-of-two sizes)
- Good memory utilization with merging

**Cons:**
- Internal fragmentation (must round up to power of two)
- More complex implementation
- Multiple free lists to manage

---

## How Major OSes Do It

### Linux: Buddy Allocator + Slabs

Linux uses a sophisticated multi-level approach:

**Level 1: Buddy Allocator (page allocator)**
- Manages physical pages in power-of-two blocks (order 0-10, i.e., 1 to 1024 pages)
- Separate free lists per order per memory zone (DMA, Normal, HighMem)
- `alloc_pages(gfp_mask, order)` allocates 2^order contiguous pages

**Level 2: Slab Allocator (object caching)**
- Built on top of the buddy allocator
- Caches frequently-allocated objects (inodes, task_structs, etc.)
- Reduces fragmentation for small allocations
- Three implementations: SLAB, SLUB (default), SLOB (embedded)

**Level 3: Per-CPU Page Caches**
- Each CPU has a local cache of free pages
- Reduces lock contention on the global buddy allocator
- Batch refills from the buddy system

```
Linux Memory Allocation Stack:

  kmalloc(size)  ←── Kernel code requests memory
       │
       ▼
  ┌─────────────┐
  │ Slab/SLUB   │  Caches common object sizes (32, 64, 128, ... bytes)
  └──────┬──────┘
         │ Requests pages when cache empty
         ▼
  ┌─────────────┐
  │ Per-CPU     │  Each CPU has local page cache
  │ Page Cache  │
  └──────┬──────┘
         │ Refills from buddy allocator
         ▼
  ┌─────────────┐
  │   Buddy     │  Manages physical pages in power-of-2 blocks
  │  Allocator  │  order-0: 4KB, order-1: 8KB, ... order-10: 4MB
  └─────────────┘
```

### macOS/Darwin: Zone Allocator

macOS uses a zone-based approach:

**Mach Zone Allocator:**
- Memory divided into "zones" for different allocation sizes
- Each zone manages objects of a single size
- Garbage collection can compact zones

**Page-Level (vm_page):**
- Buddy-like system for page management
- Integrated with Mach virtual memory system
- Pages tracked with `vm_page` structures

```
macOS Memory Zones:

Zone: "kalloc.16"    → 16-byte objects
Zone: "kalloc.32"    → 32-byte objects
Zone: "kalloc.64"    → 64-byte objects
...
Zone: "vm_page"      → vm_page structures
Zone: "ipc_port"     → Mach port structures
```

### Windows: Page Frame Number Database

Windows NT uses a different approach centered on the Page Frame Number (PFN) database:

**PFN Database:**
- Array of structures, one per physical page frame
- Each entry tracks: state, reference count, owning process, position in lists
- States: Free, Zeroed, Standby, Modified, Bad, etc.

**Page Lists:**
- Free page list (uninitialized pages)
- Zeroed page list (pre-zeroed, ready for secure allocation)
- Standby list (unmapped but contents preserved for soft fault)
- Modified list (dirty pages waiting to be written)

```
Windows PFN Database:

PFN Entry structure:
┌────────────────────────────────────────┐
│ State: Active/Standby/Modified/Free    │
│ Reference Count                        │
│ PTE Address (if mapped)                │
│ Share Count                            │
│ List links (prev/next)                 │
│ Color (for NUMA/cache optimization)    │
└────────────────────────────────────────┘

Page transitions:

         ┌──── Soft fault ────┐
         │                    │
         ▼                    │
     ┌───────┐           ┌─────────┐
     │ Free  │──────────►│ Zeroed  │ (background zeroing thread)
     └───────┘           └────┬────┘
         ▲                    │
         │                    ▼ Allocation
    Write to disk        ┌─────────┐
         │               │ Active  │ (in use by process)
         │               └────┬────┘
         │                    │ Unmapped
    ┌────┴─────┐              ▼
    │ Modified │◄────── ┌──────────┐
    └──────────┘        │ Standby  │ (contents preserved)
                        └──────────┘
```

### Comparison Summary

| OS | Primary Allocator | Small Objects | Special Features |
|----|-------------------|---------------|------------------|
| Linux | Buddy | SLUB slab | Per-CPU caches, memory cgroups |
| macOS | Zone | Zone allocator | Garbage collection, compaction |
| Windows | PFN Database | Pool allocator | Page lists, pre-zeroing thread |
| **Seed OS** | **Bitmap** | N/A (not yet) | Simple, educational |

---

## Our Design Decisions

For Seed OS, we chose a **bitmap allocator** for these reasons:

### Why Bitmap?

1. **Simplicity**: ~100 lines of code, easy to understand and debug
2. **Correctness first**: Fewer bugs than complex structures
3. **Educational value**: The concept maps directly to the problem
4. **Sufficient for hobby OS**: We're not optimizing for large servers

### Design Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          PMM Data Structures                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  bitmap (uint8_t*)                                                          │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬─────────────────────┐   │
│  │FF │FF │00 │00 │00 │80 │...│...│...│...│...│FF │  (bitmap_size bytes)│   │
│  └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴─────────────────────┘   │
│    │                                                                        │
│    └── Each bit = one 4KB page                                              │
│        0 = free, 1 = used                                                   │
│                                                                             │
│  total_pages: Number of pages being tracked (highest_addr / 4096)           │
│  free_pages:  Count of currently free pages                                 │
│  bitmap_size: Size of bitmap in bytes ((total_pages + 7) / 8)               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

1. **Mark all as used first, then free known-good regions**
   - Safer than the reverse: unknown regions are treated as reserved
   - Prevents accidental allocation of hardware-mapped memory

2. **Page 0 is never allocated**
   - NULL pointer dereferences (accessing address 0) should fault
   - We explicitly reserve physical page 0

3. **Linear allocation scan**
   - Simple but O(n) - acceptable for our use case
   - Starts from page 0 each time (could be optimized with "next free" hint)

4. **Bitmap stored in physical memory via HHDM**
   - Bitmap lives in the first suitable usable region
   - Accessed via Higher Half Direct Map (physical + offset = virtual)

---

## Implementation Walkthrough

Let's walk through the implementation in detail.

### File Structure

```
pmm.h  - Public API and documentation
pmm.c  - Implementation
memory.h - Helper macros (phys_to_virt, virt_to_phys)
```

### Global State (pmm.c:46-49)

```c
static uint8_t *bitmap;           /* Pointer to bitmap array (virtual address) */
static uint64_t bitmap_size;      /* Size of bitmap in bytes */
static uint64_t total_pages;      /* Total number of pages being tracked */
static uint64_t free_pages;       /* Number of currently free pages */
```

These are `static` (file-local) to encapsulate the allocator's state. External code uses the API functions.

### Bitmap Manipulation (pmm.c:63-80)

```c
static inline void bitmap_mark_used(uint64_t page_index) {
    bitmap[page_index / 8] |= (1 << (page_index % 8));
}
```

**How it works:**
- `page_index / 8`: Which byte in the bitmap array
- `page_index % 8`: Which bit within that byte
- `|= (1 << bit)`: Set that bit to 1

**Example:** Mark page 13 as used
```
page_index = 13
byte_index = 13 / 8 = 1
bit_index  = 13 % 8 = 5

bitmap[1] |= (1 << 5)
bitmap[1] |= 0b00100000

Before: bitmap[1] = 0b00001111  (pages 8-11 used)
After:  bitmap[1] = 0b00101111  (pages 8-11 and 13 used)
```

The `bitmap_mark_free` function does the inverse:
```c
static inline void bitmap_mark_free(uint64_t page_index) {
    bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}
```

`&= ~(1 << bit)` clears the bit: AND with all 1s except the target bit.

### Initialization (pmm.c:82-178)

The initialization is a 6-step process:

#### Step 1: Find highest physical address

```c
uint64_t highest_addr = 0;
for (uint64_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];
    uint64_t entry_end = entry->base + entry->length;
    if (entry_end > highest_addr) {
        highest_addr = entry_end;
    }
}
total_pages = highest_addr / PAGE_SIZE;
bitmap_size = (total_pages + 7) / 8;
```

We scan all memory regions (usable or not) to find the top of physical memory. This determines how many pages we need to track.

**Example:** If highest address is 4GB (0x100000000):
```
total_pages = 0x100000000 / 4096 = 1,048,576 pages
bitmap_size = (1,048,576 + 7) / 8 = 131,072 bytes = 128 KB
```

#### Step 2: Find space for the bitmap

```c
uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
uint64_t bitmap_phys = 0;

for (uint64_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];
    if (entry->type != LIMINE_MEMMAP_USABLE) {
        continue;
    }
    if (entry->length >= bitmap_pages * PAGE_SIZE) {
        bitmap_phys = entry->base;
        break;
    }
}
```

We find the first usable region large enough to hold the bitmap. The bitmap must be in usable memory because we'll write to it.

#### Step 3: Mark everything as used

```c
bitmap = phys_to_virt(bitmap_phys);

for (uint64_t i = 0; i < bitmap_size; i++) {
    bitmap[i] = 0xFF;  /* All bits set = all pages used */
}
free_pages = 0;
```

**Why mark all used first?** This is a defensive approach. Any region we don't explicitly mark as free will remain "used." This prevents us from accidentally allocating:
- Reserved BIOS regions
- ACPI tables
- Memory-mapped I/O
- Regions the memory map didn't tell us about

`phys_to_virt()` converts the physical address to a virtual address using the HHDM:
```c
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + g_hhdm_offset);
}
```

#### Step 4: Mark usable regions as free

```c
for (uint64_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];
    if (entry->type != LIMINE_MEMMAP_USABLE) {
        continue;
    }

    uint64_t base_page = entry->base / PAGE_SIZE;
    uint64_t page_count = entry->length / PAGE_SIZE;

    for (uint64_t p = 0; p < page_count; p++) {
        bitmap_mark_free(base_page + p);
        free_pages++;
    }
}
```

Now we walk the memory map again, this time only looking at `USABLE` regions. Each page in these regions is marked free.

#### Step 5: Reserve the bitmap's own pages

```c
uint64_t bitmap_base_page = bitmap_phys / PAGE_SIZE;
for (uint64_t p = 0; p < bitmap_pages; p++) {
    if (!bitmap_is_used(bitmap_base_page + p)) {
        bitmap_mark_used(bitmap_base_page + p);
        free_pages--;
    }
}
```

The bitmap itself occupies physical memory! We marked that region as "usable" in step 4, but we've now written our bitmap there. We must mark those pages as used so we don't allocate over our own data structure.

#### Step 6: Reserve page 0

```c
if (!bitmap_is_used(0)) {
    bitmap_mark_used(0);
    free_pages--;
}
```

Physical page 0 (addresses 0x0000-0x0FFF) must never be allocated. If it were, and we mapped it into a process's address space, NULL pointer dereferences would succeed instead of faulting. This is a security and debugging hazard.

### Allocation (pmm.c:180-194)

```c
uint64_t pmm_alloc(void) {
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_is_used(i)) {
            bitmap_mark_used(i);
            free_pages--;
            return i * PAGE_SIZE;
        }
    }
    return 0;  /* Out of memory */
}
```

This is a simple linear scan. We check each page starting from 0 until we find a free one.

**Time complexity:** O(n) where n = total pages. For 4GB RAM, this could check up to 1 million bits. In practice, we find a free page quickly because most memory is free.

**Optimization opportunity:** Track "last allocated page" to start scanning from there, implementing a "next-fit" strategy.

**Return value:**
- Success: Physical address of the allocated page (guaranteed to be page-aligned)
- Failure: 0 (which is why we never allocate page 0!)

### Deallocation (pmm.c:196-204)

```c
void pmm_free(uint64_t phys_addr) {
    uint64_t page_index = phys_addr / PAGE_SIZE;

    if (page_index < total_pages && bitmap_is_used(page_index)) {
        bitmap_mark_free(page_index);
        free_pages++;
    }
}
```

Freeing is O(1) - we directly compute the bit position and clear it.

**Safety checks:**
- `page_index < total_pages`: Don't write beyond the bitmap
- `bitmap_is_used(page_index)`: Don't double-free (would corrupt free_pages count)

Double-free is silently ignored rather than crashing. This is a design choice - a production system might want to panic on double-free to catch bugs.

---

## Usage Examples

### Example 1: Basic Allocation

```c
// Allocate a page for a new page table
uint64_t page_table_phys = pmm_alloc();
if (page_table_phys == 0) {
    panic("Out of memory!");
}

// Convert to virtual address to write to it
uint64_t *page_table = (uint64_t *)phys_to_virt(page_table_phys);

// Zero the page table
for (int i = 0; i < 512; i++) {
    page_table[i] = 0;
}
```

### Example 2: Allocating User Memory

```c
// Allocate physical pages for a user process
uint64_t code_phys = pmm_alloc();
uint64_t stack_phys = pmm_alloc();

// Map into user's address space
vmm_map_page(user_pml4, 0x400000, code_phys, PTE_PRESENT | PTE_USER);
vmm_map_page(user_pml4, 0x7FFFFF000, stack_phys,
             PTE_PRESENT | PTE_WRITABLE | PTE_USER);
```

### Example 3: Freeing Memory

```c
// When a process exits, free its physical pages
void process_cleanup(struct process *proc) {
    for (int i = 0; i < proc->page_count; i++) {
        pmm_free(proc->phys_pages[i]);
    }
}
```

### Example 4: Checking Memory Status

```c
void print_memory_stats(void) {
    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    uint64_t used = total - free;

    printf("Memory: %llu MB free, %llu MB used, %llu MB total\n",
           free * 4 / 1024,
           used * 4 / 1024,
           total * 4 / 1024);
}
```

---

## Limitations and Future Work

### Current Limitations

1. **O(n) allocation**: Linear scan is slow for large memory
2. **No contiguous allocation**: Can't allocate multiple adjacent pages efficiently
3. **No NUMA awareness**: Doesn't consider memory locality
4. **No memory zones**: Can't separate DMA-capable memory from high memory

### Potential Improvements

**Short-term (Easy):**
- Add "next-fit" hint to speed up allocation
- Add `pmm_alloc_zeroed()` that returns a pre-zeroed page
- Add statistics (allocation count, peak usage)

**Medium-term (Moderate):**
- Implement free list for O(1) allocation
- Add `pmm_alloc_contiguous(n)` for multi-page allocations

**Long-term (Complex):**
- Buddy allocator for efficient contiguous allocation
- Memory zones (DMA, Normal, High)
- NUMA-aware allocation
- Slab allocator on top for small objects

### The Path to a Real Allocator

```
Current: Bitmap (O(n) alloc, O(1) free)
    │
    ▼
Phase 1: Bitmap + free list hybrid
         - Free list for fast single-page alloc
         - Bitmap for contiguous alloc and status queries
    │
    ▼
Phase 2: Buddy allocator
         - O(log n) alloc/free
         - Efficient contiguous allocation
    │
    ▼
Phase 3: Buddy + Slab
         - Buddy for pages
         - Slab for objects (kmalloc equivalent)
    │
    ▼
Phase 4: Full production allocator
         - Per-CPU caches
         - NUMA awareness
         - Memory compaction
```

For a hobby OS, the bitmap allocator is perfectly adequate. It's only when you have many processes frequently allocating and freeing memory that performance becomes a concern.

---

## Further Reading

- [OSDev Wiki: Page Frame Allocation](https://wiki.osdev.org/Page_Frame_Allocation)
- [Linux Kernel: Physical Page Allocation](https://www.kernel.org/doc/html/latest/core-api/memory-allocation.html)
- [The Slab Allocator (Bonwick)](https://www.usenix.org/legacy/publications/library/proceedings/bos94/full_papers/bonwick.a)
- [Understanding the Linux Virtual Memory Manager (Gorman)](https://www.kernel.org/doc/gorman/html/understand/)
- [Windows Internals, 7th Ed. (Russinovich et al.)](https://docs.microsoft.com/en-us/sysinternals/) - Chapter 5: Memory Management
