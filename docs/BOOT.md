# How Seed OS Boots: The Journey from Power-On to Kernel

This document explains the complete boot process of Seed OS, from the moment power is applied to the machine until our kernel code begins executing. Understanding this process is fundamental to OS development.

## Table of Contents

1. [The Boot Problem](#the-boot-problem)
2. [Why We Skip the Bootloader (Philosophy)](#why-we-skip-the-bootloader-philosophy)
3. [What is Limine?](#what-is-limine)
4. [The Boot Sequence](#the-boot-sequence)
5. [What Limine Does For Us](#what-limine-does-for-us)
6. [The Limine Protocol](#the-limine-protocol)
7. [Our Kernel's Entry Point](#our-kernels-entry-point)
8. [What We Don't Have to Do](#what-we-dont-have-to-do)
9. [What We Still Need to Do](#what-we-still-need-to-do)

---

## The Boot Problem

When a computer powers on, it faces a bootstrapping problem:

1. The CPU starts executing in **16-bit real mode** (for x86 compatibility)
2. Only the first 512 bytes of the boot disk are loaded into memory
3. There's no filesystem driver, no memory management, no nothing
4. The kernel is a large file sitting on a disk the CPU can't read yet

Someone has to bridge this gap between "CPU just powered on" and "kernel is running in 64-bit mode with memory set up." That's the bootloader's job.

### The Historical Approach

In the old days, OS developers would write their own bootloader:

1. Write a 512-byte **boot sector** that fits in the Master Boot Record (MBR)
2. That code loads a **second-stage bootloader** from disk
3. The second stage switches to **protected mode** (32-bit)
4. Load the kernel into memory
5. Switch to **long mode** (64-bit)
6. Set up page tables and jump to the kernel

This is hundreds of lines of tricky assembly code dealing with:
- BIOS disk interrupts (INT 13h)
- A20 line enabling (legacy 8086 memory wrapping)
- GDT setup for protected mode
- Page table construction for long mode
- Parsing filesystem structures to find the kernel file

Writing a bootloader is educational but time-consuming. Modern OS developers often use an existing bootloader.

---

## Why We Skip the Bootloader (Philosophy)

Many hobby OS projects never get past the bootloader phase. Developers spend months wrestling with:

- 16-bit real mode assembly
- BIOS interrupts and their quirks
- The A20 gate (a hack from 1982 that persists today)
- Protected mode setup
- Long mode transition
- Legacy disk drivers

These are **solved problems**. They're historically interesting, but they teach you very little about operating system design. You're not learning about scheduling, virtual memory, or syscalls - you're debugging ancient x86 initialization sequences.

### The Graveyard of Hobby OSes

The OSDev community has seen countless projects that:

1. Start with enthusiasm
2. Spend 3-6 months on bootloader and mode switching
3. Finally get to "Hello World" in protected mode
4. Burn out before implementing anything interesting
5. Abandon the project

This is the **bootloader trap**. It's a rite of passage that filters out most hobbyists before they reach the genuinely educational parts of OS development.

### Our Approach: Focus on What Matters

This project takes a different philosophy:

| Traditional Approach | Our Approach |
|---------------------|--------------|
| Write 16-bit bootloader | Use Limine |
| Implement real→protected→long mode | Start in 64-bit long mode |
| Parse FAT/ext2 to load kernel | Let bootloader handle it |
| Query BIOS for memory map | Receive it from Limine |
| Spend months on boot code | Spend time on actual OS |

**We deliberately skip:**
- 16-bit real mode programming
- 32-bit protected mode (except as a concept)
- BIOS interrupt handlers
- Legacy disk I/O
- VGA text mode (we use framebuffer graphics)

**We focus on:**
- Modern 64-bit long mode from the start
- Virtual memory and paging (4-level page tables)
- User/kernel separation (Ring 0 vs Ring 3)
- System calls and the syscall interface
- Process management and scheduling
- Device drivers and interrupt handling

### Why 64-bit? Why UEFI-era Tools?

**64-bit (long mode) is the present and future.** No new OS targets 32-bit anymore. The concepts are the same, but:
- Larger address space enables more interesting memory layouts
- Modern calling conventions are cleaner
- You're learning skills applicable to real systems

**UEFI has replaced BIOS.** Legacy BIOS boot is maintained for compatibility but:
- All modern machines use UEFI
- UEFI provides better pre-boot services
- Tools like Limine abstract this anyway

### The Trade-off

Yes, we miss learning about:
- Real mode segmentation (segment:offset addressing)
- The GDT/LDT dance in protected mode
- PIC vs APIC initialization from scratch
- BIOS data area and interrupt vector table

But we gain time to learn about:
- How `fork()` and `exec()` actually work
- Virtual memory tricks (copy-on-write, demand paging)
- File systems and the VFS layer
- Network stacks and protocols
- IPC mechanisms

**The goal is to build a working OS that does interesting things, not to become an expert in 1980s PC architecture.**

### When *Should* You Write a Bootloader?

Writing your own bootloader makes sense if:
- You're specifically interested in boot processes
- You're targeting non-x86 hardware
- You need custom boot behavior Limine doesn't support
- You want the complete "bare metal to userspace" experience

For this project, the bootloader is a means to an end. The end is a functioning operating system.

---

## What is Limine?

[Limine](https://github.com/limine-bootloader/limine) is a modern, feature-rich bootloader designed for hobby OS development. It handles all the complex early boot work and provides a clean interface to pass information to your kernel.

### Why Limine?

1. **Supports both BIOS and UEFI** - One bootloader for legacy and modern systems
2. **Boots directly to 64-bit long mode** - No need to do mode switching ourselves
3. **Higher-half kernel support** - Can load kernel at high virtual addresses
4. **Provides essential information** - Memory map, framebuffer, ACPI tables
5. **Well-documented protocol** - Clear specification for kernel/bootloader interface
6. **Active development** - Regular updates and bug fixes

---

## The Boot Sequence

Here's what happens when you boot Seed OS:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         FIRMWARE (BIOS/UEFI)                        │
│  1. Power-on self-test (POST)                                       │
│  2. Initialize basic hardware                                       │
│  3. Find bootable media (our ISO)                                   │
│  4. Load and execute bootloader                                     │
└──────────────────────────────────┬──────────────────────────────────┘
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         LIMINE BOOTLOADER                           │
│  5. Parse limine.conf to find kernel                                │
│  6. Load kernel.elf into memory                                     │
│  7. Switch CPU to 64-bit long mode                                  │
│  8. Set up initial page tables (identity + higher-half + HHDM)      │
│  9. Scan kernel for request structures                              │
│ 10. Fill in responses (memory map, framebuffer, HHDM offset)        │
│ 11. Jump to kernel entry point (_start)                             │
└──────────────────────────────────┬──────────────────────────────────┘
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                          OUR KERNEL (boot.S)                        │
│ 12. Set up kernel stack                                             │
│ 13. Call kernel_main()                                              │
└──────────────────────────────────┬──────────────────────────────────┘
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         OUR KERNEL (kernel.c)                       │
│ 14. Initialize serial port (for debugging)                          │
│ 15. Initialize framebuffer + console                                │
│ 16. Read Limine responses (memory map, HHDM offset)                 │
│ 17. Initialize PMM, GDT, VMM, IDT, PIC, keyboard                    │
│ 18. Run user program, then start shell                              │
└─────────────────────────────────────────────────────────────────────┘
```

---

## What Limine Does For Us

Limine performs an enormous amount of work before our kernel runs. Here's what we get "for free":

### 1. Mode Switching (Real → Protected → Long)

The CPU boots in 16-bit real mode. Getting to 64-bit long mode requires:

```
Real Mode (16-bit)
    │
    ├── Enable A20 line (access memory above 1MB)
    ├── Load a GDT with 32-bit segments
    ├── Set CR0.PE bit (enable protected mode)
    │
    ▼
Protected Mode (32-bit)
    │
    ├── Set up identity-mapped page tables
    ├── Enable PAE (Physical Address Extension)
    ├── Enable long mode in EFER MSR
    ├── Enable paging (CR0.PG)
    │
    ▼
Long Mode (64-bit)
    │
    └── Load a GDT with 64-bit segments
```

**We don't write any of this.** Limine does it all.

### 2. Page Tables and Memory Mapping

Limine sets up a sophisticated paging structure:

```
Virtual Address Space (as seen by kernel)
═══════════════════════════════════════════════════════════════════

0xFFFFFFFFFFFFFFFF ┐
                   │  Kernel is mapped here
0xFFFFFFFF80000000 ┤  ← Our kernel code runs at these addresses
                   │     (specified in linker.ld)
                   │
0xFFFF800000000000 ┤  ← HHDM (Higher Half Direct Map)
                   │     All physical RAM is accessible here
                   │     virtual = physical + 0xFFFF800000000000
                   │
    (non-canonical hole - addresses here cause #GP)
                   │
0x0000800000000000 ┤
                   │  Lower half (for userspace, unused at boot)
0x0000000000000000 ┘
```

**Key insight:** The HHDM lets us access any physical address by adding an offset. This is how we manipulate page tables and access hardware-mapped memory from the kernel.

### 3. ELF Loading

Our kernel is compiled as an ELF executable. Limine:

1. Reads the ELF headers
2. Loads each program segment to its specified virtual address
3. Handles the higher-half addressing (kernel linked at `0xFFFFFFFF80000000`)
4. Sets up page tables so these virtual addresses work

### 4. Framebuffer Setup

Limine configures a linear framebuffer for graphics output:

- Negotiates video mode with firmware
- Maps framebuffer memory into our address space
- Tells us the resolution, pixel format, and memory location

### 5. Memory Map

Limine queries the firmware for the physical memory layout:

- Which regions are usable RAM
- Which are reserved (BIOS, hardware-mapped)
- Which contain ACPI tables
- Where our kernel was loaded
- Where the framebuffer lives

This map is **essential** for our physical memory manager (PMM).

---

## The Limine Protocol

Limine uses a **request/response protocol** to communicate with the kernel. This is how it works:

### Magic Numbers and Scanning

When Limine loads our kernel ELF, it scans the binary looking for specific byte patterns (magic numbers). These patterns identify **request structures** that we've placed in a special section.

```c
// In kernel.c - we declare what we want from Limine
LIMINE_BASE_REVISION_DECLARATION;   // Protocol version
LIMINE_HHDM_REQUEST;                 // We want the HHDM offset
LIMINE_MEMMAP_REQUEST;               // We want the memory map
LIMINE_FRAMEBUFFER_REQUEST;          // We want a framebuffer
```

These macros expand to structures like:

```c
// Example: memory map request structure
struct limine_memmap_request {
    uint64_t id[4];                          // Magic bytes Limine searches for
    uint64_t revision;                       // Protocol version we support
    struct limine_memmap_response *response; // Initially NULL, Limine fills this
};
```

### The Section Attribute

```c
__attribute__((used, section(".limine_requests")))
```

- `used`: Prevents compiler from optimizing away the structure (it looks unused from C's perspective)
- `section(".limine_requests")`: Places structure in a known section (not in .bss where it would be zeroed)

Our `linker.ld` ensures this section is included:

```
.limine_requests : {
    *(.limine_requests)
}
```

### How It Works

1. **Before boot**: Request structures have `response = NULL`
2. **Limine runs**: Scans kernel, finds requests by magic ID bytes
3. **Limine processes**: For each request, allocates a response structure and fills it
4. **Limine updates**: Sets the `response` pointer to point to the response
5. **Kernel runs**: Checks `response != NULL`, then reads the data

```c
// In kernel_main()
if (memmap_request.response == NULL) {
    // Limine didn't give us a memory map - fatal error!
    puts("ERROR: No memory map from bootloader!\n");
}

// Safe to use the memory map
struct limine_memmap_response *memmap = memmap_request.response;
for (uint64_t i = 0; i < memmap->entry_count; i++) {
    // Process each memory region...
}
```

---

## Our Kernel's Entry Point

After Limine finishes, it jumps to `_start` (specified by the ELF entry point). Here's our minimal entry code:

```asm
# boot.S - Kernel entry point

.section .bss
.align 16
stack_bottom:
    .space 16384          # 16 KB kernel stack
stack_top:

.section .text
.global _start
_start:
    leaq stack_top(%rip), %rsp    # Set up stack pointer
    call kernel_main               # Jump to C code

.hang:
    cli                            # Disable interrupts
    hlt                            # Halt CPU
    jmp .hang                      # Loop forever if we wake up
```

This is remarkably simple because Limine has done the hard work:

1. **We're already in 64-bit mode** - No mode switching needed
2. **Paging is already enabled** - Higher-half addresses work
3. **We just need a stack** - Limine doesn't set one up for us
4. **Then jump to C** - The rest is in `kernel_main()`

### Why RIP-Relative Addressing?

```asm
leaq stack_top(%rip), %rsp
```

The `(%rip)` makes this position-independent. Since we're running at a high virtual address (`0xFFFFFFFF80000000+`), we need RIP-relative addressing to correctly reference symbols in our binary.

---

## What We Don't Have to Do

Thanks to Limine, we skip these complex tasks:

| Task | Difficulty | We Do It? |
|------|------------|-----------|
| Boot sector (MBR/VBR) | Hard | No |
| BIOS disk I/O (INT 13h) | Medium | No |
| A20 line enable | Easy but tedious | No |
| Real → Protected mode switch | Medium | No |
| Protected → Long mode switch | Hard | No |
| Initial page table setup | Hard | No |
| ELF parsing and loading | Medium | No |
| Video mode setup | Medium | No |
| E820 memory map query | Medium | No |
| UEFI boot services | Very Hard | No |

**Total saved: ~1000+ lines of assembly and C code, plus weeks of debugging.**

---

## What We Still Need to Do

Limine provides a foundation, but we build everything else:

### Immediately After Boot

1. **Set up our stack** - Limine doesn't give us one
2. **Validate responses** - Check that Limine gave us what we asked for
3. **Initialize serial port** - For debugging before graphics work

### Core Kernel Systems

| System | Purpose | Uses Limine Data? |
|--------|---------|-------------------|
| PMM | Allocate physical pages | Yes - memory map |
| VMM | Virtual memory, page tables | Yes - HHDM offset |
| GDT | Segment descriptors for Ring 0/3 | No |
| IDT | Interrupt handlers | No |
| PIC | Hardware interrupt routing | No |
| Console | Text output | Yes - framebuffer |

### Example: Using the HHDM

The HHDM (Higher Half Direct Map) is one of Limine's most useful features. Here's how we use it:

```c
// Get the HHDM offset from Limine
uint64_t hhdm_offset = hhdm_request.response->offset;  // 0xFFFF800000000000

// Convert physical address to virtual
uint64_t phys_addr = 0x100000;  // Physical address 1MB
uint64_t virt_addr = phys_addr + hhdm_offset;

// Now we can access physical memory through the virtual address
uint8_t *ptr = (uint8_t *)virt_addr;
ptr[0] = 0x42;  // Writes to physical address 0x100000
```

This is how we:
- Initialize our bitmap in the PMM
- Write to page tables (which are physical structures)
- Copy user programs to allocated physical pages

---

## Building and Booting the ISO

Our `build-iso.sh` script assembles the bootable image:

```bash
# Directory structure of the ISO:
iso_root/
├── boot/
│   ├── kernel.elf              # Our kernel
│   └── limine/
│       ├── limine.conf         # Bootloader configuration
│       ├── limine-bios.sys     # Limine stage 2 (BIOS)
│       ├── limine-bios-cd.bin  # Boot sector for CD (BIOS)
│       └── limine-uefi-cd.bin  # UEFI boot for CD
└── EFI/
    └── BOOT/
        ├── BOOTX64.EFI         # UEFI bootloader (64-bit)
        └── BOOTIA32.EFI        # UEFI bootloader (32-bit)
```

The `limine.conf` tells Limine where to find our kernel:

```
timeout: 0

/Seed OS
    protocol: limine
    path: boot():/boot/kernel.elf
```

- `timeout: 0` - Boot immediately, no menu
- `protocol: limine` - Use the Limine boot protocol
- `path: boot():/boot/kernel.elf` - Kernel location on the boot device

---

## The Complete Picture

```
┌────────────────────────────────────────────────────────────────────────────┐
│                              BIOS/UEFI                                     │
│                                  │                                         │
│                        Load boot sector                                    │
│                                  ▼                                         │
│  ┌────────────────────────────────────────────────────────────────────┐   │
│  │                        LIMINE BOOTLOADER                           │   │
│  │                                                                    │   │
│  │  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐    │   │
│  │  │ Parse    │───▶│ Load     │───▶│ Set up   │───▶│ Fill     │    │   │
│  │  │ limine   │    │ kernel   │    │ paging   │    │ requests │    │   │
│  │  │ .conf    │    │ ELF      │    │ (HHDM)   │    │          │    │   │
│  │  └──────────┘    └──────────┘    └──────────┘    └──────────┘    │   │
│  │                                                         │         │   │
│  │                                           Jump to _start│         │   │
│  └─────────────────────────────────────────────────────────┼─────────┘   │
│                                                            ▼             │
│  ┌────────────────────────────────────────────────────────────────────┐   │
│  │                          OUR KERNEL                                │   │
│  │                                                                    │   │
│  │  _start (boot.S)           kernel_main (kernel.c)                  │   │
│  │  ┌────────────┐           ┌─────────────────────────────────────┐ │   │
│  │  │ Set up     │──────────▶│ Read Limine responses               │ │   │
│  │  │ stack      │           │ Initialize PMM (using memory map)   │ │   │
│  │  │            │           │ Initialize GDT, VMM, IDT, etc.      │ │   │
│  │  └────────────┘           │ Enter userspace / Start shell       │ │   │
│  │                           └─────────────────────────────────────┘ │   │
│  └────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## Further Reading

- [Limine Boot Protocol Specification](https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md)
- [OSDev Wiki: Limine](https://wiki.osdev.org/Limine)
- [OSDev Wiki: Booting](https://wiki.osdev.org/Booting)
- [OSDev Wiki: Setting Up Long Mode](https://wiki.osdev.org/Setting_Up_Long_Mode)
- [Intel Software Developer Manual, Vol. 3](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html) - Chapter 9 (Processor Management and Initialization)
