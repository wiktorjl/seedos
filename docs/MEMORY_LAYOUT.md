# Memory Layout - Seed OS x86-64

This document explains how memory is organized in Seed OS, from physical RAM to virtual address spaces, and how everything comes together during boot.

## Table of Contents

1. [The Big Picture](#the-big-picture)
2. [Physical Memory](#physical-memory)
3. [Virtual Address Space Layout](#virtual-address-space-layout)
4. [x86-64 Paging Explained](#x86-64-paging-explained)
5. [The HHDM: Direct Physical Access](#the-hhdm-direct-physical-access)
6. [Boot Sequence: Memory Setup](#boot-sequence-memory-setup)
7. [Kernel vs User Memory](#kernel-vs-user-memory)
8. [Concrete Example: Running a User Program](#concrete-example-running-a-user-program)

---

## The Big Picture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        VIRTUAL ADDRESS SPACE                            │
│                        (What the CPU sees)                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  0xFFFFFFFFFFFFFFFF ─┐                                                  │
│                      │  KERNEL SPACE (shared by all processes)          │
│  0xFFFFFFFF80000000 ─┤  ← Kernel code & data live here                  │
│                      │                                                  │
│  0xFFFF800000000000 ─┤  ← HHDM: All physical RAM mapped here            │
│                      │                                                  │
│        ~~~~~~~~~~~~~ │  (non-canonical hole - unusable)                 │
│                      │                                                  │
│  0x00007FFFFFFFFFFF ─┤                                                  │
│                      │  USER SPACE (per-process, isolated)              │
│  0x0000007FFFFF000  ─┤  ← User stack (grows down)                       │
│                      │                                                  │
│  0x0000000000400000 ─┤  ← User code                                     │
│                      │                                                  │
│  0x0000000000000000 ─┘                                                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**Key insight:** Every process has the same kernel mapped in the upper half. This is why system calls work - when you trap into the kernel, the kernel code is already mapped and accessible.

---

## Physical Memory

Physical memory is the actual RAM chips in your computer. It's a flat array of bytes starting at address 0.

### What's in Physical Memory?

```
Physical Address        Contents
───────────────────────────────────────────────────────
0x00000000             Reserved (real mode IVT, BDA)
0x00001000             Usable RAM begins
    ...                (varies by system)
0x00100000 (1MB)       Kernel loaded here (physical)
    ...
0x????????             PMM bitmap
    ...
0x????????             Page tables
    ...
0xFFF00000+            Reserved (BIOS, MMIO, etc.)
```

### Physical Memory Manager (PMM)

The PMM tracks which 4KB pages are free or in use using a bitmap:

```c
// Each bit = one 4KB page
// 0 = free, 1 = used

bitmap[0] = 0b11110001;  // Pages 0,4,5,6,7 used; pages 1,2,3 free
bitmap[1] = 0b00000000;  // Pages 8-15 all free
...
```

**API:**
- `pmm_alloc()` - Find a free page, mark it used, return its physical address
- `pmm_free(addr)` - Mark a page as free again

---

## Virtual Address Space Layout

Virtual addresses are what your code uses. The CPU's MMU translates them to physical addresses using page tables.

### Full 64-bit Layout

```
Virtual Address              Purpose                    Size
─────────────────────────────────────────────────────────────────────
0xFFFFFFFFFFFFFFFF ┐
                   │ Kernel code, data, BSS         ~few MB
0xFFFFFFFF80000000 ┴ KERNEL_VIRT_BASE

0xFFFF800000000000    HHDM (direct physical map)     = physical RAM size
                      phys addr X → virt addr (X + HHDM_OFFSET)

──────────────────── NON-CANONICAL HOLE ────────────────────
                      (addresses where bits 48-63 don't match bit 47
                       cause a fault - this is enforced by hardware)

0x00007FFFFFFFFFFF ┐
                   │ User space (per-process)       128 TB max
0x0000000000000000 ┘
```

### User Space Details

```
0x0000007FFFFF000    USER_STACK_BASE    User stack top (grows DOWN)
        │                               │
        │                               ▼ Stack grows this way
        │
        │               (unmapped - will fault if accessed)
        │
0x0000000000400000   USER_CODE_BASE     User program loaded here
        │
        │               (unmapped - NULL pointer region)
        │
0x0000000000000000   NULL               Accessing this faults (good!)
```

**Why 0x400000?** It's a traditional Unix convention. Leaves room below for the NULL guard region.

---

## x86-64 Paging Explained

x86-64 uses **4-level page tables** to translate virtual addresses to physical addresses.

### Address Translation

A 64-bit virtual address is split into parts:

```
 63    48 47    39 38    30 29    21 20    12 11        0
┌────────┬────────┬────────┬────────┬────────┬──────────┐
│ Sign   │ PML4   │ PDPT   │  PD    │  PT    │  Offset  │
│ extend │ Index  │ Index  │ Index  │ Index  │ (12 bit) │
└────────┴────────┴────────┴────────┴────────┴──────────┘
   16b      9b       9b       9b       9b        12b

Each index = 9 bits = values 0-511
Offset = 12 bits = 0-4095 (position within 4KB page)
```

### The Four Levels

```
CR3 Register
    │
    ▼
┌─────────┐
│  PML4   │  Page Map Level 4 (512 entries, index from bits 47-39)
│ [0-511] │
└────┬────┘
     │ PML4[index] points to...
     ▼
┌─────────┐
│  PDPT   │  Page Directory Pointer Table (512 entries, index from bits 38-30)
│ [0-511] │
└────┬────┘
     │ PDPT[index] points to...
     ▼
┌─────────┐
│   PD    │  Page Directory (512 entries, index from bits 29-21)
│ [0-511] │
└────┬────┘
     │ PD[index] points to...
     ▼
┌─────────┐
│   PT    │  Page Table (512 entries, index from bits 20-12)
│ [0-511] │
└────┬────┘
     │ PT[index] contains...
     ▼
┌─────────────────────┐
│ Physical Page Addr  │ + Offset (bits 11-0) = Final Physical Address
└─────────────────────┘
```

### Page Table Entry Format

Each entry in any level is 64 bits:

```
 63  62       52 51          12 11   9 8 7 6 5 4 3 2 1 0
┌───┬───────────┬──────────────┬──────┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
│NX │  Reserved │Physical Addr │ Avl  │G│ │D│A│ │ │U│W│P│
└───┴───────────┴──────────────┴──────┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 │                     │                       │ │ │ │ └─ Present
 │                     │                       │ │ │ └─── Writable
 │                     │                       │ │ └───── User-accessible
 │                     │                       │ └─────── Accessed (CPU sets)
 │                     │                       └───────── Dirty (CPU sets on write)
 │                     └───────────────────────────────── Physical address (bits 12-51)
 └─────────────────────────────────────────────────────── No-Execute
```

**Key flags for this OS:**
- `PTE_PRESENT (bit 0)` - Page is valid and mapped
- `PTE_WRITABLE (bit 1)` - Page can be written to
- `PTE_USER (bit 2)` - Ring 3 code can access this page

---

## The HHDM: Direct Physical Access

**HHDM = Higher Half Direct Map**

The bootloader (Limine) creates a mapping of ALL physical RAM at a high virtual address. This lets the kernel easily access any physical address.

```
Physical RAM:                    HHDM Virtual Addresses:

0x00000000 ─┐                    0xFFFF800000000000 ─┐
            │                                        │
0x00001000 ─┤                    0xFFFF800000001000 ─┤
            │   ══════════►                          │
0x00100000 ─┤   Add HHDM         0xFFFF800000100000 ─┤
            │   offset                               │
   ...      │                       ...              │
            │                                        │
(end RAM)  ─┘                    (end HHDM)         ─┘
```

**The conversion macros:**

```c
// Physical → Virtual (for kernel to read/write physical memory)
#define phys_to_virt(phys)  ((void *)((phys) + g_hhdm_offset))

// Virtual → Physical (to put addresses in page tables)
#define virt_to_phys(virt)  ((uint64_t)(virt) - g_hhdm_offset)
```

**Why is HHDM essential?**

Page tables store *physical* addresses, but the CPU is executing with virtual addressing enabled. To modify a page table, the kernel needs to access it - but where?

Answer: Through the HHDM! If a page table is at physical address 0x123000, access it at virtual address `0xFFFF800000123000`.

---

## Boot Sequence: Memory Setup

Here's how memory gets organized from power-on to userspace:

### Stage 1: Bootloader (Limine)

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. BIOS/UEFI runs, finds Limine bootloader                       │
│ 2. Limine loads kernel ELF into memory                           │
│ 3. Limine sets up initial page tables:                           │
│    - Kernel at 0xFFFFFFFF80000000 (virtual)                      │
│    - HHDM at 0xFFFF800000000000                                  │
│ 4. Limine switches to long mode (64-bit)                         │
│ 5. Limine jumps to kernel entry point (_start in boot.S)         │
└──────────────────────────────────────────────────────────────────┘
```

### Stage 2: Early Kernel (boot.S)

```asm
_start:
    leaq stack_top(%rip), %rsp   ; Set up 16KB kernel stack
    call kernel_main             ; Enter C code
```

### Stage 3: Kernel Initialization (kernel.c)

```
┌──────────────────────────────────────────────────────────────────┐
│ kernel_main() executes:                                          │
│                                                                  │
│ 1. serial_init()     - Debug output via COM1                     │
│                                                                  │
│ 2. fb_init()         - Framebuffer graphics                      │
│                                                                  │
│ 3. Get bootloader info:                                          │
│    - hhdm_request.response->offset  → g_hhdm_offset              │
│    - memmap_request.response        → memory map                 │
│                                                                  │
│ 4. pmm_init(memmap, hhdm)                                        │
│    - Parse memory map from bootloader                            │
│    - Find usable regions                                         │
│    - Allocate bitmap in usable memory                            │
│    - Mark all usable pages as free                               │
│    - Mark reserved/kernel pages as used                          │
│                                                                  │
│ 5. gdt_init()                                                    │
│    - Set up segment descriptors for ring 0 and ring 3            │
│    - Configure TSS for kernel stack on interrupts                │
│                                                                  │
│ 6. vmm_init(hhdm)                                                │
│    - Save kernel's PML4 physical address (from CR3)              │
│    - This PML4 was set up by bootloader                          │
│                                                                  │
│ 7. idt_init(), pic_init(), keyboard_init()                       │
│    - Interrupt handling ready                                    │
│                                                                  │
│ 8. Create user process (see next section)                        │
│                                                                  │
│ 9. shell_init()      - Interactive shell                         │
└──────────────────────────────────────────────────────────────────┘
```

---

## Kernel vs User Memory

### The Split

The PML4 has 512 entries. We split them:

```
PML4 Index    Virtual Address Range              Owner
─────────────────────────────────────────────────────────────
0 - 255       0x0000000000000000 - 0x00007FFFFFFFFFFF   USER
256 - 511     0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF   KERNEL
```

**Entry 256** starts at `0xFFFF800000000000` because:
- Index 256 = binary `100000000`
- Bits 47-39 of address = 256
- Sign extension makes bits 63-48 all 1s
- Result: `0xFFFF800000000000`

### Sharing Kernel Mappings

Every process needs the kernel mapped so interrupts/syscalls work:

```c
uint64_t vmm_create_address_space(void) {
    // Allocate fresh PML4
    uint64_t new_pml4_phys = pmm_alloc();
    uint64_t *new_pml4 = phys_to_virt(new_pml4_phys);

    // Clear user half (entries 0-255)
    memset(new_pml4, 0, 256 * sizeof(uint64_t));

    // Copy kernel half (entries 256-511) from kernel's PML4
    uint64_t *kernel_pml4 = phys_to_virt(kernel_pml4_phys);
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4_phys;
}
```

Result: Each process has its own user mappings but shares kernel mappings.

---

## Concrete Example: Running a User Program

Let's trace what happens when we run "Hello World" in userspace:

### Step 1: Allocate Address Space

```c
// Create new PML4 with kernel mapped
uint64_t user_pml4 = vmm_create_address_space();
```

```
user_pml4 (new PML4):
┌─────────────────────────────────┐
│ Entry 0:   0 (empty)            │  ← User space
│ Entry 1:   0 (empty)            │
│   ...                           │
│ Entry 255: 0 (empty)            │
├─────────────────────────────────┤
│ Entry 256: [same as kernel]     │  ← Kernel space
│ Entry 257: [same as kernel]     │     (copied from kernel PML4)
│   ...                           │
│ Entry 511: [same as kernel]     │
└─────────────────────────────────┘
```

### Step 2: Allocate Physical Pages

```c
uint64_t code_phys = pmm_alloc();   // e.g., returns 0x200000
uint64_t stack_phys = pmm_alloc();  // e.g., returns 0x201000
```

### Step 3: Map User Code

```c
vmm_map_page(user_pml4,
             0x400000,              // Virtual address (USER_CODE_BASE)
             code_phys,             // Physical address
             PTE_PRESENT | PTE_USER);
```

This creates page table entries:

```
Virtual 0x400000 breakdown:
- PML4 index:  0      (bits 47-39)
- PDPT index:  0      (bits 38-30)
- PD index:    2      (bits 29-21)
- PT index:    0      (bits 20-12)
- Offset:      0      (bits 11-0)

Result:
PML4[0] → new PDPT
PDPT[0] → new PD
PD[2]   → new PT
PT[0]   → code_phys | PTE_PRESENT | PTE_USER
```

### Step 4: Map User Stack

```c
vmm_map_page(user_pml4,
             0x7FFFFF000,           // Virtual address (USER_STACK_BASE)
             stack_phys,            // Physical address
             PTE_PRESENT | PTE_WRITABLE | PTE_USER);
```

### Step 5: Copy User Program

```c
// Copy hardcoded binary to the code page
void *code_virt = phys_to_virt(code_phys);
memcpy(code_virt, user_program_binary, user_program_size);
```

### Step 6: Enter Userspace

```c
struct user_context ctx = {
    .pml4 = user_pml4,
    .entry = 0x400000,                    // Start executing here
    .stack = 0x7FFFFF000 + 0x1000,        // Stack pointer (top of page)
};
context_switch_to_user(&ctx);
```

**What context_switch_to_user does:**

```
1. Save kernel state (registers, stack pointer)
2. Set TSS.RSP0 = current kernel stack (for ring 3→0 transitions)
3. Load CR3 = user_pml4 (switch address space!)
4. Build iretq frame:
   ┌─────────────────┐
   │ SS:   0x23      │  (user data segment, RPL=3)
   │ RSP:  0x800000  │  (user stack pointer)
   │ RFLAGS: 0x202   │  (interrupts enabled)
   │ CS:   0x1b      │  (user code segment, RPL=3)
   │ RIP:  0x400000  │  (user entry point)
   └─────────────────┘
5. Clear all GPRs
6. Execute: iretq    → CPU pops frame, jumps to ring 3
```

### Step 7: User Program Executes

```
Now running at ring 3, virtual address 0x400000:

User's view of memory:
┌────────────────────────────────────┐
│ 0x7FFFFF000-0x800000000: Stack ✓   │ (mapped, writable)
│ 0x400000-0x401000: Code ✓          │ (mapped, executable)
│ Everything else: UNMAPPED          │ (fault if accessed)
└────────────────────────────────────┘

User code runs:
    mov rax, 1          ; sys_write
    mov rdi, 1          ; fd = stdout
    mov rsi, message    ; buffer
    mov rdx, 13         ; length
    int 0x80            ; SYSCALL!
```

### Step 8: System Call (int 0x80)

```
CPU detects int 0x80 from ring 3:

1. Look up IDT[0x80]
2. Load kernel CS (0x08) and SS (0x10)
3. Load RSP from TSS.RSP0 (kernel stack)
4. Push user's SS, RSP, RFLAGS, CS, RIP onto kernel stack
5. Jump to syscall handler (ring 0)

Kernel handles sys_write:
- Validates buffer address (is it in user space?)
- Copies string to console
- Returns to user via iret
```

### Step 9: Exit (sys_exit)

```c
// User code calls sys_exit
mov rax, 0       ; sys_exit
mov rdi, 0       ; exit code
int 0x80

// Kernel's sys_exit handler:
void sys_exit(int code) {
    // Switch back to kernel address space
    vmm_switch_address_space(kernel_pml4_phys);
    // Return to where context_switch_to_user was called
    context_return_to_kernel();
}
```

---

## Quick Reference

### Key Addresses

| Name | Address | Purpose |
|------|---------|---------|
| `KERNEL_VIRT_BASE` | `0xFFFFFFFF80000000` | Where kernel code/data lives |
| `HHDM_OFFSET` | `0xFFFF800000000000` | Physical memory direct map |
| `USER_CODE_BASE` | `0x0000000000400000` | User program entry point |
| `USER_STACK_BASE` | `0x0000007FFFFF000` | Top of user stack region |

### Key GDT Selectors

| Selector | Value | Ring | Type |
|----------|-------|------|------|
| `GDT_KERNEL_CODE` | `0x08` | 0 | Code |
| `GDT_KERNEL_DATA` | `0x10` | 0 | Data |
| `GDT_USER_CODE` | `0x18` | 3 | Code |
| `GDT_USER_DATA` | `0x20` | 3 | Data |

### Conversion Macros

```c
phys_to_virt(phys)  // Physical → Virtual (via HHDM)
virt_to_phys(virt)  // Virtual → Physical (via HHDM)

PML4_INDEX(va)      // Extract PML4 index (bits 47-39)
PDPT_INDEX(va)      // Extract PDPT index (bits 38-30)
PD_INDEX(va)        // Extract PD index (bits 29-21)
PT_INDEX(va)        // Extract PT index (bits 20-12)
```

---

## Further Reading

- **Intel SDM Vol. 3A, Chapter 4** - Paging in detail
- **OSDev Wiki: Paging** - https://wiki.osdev.org/Paging
- **OSDev Wiki: Higher Half Kernel** - https://wiki.osdev.org/Higher_Half_Kernel
- **Limine Boot Protocol** - https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md
