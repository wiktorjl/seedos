# Virtual Memory Management - Deep Dive

This document provides a comprehensive look at virtual memory management: the general concepts, how production operating systems implement it, and a detailed walkthrough of MyOS's implementation.

## Table of Contents

1. [What is Virtual Memory?](#what-is-virtual-memory)
2. [Why Virtual Memory Matters](#why-virtual-memory-matters)
3. [Core Concepts](#core-concepts)
4. [How Major Operating Systems Do It](#how-major-operating-systems-do-it)
5. [MyOS Design Decisions](#myos-design-decisions)
6. [Implementation Walkthrough](#implementation-walkthrough)
7. [Code Deep Dive](#code-deep-dive)

---

## What is Virtual Memory?

Virtual memory is an abstraction that gives each process the illusion of having its own private, contiguous address space, regardless of the actual physical memory layout.

```
Without Virtual Memory:              With Virtual Memory:

Process A thinks:                    Process A thinks:
"I own address 0x1000"               "I own address 0x400000"
         ↓                                    ↓
    Physical 0x1000                      (MMU translates)
         ↓                                    ↓
CONFLICT! Process B also             Physical 0x50000 (A's private copy)
wants 0x1000!
                                     Process B thinks:
                                     "I own address 0x400000"
                                              ↓
                                         (MMU translates)
                                              ↓
                                     Physical 0x80000 (B's private copy)
```

The **Memory Management Unit (MMU)** is hardware that sits between the CPU and memory, translating every memory access from virtual to physical addresses using **page tables**.

---

## Why Virtual Memory Matters

### 1. Process Isolation
Each process has its own address space. Process A cannot read or write Process B's memory, even if they use the same virtual addresses. This is the foundation of operating system security.

### 2. Simplified Programming Model
Programs don't need to know where they're loaded in physical memory. Every program can be compiled to run at the same virtual address (e.g., `0x400000`).

### 3. Memory Protection
The OS can mark pages as read-only, no-execute, or kernel-only. This prevents:
- Code injection (mark data pages as no-execute)
- Accidental overwrites (mark code as read-only)
- Privilege escalation (mark kernel pages as supervisor-only)

### 4. Efficient Memory Use
- **Demand paging**: Pages are only loaded when accessed
- **Copy-on-write**: Forked processes share pages until one writes
- **Swapping**: Rarely-used pages can be evicted to disk
- **Memory-mapped files**: Files appear as memory regions

### 5. Shared Memory
Multiple processes can map the same physical page, enabling:
- Shared libraries (one copy of libc in physical RAM)
- Inter-process communication
- Memory-mapped I/O

---

## Core Concepts

### Pages and Frames

```
VIRTUAL MEMORY (Pages)              PHYSICAL MEMORY (Frames)
┌─────────────────────┐             ┌─────────────────────┐
│ Page 0 (4KB)        │────────────→│ Frame 37            │
├─────────────────────┤             ├─────────────────────┤
│ Page 1 (4KB)        │──┐          │ Frame 38            │
├─────────────────────┤  │          ├─────────────────────┤
│ Page 2 (4KB)        │  │     ┌───→│ Frame 39            │
├─────────────────────┤  │     │    ├─────────────────────┤
│ Page 3 (unmapped)   │  └─────┼───→│ Frame 40            │
├─────────────────────┤        │    ├─────────────────────┤
│ Page 4 (4KB)        │────────┘    │ Frame 41            │
└─────────────────────┘             └─────────────────────┘

- Virtual pages don't need to map to contiguous physical frames
- Some pages may be unmapped (accessing them causes a fault)
- Multiple virtual pages can map to the same physical frame
```

### Page Tables

Page tables are data structures that define the virtual-to-physical mapping. On x86-64, they form a 4-level hierarchy:

```
CR3 (root)
    │
    ▼
┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
│  PML4   │────→│  PDPT   │────→│   PD    │────→│   PT    │────→ Physical Page
│512 entries│   │512 entries│   │512 entries│   │512 entries│
└─────────┘     └─────────┘     └─────────┘     └─────────┘

Each entry: 64 bits = physical address of next level + permission flags
```

### Translation Lookaside Buffer (TLB)

Walking 4 levels of page tables for every memory access would be slow. The TLB is a cache that stores recent translations:

```
Virtual Address ─────┬────→ [TLB] ─── HIT ───→ Physical Address (fast!)
                     │         │
                     │        MISS
                     │         │
                     │         ▼
                     └────→ [Page Table Walk] → Physical Address (slow)
                                   │
                                   ▼
                              Update TLB
```

**TLB invalidation** happens when:
- CR3 is changed (full flush, except global pages)
- INVLPG instruction (single page flush)

---

## How Major Operating Systems Do It

### Linux

Linux uses a sophisticated VMM with many optimizations:

**Page Table Structure:**
- 4 or 5 levels (5-level for > 256TB address spaces)
- Uses huge pages (2MB, 1GB) to reduce TLB pressure
- Kernel mapped in upper addresses (KASLR randomizes base)

**Key Data Structures:**
```c
struct mm_struct {           // Per-process memory descriptor
    pgd_t *pgd;              // Page Global Directory (PML4)
    struct vm_area_struct *mmap;  // List of VMAs
    // ... statistics, locks, etc.
};

struct vm_area_struct {      // Virtual Memory Area
    unsigned long vm_start;  // Start address
    unsigned long vm_end;    // End address
    unsigned long vm_flags;  // Permissions (VM_READ, VM_WRITE, VM_EXEC)
    struct file *vm_file;    // Backing file (or NULL for anonymous)
    // ... linked list pointers, operations, etc.
};
```

**VMAs (Virtual Memory Areas):**
Instead of tracking individual pages, Linux groups contiguous ranges with the same properties into VMAs:

```
Process address space as VMAs:

0x400000 ─────────┐
                  │ VMA: code (r-x, file-backed from executable)
0x401000 ─────────┘
0x600000 ─────────┐
                  │ VMA: data (rw-, file-backed)
0x601000 ─────────┘
0x602000 ─────────┐
                  │ VMA: heap (rw-, anonymous, grows up)
0x700000 ─────────┘
    ...
0x7fff0000 ───────┐
                  │ VMA: stack (rw-, anonymous, grows down)
0x7fffffff ───────┘
```

**Demand Paging:**
Pages are not allocated until accessed. When a process touches an unmapped page:
1. Page fault occurs
2. Kernel checks if address is in a valid VMA
3. If yes: allocate physical page, update page table, resume
4. If no: segmentation fault (SIGSEGV)

**Copy-on-Write (COW):**
When `fork()` is called:
1. Child gets a copy of parent's page tables (not physical pages!)
2. All writable pages marked read-only in both processes
3. On write: page fault triggers, kernel copies the page, marks both writable

### macOS / Darwin (XNU Kernel)

macOS uses the Mach microkernel's VM system, which influenced many modern designs:

**Two-Layer Design:**
```
┌─────────────────────────────────────────────┐
│           BSD Layer (higher level)          │
│  - mmap(), munmap(), mprotect()             │
│  - File-backed mappings                     │
└─────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────┐
│         Mach VM Layer (lower level)         │
│  - vm_map (address space)                   │
│  - vm_object (memory object)                │
│  - vm_page (physical page)                  │
└─────────────────────────────────────────────┘
```

**Memory Objects:**
Mach introduces the concept of **memory objects** - abstract entities that can provide page contents. A memory object could be:
- Anonymous memory (zeroed pages)
- A file (pages read from disk)
- Device memory (framebuffer, etc.)
- Another process's memory (for sharing)

**Key Concepts:**
- **vm_map**: Collection of vm_map_entry structs describing address ranges
- **vm_object**: Source of page contents, can be chained (shadow objects for COW)
- **Pagers**: External processes that handle page-in/page-out (though most are in-kernel now)

**Universal Page Tables:**
macOS supports both Intel (x86-64) and Apple Silicon (ARM64). The VM layer is hardware-agnostic; platform-specific code (pmap layer) handles actual page table manipulation.

### Windows NT

Windows uses a different terminology but similar concepts:

**Key Components:**
```
┌─────────────────────────────────────────────┐
│           Memory Manager (Mm)               │
│  - Page fault handling                      │
│  - Working set management                   │
│  - Modified page writer                     │
└─────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────┐
│        Virtual Address Descriptors          │
│  - VAD tree (balanced binary tree)          │
│  - Describes committed/reserved regions     │
└─────────────────────────────────────────────┘
```

**Virtual Address Descriptors (VADs):**
Similar to Linux VMAs, but organized as a self-balancing tree for O(log n) lookups:

```c
// Simplified VAD structure
struct MMVAD {
    ULONG_PTR StartingVpn;   // Starting virtual page number
    ULONG_PTR EndingVpn;     // Ending virtual page number
    ULONG Protection;         // PAGE_READONLY, PAGE_READWRITE, etc.
    struct MMVAD *Left;       // Left child in AVL tree
    struct MMVAD *Right;      // Right child in AVL tree
    // ...
};
```

**Page States:**
Windows tracks pages through multiple states:
- **Free**: Not part of any process
- **Zeroed**: Free and zeroed (ready for secure allocation)
- **Standby**: Was in a working set, now available but contents preserved
- **Modified**: Needs to be written to disk before reuse
- **Active**: In a process's working set

**Working Sets:**
Windows aggressively manages which pages are "resident" (mapped in page tables):
- Each process has a working set (resident pages)
- Working set trimmer removes pages under memory pressure
- Soft faults (page still in standby) are fast; hard faults (disk read) are slow

### Comparison Table

| Feature | Linux | macOS | Windows |
|---------|-------|-------|---------|
| Region tracking | VMA linked list + rb-tree | vm_map_entry | VAD tree |
| Page table levels | 4-5 | 4 (Intel), 3-4 (ARM) | 4 |
| COW mechanism | Shadow page tables | Shadow objects | Prototype PTEs |
| Huge pages | 2MB, 1GB (explicit) | Superpage coalescing | Large pages (2MB) |
| Kernel mapping | Shared upper half | Shared upper half | Shared upper half |
| ASLR | Yes (stack, heap, libs) | Yes (aggressive) | Yes |
| Page size | 4KB (default) | 16KB (ARM), 4KB (Intel) | 4KB |

---

## MyOS Design Decisions

MyOS takes a minimalist approach, implementing the core VMM functionality needed to run userspace programs.

### Design Goals

1. **Simplicity over performance**: Linear scans instead of trees
2. **Clarity**: Well-commented code that teaches concepts
3. **Correctness**: Get the basics right before optimizing

### What We Implement

```
┌────────────────────────────────────────────────────────────┐
│                     MyOS VMM Features                      │
├────────────────────────────────────────────────────────────┤
│ ✓ 4-level page tables (PML4 → PDPT → PD → PT)             │
│ ✓ Per-process address spaces                               │
│ ✓ Kernel mapped in all address spaces (entries 256-511)   │
│ ✓ User/kernel separation via PTE_USER flag                │
│ ✓ HHDM for physical memory access                         │
│ ✓ Basic page mapping (vmm_map_page)                       │
│ ✓ Address space switching (CR3 manipulation)              │
├────────────────────────────────────────────────────────────┤
│ ✗ Demand paging (pages pre-allocated)                     │
│ ✗ Copy-on-write                                            │
│ ✗ Page swapping to disk                                    │
│ ✗ VMAs / region tracking                                   │
│ ✗ Huge pages                                               │
│ ✗ Page unmapping / freeing                                 │
└────────────────────────────────────────────────────────────┘
```

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                          kernel.c                               │
│  Creates user contexts, calls context_switch_to_user()          │
└─────────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
        ┌──────────┐   ┌──────────┐   ┌──────────────┐
        │  vmm.c   │   │  pmm.c   │   │ context_asm.S│
        │          │   │          │   │              │
        │ - Create │   │ - Alloc  │   │ - Switch CR3 │
        │   PML4   │   │   pages  │   │ - Enter ring3│
        │ - Map    │   │ - Track  │   │ - Return     │
        │   pages  │   │   bitmap │   │              │
        └──────────┘   └──────────┘   └──────────────┘
              │               │
              └───────┬───────┘
                      ▼
              ┌──────────────┐
              │  memory.h    │
              │              │
              │ phys_to_virt │
              │ virt_to_phys │
              └──────────────┘
```

### Key Design Choices

**1. Bitmap Allocator for Physical Pages**

We use a simple bitmap instead of a buddy allocator or free list:

```
Pros:
+ Simple to implement and understand
+ Constant memory overhead (1 bit per page)
+ No fragmentation in the allocator itself

Cons:
- O(n) allocation (linear scan)
- Can't efficiently allocate contiguous ranges
- No size classes for different allocation sizes
```

**2. No VMA Tracking**

Production OSes track memory regions to:
- Handle page faults intelligently
- Implement mmap/munmap
- Support file-backed mappings

We skip this because:
- Our user programs are simple and pre-mapped
- We don't have demand paging yet
- It would add significant complexity

**3. Copy Kernel Entries, Not Deep Copy**

When creating a new address space, we copy PML4 entries 256-511:

```c
for (int i = 256; i < 512; i++) {
    new_pml4[i] = kernel_pml4[i];
}
```

This means all processes share the same kernel PDPT/PD/PT structures. Changes to kernel mappings are automatically visible everywhere. This is standard practice in production OSes too.

**4. Intermediate Tables Always Have Full Permissions**

When we create intermediate page tables (PDPT, PD, PT), we mark them with:
```c
pdpt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER
```

The final PT entry determines actual permissions. This is correct because:
- Permissions are AND-ed through the hierarchy
- If any level denies access, access is denied
- Setting permissive flags on intermediates lets the final entry decide

---

## Implementation Walkthrough

Let's trace through exactly what happens when we create and use a user address space.

### Step 1: Initialize the VMM

```c
// In kernel.c, after PMM is ready:
vmm_init(hhdm_response->offset);
```

What happens in `vmm_init()`:

```c
void vmm_init(uint64_t hhdm_offset) {
    (void)hhdm_offset;  // Already set by PMM

    // Read CR3 to get the kernel's PML4 physical address
    uint64_t cr3;
    asm volatile ("movq %%cr3, %0" : "=r"(cr3));
    kernel_pml4_phys = cr3 & PTE_ADDR_MASK;
    // kernel_pml4_phys is now something like 0x1000000
}
```

The bootloader (Limine) already set up page tables for the kernel. We just capture that PML4 address so we can copy it later.

### Step 2: Create a User Address Space

```c
uint64_t user_pml4 = vmm_create_address_space();
```

What happens:

```
1. Allocate a fresh 4KB page for the new PML4
   └─→ pmm_alloc() returns physical address, e.g., 0x200000

2. Zero all 512 entries (via HHDM)
   └─→ All entries start as "not present"

3. Copy kernel entries (256-511) from kernel's PML4
   └─→ Now kernel code/data is accessible in this address space

Result:
┌─────────────────────────────────┐
│ new_pml4 @ physical 0x200000    │
├─────────────────────────────────┤
│ Entry 0:   0 (not present)      │ ← User space (empty)
│ Entry 1:   0                    │
│   ...                           │
│ Entry 255: 0                    │
├─────────────────────────────────┤
│ Entry 256: [copied from kernel] │ ← Kernel space (shared)
│ Entry 257: [copied from kernel] │
│   ...                           │
│ Entry 511: [copied from kernel] │
└─────────────────────────────────┘
```

### Step 3: Allocate Physical Pages for User Code and Stack

```c
uint64_t code_phys = pmm_alloc();   // e.g., 0x201000
uint64_t stack_phys = pmm_alloc();  // e.g., 0x202000
```

These are raw physical pages. We'll map them to virtual addresses next.

### Step 4: Map User Code

```c
vmm_map_page(user_pml4,
             0x400000,           // Virtual address
             code_phys,          // Physical address
             PTE_PRESENT | PTE_USER);
```

Let's trace through `vmm_map_page()`:

```
Virtual address 0x400000 breakdown:
- Binary: 0000...0100 0000 0000 0000 0000 0000
- PML4 index: bits 47-39 = 0
- PDPT index: bits 38-30 = 0
- PD index:   bits 29-21 = 2
- PT index:   bits 20-12 = 0
```

The function walks the hierarchy:

```
Step A: Check PML4[0]
        └─→ It's 0 (not present)
        └─→ Allocate new PDPT (pmm_alloc → 0x203000)
        └─→ PML4[0] = 0x203000 | PRESENT | WRITABLE | USER

Step B: Check PDPT[0]
        └─→ It's 0 (not present)
        └─→ Allocate new PD (pmm_alloc → 0x204000)
        └─→ PDPT[0] = 0x204000 | PRESENT | WRITABLE | USER

Step C: Check PD[2]
        └─→ It's 0 (not present)
        └─→ Allocate new PT (pmm_alloc → 0x205000)
        └─→ PD[2] = 0x205000 | PRESENT | WRITABLE | USER

Step D: Set PT[0]
        └─→ PT[0] = code_phys | PRESENT | USER
        └─→ PT[0] = 0x201000 | 0x5 = 0x201005
```

After this, the page table hierarchy looks like:

```
user_pml4 (0x200000)
    │
    ├─[0]─→ PDPT (0x203000)
    │           │
    │           └─[0]─→ PD (0x204000)
    │                       │
    │                       └─[2]─→ PT (0x205000)
    │                                   │
    │                                   └─[0]─→ 0x201005 (code page, user, present)
    │
    ├─[1-255]─→ 0 (not present)
    │
    └─[256-511]─→ (kernel mappings)
```

### Step 5: Map User Stack

```c
vmm_map_page(user_pml4,
             0x7FFFFF000,        // Virtual address
             stack_phys,         // Physical address
             PTE_PRESENT | PTE_WRITABLE | PTE_USER);
```

Similar process, but at different indices:

```
Virtual address 0x7FFFFF000:
- PML4 index: 0
- PDPT index: 1
- PD index:   511
- PT index:   511

Since PML4[0] already exists (we created it above),
we reuse that PDPT. But PDPT[1] is new, so we create:
- New PD at PDPT[1]
- New PT at PD[511]
- Set PT[511] = stack_phys | PRESENT | WRITABLE | USER
```

### Step 6: Copy User Program to Memory

```c
// Access the physical code page via HHDM
void *code_virt = phys_to_virt(code_phys);

// Copy our hardcoded user program binary
memcpy(code_virt, user_program_binary, user_program_size);
```

### Step 7: Switch to User Address Space

```c
vmm_switch_address_space(user_pml4);
```

This is a single instruction:

```c
void vmm_switch_address_space(uint64_t pml4_phys) {
    asm volatile ("movq %0, %%cr3" : : "r"(pml4_phys) : "memory");
}
```

What happens:
1. CR3 is loaded with `user_pml4` physical address
2. TLB is flushed (except global pages)
3. All subsequent memory accesses use the new page tables

Now:
- `0x400000` translates to our code page
- `0x7FFFFF000` translates to our stack page
- Kernel addresses still work (we copied those entries)
- Everything else faults (not mapped)

### Step 8: Enter Userspace

```c
struct user_context ctx = {
    .pml4 = user_pml4,
    .entry = 0x400000,
    .stack = 0x7FFFFF000 + 0x1000,  // Top of stack page
};
context_switch_to_user(&ctx);
```

The assembly code builds an `iretq` frame and jumps to ring 3. Now the user program executes at virtual address `0x400000`, using the stack at `0x7FFFFF000`.

---

## Code Deep Dive

### vmm.h - Header File

```c
// File: vmm.h

/*
 * Page Table Entry flags - these bits control page permissions
 */
#define PTE_PRESENT     (1ULL << 0)   // Page is valid
#define PTE_WRITABLE    (1ULL << 1)   // Allow writes (else read-only)
#define PTE_USER        (1ULL << 2)   // Allow ring 3 access
#define PTE_WRITETHROUGH (1ULL << 3)  // Write-through caching
#define PTE_NOCACHE     (1ULL << 4)   // Disable caching
#define PTE_ACCESSED    (1ULL << 5)   // CPU sets when page is read
#define PTE_DIRTY       (1ULL << 6)   // CPU sets when page is written
#define PTE_HUGE        (1ULL << 7)   // 2MB page (in PD) or 1GB (in PDPT)
#define PTE_GLOBAL      (1ULL << 8)   // Don't flush from TLB on CR3 switch
#define PTE_NX          (1ULL << 63)  // No-execute (requires EFER.NXE)

/*
 * Extract the physical address from a page table entry
 * Bits 12-51 contain the page frame number
 */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL
```

**Understanding PTE_ADDR_MASK:**

```
Page Table Entry (64 bits):
┌───┬─────────────────────────────────────────────────┬───────────┐
│NX │ Available │     Physical Address (40 bits)      │   Flags   │
│63 │  62-52    │           51-12                     │   11-0    │
└───┴─────────────────────────────────────────────────┴───────────┘

PTE_ADDR_MASK = 0x000FFFFFFFFFF000
             = 0000 0000 0000 [1111...1111] 0000 0000 0000
                              ↑ bits 51-12 ↑

To extract physical address: entry & PTE_ADDR_MASK
To extract flags: entry & ~PTE_ADDR_MASK (or just entry & 0xFFF for low flags)
```

### Index Extraction Macros

```c
#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)  // Bits 47-39
#define PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)  // Bits 38-30
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)  // Bits 29-21
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)  // Bits 20-12
```

**Example with 0x7FFFFF000:**

```
0x7FFFFF000 in binary (48 significant bits):
0000 0000 0000 0000 0111 1111 1111 1111 1111 1111 0000 0000 0000

Split into fields:
PML4 (47-39): 0 0000 0000 = 0
PDPT (38-30): 0 0000 0001 = 1
PD   (29-21): 1 1111 1111 = 511
PT   (20-12): 1 1111 1111 = 511
Offset(11-0): 0000 0000 0000 = 0

Let's verify with the macros:
PML4_INDEX(0x7FFFFF000) = (0x7FFFFF000 >> 39) & 0x1FF
                        = 0 & 0x1FF
                        = 0 ✓

PDPT_INDEX(0x7FFFFF000) = (0x7FFFFF000 >> 30) & 0x1FF
                        = 1 & 0x1FF
                        = 1 ✓

PD_INDEX(0x7FFFFF000)   = (0x7FFFFF000 >> 21) & 0x1FF
                        = 0x3FF & 0x1FF
                        = 511 ✓

PT_INDEX(0x7FFFFF000)   = (0x7FFFFF000 >> 12) & 0x1FF
                        = 0x7FFFF & 0x1FF
                        = 511 ✓
```

### vmm.c - Core Implementation

#### Allocating Page Tables

```c
static uint64_t alloc_page_table(void) {
    // Get a physical page from the PMM
    uint64_t phys = pmm_alloc();
    if (phys == 0) {
        return 0;  // Out of memory
    }

    // Zero all 512 entries via HHDM
    // phys_to_virt converts physical address to virtual address
    // so we can write to it
    uint64_t *table = phys_to_virt(phys);
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }

    return phys;  // Return PHYSICAL address (that's what page tables store)
}
```

**Why zero the table?**

A non-zero entry might have `PTE_PRESENT` set by accident, causing the CPU to follow a garbage pointer. Always zero new page tables!

#### Creating an Address Space

```c
uint64_t vmm_create_address_space(void) {
    // Allocate new PML4 (zeroed)
    uint64_t new_pml4_phys = alloc_page_table();
    if (new_pml4_phys == 0) {
        return 0;
    }

    // Get virtual pointers to both PML4s
    uint64_t *new_pml4 = phys_to_virt(new_pml4_phys);
    uint64_t *kernel_pml4 = phys_to_virt(kernel_pml4_phys);

    // Copy kernel mappings (upper half)
    // Entries 0-255: user space (leave as zero)
    // Entries 256-511: kernel space (copy from kernel)
    for (int i = KERNEL_PML4_START; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4_phys;
}
```

**Critical insight:** We're copying PML4 *entries*, not the underlying tables. This means:

```
new_pml4[256] = kernel_pml4[256]  // Same pointer to same PDPT!

Both PML4s point to the SAME kernel PDPT/PD/PT structures.
This is intentional - kernel memory should be shared.
```

#### Mapping a Page

This is the heart of the VMM:

```c
int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = phys_to_virt(pml4_phys);

    // Extract indices from virtual address
    int pml4_idx = PML4_INDEX(virt);
    int pdpt_idx = PDPT_INDEX(virt);
    int pd_idx   = PD_INDEX(virt);
    int pt_idx   = PT_INDEX(virt);

    // ═══════════════════════════════════════════════════════
    // Level 1: PML4 → PDPT
    // ═══════════════════════════════════════════════════════
    uint64_t *pdpt;
    if (pml4[pml4_idx] & PTE_PRESENT) {
        // PDPT already exists - extract its address
        pdpt = phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);
    } else {
        // Need to create a new PDPT
        uint64_t pdpt_phys = alloc_page_table();
        if (pdpt_phys == 0) return -1;

        // Store in PML4 with permissive flags
        // (final permissions are in the PT entry)
        pml4[pml4_idx] = pdpt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pdpt = phys_to_virt(pdpt_phys);
    }

    // ═══════════════════════════════════════════════════════
    // Level 2: PDPT → PD
    // ═══════════════════════════════════════════════════════
    uint64_t *pd;
    if (pdpt[pdpt_idx] & PTE_PRESENT) {
        pd = phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pd_phys = alloc_page_table();
        if (pd_phys == 0) return -1;
        pdpt[pdpt_idx] = pd_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pd = phys_to_virt(pd_phys);
    }

    // ═══════════════════════════════════════════════════════
    // Level 3: PD → PT
    // ═══════════════════════════════════════════════════════
    uint64_t *pt;
    if (pd[pd_idx] & PTE_PRESENT) {
        pt = phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);
    } else {
        uint64_t pt_phys = alloc_page_table();
        if (pt_phys == 0) return -1;
        pd[pd_idx] = pt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pt = phys_to_virt(pt_phys);
    }

    // ═══════════════════════════════════════════════════════
    // Level 4: PT entry (the actual mapping!)
    // ═══════════════════════════════════════════════════════
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags;

    return 0;
}
```

**Visual representation of what this function builds:**

```
Before vmm_map_page(pml4, 0x400000, 0x201000, PTE_PRESENT|PTE_USER):

pml4[0] = 0 (empty)

After:

pml4[0] ──────────────────────────────────────┐
   = 0x203007                                 │
   = 0x203000 | PRESENT | WRITABLE | USER     │
                                              ▼
                                    ┌─────────────────┐
                                    │ PDPT @ 0x203000 │
                                    ├─────────────────┤
                                    │ [0] = 0x204007  │──┐
                                    │ [1-511] = 0     │  │
                                    └─────────────────┘  │
                                                         ▼
                                               ┌─────────────────┐
                                               │  PD @ 0x204000  │
                                               ├─────────────────┤
                                               │ [0-1] = 0       │
                                               │ [2] = 0x205007  │──┐
                                               │ [3-511] = 0     │  │
                                               └─────────────────┘  │
                                                                    ▼
                                                          ┌─────────────────┐
                                                          │  PT @ 0x205000  │
                                                          ├─────────────────┤
                                                          │ [0] = 0x201005  │
                                                          │   ↓             │
                                                          │  0x201000       │
                                                          │  | PRESENT      │
                                                          │  | USER         │
                                                          │ [1-511] = 0     │
                                                          └─────────────────┘
```

#### Switching Address Spaces

```c
void vmm_switch_address_space(uint64_t pml4_phys) {
    asm volatile ("movq %0, %%cr3" : : "r"(pml4_phys) : "memory");
}
```

**What the CPU does when CR3 is written:**

1. Stores new PML4 physical address in CR3
2. Flushes TLB (except entries with PTE_GLOBAL)
3. All future memory accesses use new page tables

**The "memory" clobber** tells the compiler that this instruction has memory side effects. The compiler won't reorder memory accesses across this instruction.

### pmm.c - Physical Memory Allocator

The PMM provides physical pages for the VMM to use.

#### Bitmap Operations

```c
// Mark page at index as used (set bit to 1)
static inline void bitmap_mark_used(uint64_t page_index) {
    bitmap[page_index / 8] |= (1 << (page_index % 8));
}

// Mark page at index as free (clear bit to 0)
static inline void bitmap_mark_free(uint64_t page_index) {
    bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

// Check if page is used
static inline int bitmap_is_used(uint64_t page_index) {
    return bitmap[page_index / 8] & (1 << (page_index % 8));
}
```

**Example: Managing page 13**

```
page_index = 13
byte_index = 13 / 8 = 1
bit_index  = 13 % 8 = 5

bitmap[1] before: 0b00000000
                        ↑ bit 5

bitmap_mark_used(13):
  bitmap[1] |= (1 << 5)
  bitmap[1] |= 0b00100000
  bitmap[1] = 0b00100000

bitmap_is_used(13):
  bitmap[1] & (1 << 5)
  0b00100000 & 0b00100000
  = 0b00100000 (non-zero = true)
```

#### Allocation

```c
uint64_t pmm_alloc(void) {
    // Linear scan for a free page
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_is_used(i)) {
            bitmap_mark_used(i);
            free_pages--;
            return i * PAGE_SIZE;  // Convert index to physical address
        }
    }
    return 0;  // Out of memory
}
```

**Performance note:** This O(n) scan is simple but slow for large memory. Production allocators use:
- **Free lists**: O(1) allocation from head of list
- **Buddy allocator**: O(log n) with efficient coalescing
- **Slab allocator**: O(1) for fixed-size objects

### memory.h - Address Conversion

```c
extern uint64_t g_hhdm_offset;  // Set by pmm_init()

static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + g_hhdm_offset);
}

static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt - g_hhdm_offset;
}
```

**Why is HHDM necessary?**

Page tables store *physical* addresses. But once paging is enabled, the CPU uses *virtual* addresses. How do we read/write page tables?

The HHDM maps all physical memory at a known virtual offset:
- Physical `0x123000` is accessible at virtual `0xFFFF800000123000`
- `g_hhdm_offset` = `0xFFFF800000000000` (provided by bootloader)

```
Physical:  0x00000000  0x00001000  0x00002000  ...
                │           │           │
                │    +HHDM  │    +HHDM  │    +HHDM
                ▼           ▼           ▼
Virtual:   0xFFFF8000  0xFFFF8000  0xFFFF8000  ...
           00000000    00001000    00002000
```

---

## Summary

Virtual memory management is one of the most important subsystems in any operating system. It enables:

- **Process isolation** through separate address spaces
- **Memory protection** through permission flags
- **Efficient memory use** through demand paging and sharing

MyOS implements the essential VMM functionality:

| Component | What it does |
|-----------|--------------|
| `vmm_init()` | Captures kernel's PML4 for later copying |
| `vmm_create_address_space()` | Creates new PML4 with kernel mappings |
| `vmm_map_page()` | Walks/creates 4-level hierarchy, sets final entry |
| `vmm_switch_address_space()` | Writes new PML4 address to CR3 |
| `pmm_alloc()` / `pmm_free()` | Provides physical pages for page tables |
| `phys_to_virt()` | Converts physical to virtual via HHDM |

Future enhancements could include:
- **Demand paging**: Allocate pages on first access
- **VMA tracking**: Track memory regions for mmap/munmap
- **Page unmapping**: Free pages and reclaim memory
- **Huge pages**: Reduce TLB pressure with 2MB pages
- **Copy-on-write**: Efficient fork() implementation

---

## Further Reading

- **Intel SDM Vol. 3A, Chapter 4** - Paging (the authoritative reference)
- **OSDev Wiki: Paging** - https://wiki.osdev.org/Paging
- **Linux kernel source**: `mm/memory.c`, `arch/x86/mm/`
- **"Understanding the Linux Virtual Memory Manager"** by Mel Gorman
- **Windows Internals** by Russinovich et al. (Chapter 5: Memory Management)
