# Seed OS

A minimal x86-64 operating system built from scratch. Successfully runs "Hello World" from userspace!

## About This Project

This is a hobby project I started to re-learn fundamental CS concepts in the operating systems domain.

**Goals:**
1. **Readability** - Code should be clear and well-documented
2. **Modifiability** - Easy to extend and experiment with
3. **Educational value** - Useful for anyone learning OS development

**Non-goals:**
- Performance optimization
- Completeness
- Feature richness

**Vision:** Eventually this OS will have multitasking, a networking stack, proper userspace, and a filesystem.

**Development approach:** This project relies heavily on AI-generated code. Operating systems are large projects, and to cover significant ground learning about various subsystems, AI assistance saves time on debugging and boilerplate. There is value in writing everything from scratch and fighting through every bug, but that is not the focus here. This OS is meant to be a playground for ideas, so the priority is getting basic facilities implemented and working.

## Current State (Userspace Working)

### Completed Components

**Boot & Core:**
- Limine bootloader (BIOS/UEFI compatible)
- x86-64 long mode, higher-half kernel at `0xFFFFFFFF80000000`
- HHDM (Higher Half Direct Map) at `0xFFFF800000000000`

**Memory:**
- PMM (Physical Memory Manager) - bitmap-based page allocator
- VMM (Virtual Memory Manager) - 4-level paging, per-process address spaces
- 4KB pages, kernel mapped in upper half of all address spaces

**Interrupts:**
- IDT with 48 handlers (exceptions 0-31, IRQs 32-47) + syscall (128)
- Assembly stubs in isr.S normalize error codes
- PIC remapped to IRQs 32-47

**Hardware:**
- PS/2 keyboard driver with scancode translation
- Serial port (COM1) for debug output

**Display:**
- Framebuffer graphics via Limine protocol
- 8x16 bitmap font, text console with scrolling
- `console.c` module provides unified output to both serial and framebuffer

**GDT + TSS:**
- Custom GDT with kernel (DPL=0) and user (DPL=3) segments
- TSS for RSP0 (kernel stack on ring transition)
- Segment selectors: 0x08 kernel code, 0x10 kernel data, 0x18 user code, 0x20 user data, 0x28 TSS

**Syscalls:**
- `int 0x80` handler with DPL=3 (user-callable)
- `sys_write(fd, buf, len)` - write to console
- `sys_exit(code)` - terminate process and return to kernel

**Context Switching:**
- `context_switch_to_user()` - enter userspace, returns when sys_exit called
- `context_return_to_kernel()` - return from userspace to kernel
- Clean separation of assembly (context_switch.S) and C code

**Shell:**
- Interactive commands: help, meminfo, alloc, free, crash, divzero, clear

### File Structure

```
~/os/
├── boot.S          # Entry point, stack setup
├── kernel.c        # Main, serial I/O, initialization
├── linker.ld       # Linker script (higher-half)
├── limine.h        # Boot protocol structures
├── limine.conf     # Bootloader config
├── Makefile
├── pmm.h/c         # Physical memory manager
├── vmm.h/c         # Virtual memory manager
├── idt.h/c         # Interrupt descriptor table
├── isr.S           # Interrupt service routines (asm)
├── pic.h/c         # Programmable interrupt controller
├── keyboard.h/c    # PS/2 keyboard driver
├── shell.h/c       # Interactive shell
├── fb.h/c          # Framebuffer driver + font
├── console.h/c     # Unified serial+FB output
├── gdt.h/c         # Global descriptor table
├── gdt_load.S      # GDT/TSS loading (asm)
├── syscall.h/c     # System call handler
├── context.h       # Context switching API
├── context_switch.S # Context switching (asm)
├── user_program.h/c # Hardcoded user binary
├── tests.h/c       # Kernel test suite (shell commands)
└── user.S          # User program source (assembled separately)
```

## Completed Roadmap

- [x] **Phase 1: GDT + TSS** - Ring 3 segments, kernel stack for interrupts
- [x] **Phase 2: VMM** - Per-process page tables, map kernel + user pages
- [x] **Phase 3: Syscalls** - `int 0x80` handler, sys_write, sys_exit
- [x] **Phase 4: Context struct** - `struct user_context` with pml4, entry, stack
- [x] **Phase 5: User program** - Hardcoded "Hello World" binary
- [x] **Phase 6: Enter userspace** - Build iretq frame, jump to ring 3, return via sys_exit

## Build & Run

```bash
cd ~/os
make
./build-iso.sh
qemu-system-x86_64 -cdrom seed.iso -serial stdio
```

QEMU window: keyboard input, graphics output
Terminal: serial output (debugging)

## Architecture Notes

### Memory Layout
```
0xFFFFFFFFFFFFFFFF ┐
                   │ Kernel Space (shared across all address spaces)
0xFFFFFFFF80000000 │ <- Kernel code/data
                   │
0xFFFF800000000000 │ <- HHDM (physical memory direct map)
                   ┘
    (non-canonical hole)
0x0000800000000000 ┐
                   │ User Space (per-process)
0x7FFFFF000        │ <- User stack
                   │
0x400000           │ <- User code
0x0                ┘
```

### Key APIs

**Console output:**
- `puts()`, `putc()`, `put_hex()`, `put_dec()` - dual serial+framebuffer

**Memory:**
- `pmm_alloc()` / `pmm_free()` - physical page allocation
- `vmm_create_address_space()` - new PML4 with kernel mapped
- `vmm_map_page(pml4, virt, phys, flags)` - map a page

**Context switching:**
- `context_switch_to_user(&ctx)` - enter userspace, returns on sys_exit
- `context_return_to_kernel()` - called by sys_exit

**GDT selectors (gdt.h):**
- `GDT_KERNEL_CODE` (0x08), `GDT_KERNEL_DATA` (0x10)
- `GDT_USER_CODE` (0x18), `GDT_USER_DATA` (0x20)
- `GDT_TSS` (0x28)

## Future Ideas

- Process scheduler (round-robin, multiple processes)
- ELF loader (load programs from disk)
- Virtual filesystem
- More syscalls (read, open, close, fork, exec)
- User-space memory allocation (sbrk/mmap)
