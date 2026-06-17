# SeedOS Directory Tree Structure

This document describes the source tree organization of SeedOS. The layout follows Linux kernel conventions, making the codebase familiar to kernel developers and easier to navigate.

## Overview

```
seedos/
├── arch/           # Architecture-specific code
├── data/           # Binary assets and resources
├── demos/          # Demo programs (not core OS)
├── docs/           # Documentation
├── drivers/        # Device drivers
├── include/        # Global headers
├── init/           # Kernel initialization
├── kernel/         # Core kernel subsystems
├── lib/            # Helper libraries
├── mm/             # Memory management
├── scripts/        # Build and utility scripts
└── screenshots/    # Project screenshots
```

## Directory Details

### `arch/` - Architecture-Specific Code

Contains all code that is specific to a particular CPU architecture. Currently only x86_64 is supported.

```
arch/
└── x86/
    ├── boot/       # Bootloader integration and startup
    ├── include/    # Architecture-specific headers
    └── kernel/     # CPU, interrupts, and hardware abstraction
```

| File | Description |
|------|-------------|
| `boot/boot.S` | Early assembly startup code |
| `boot/limine.c` | Limine bootloader protocol handler |
| `boot/limine.conf` | Bootloader configuration |
| `boot/linker.ld` | Kernel linker script |
| `kernel/acpi.c` | ACPI table parsing |
| `kernel/apic.c` | Local APIC timer and IPI support |
| `kernel/idt.c` | Interrupt Descriptor Table setup |
| `kernel/ioapic.c` | I/O APIC interrupt routing |
| `kernel/isr.S` | Interrupt service routine stubs |
| `include/asm/io.h` | Port I/O primitives (`inb`, `outb`) |

### `drivers/` - Device Drivers

Contains hardware device drivers organized by subsystem.

```
drivers/
├── input/          # Input devices
│   ├── keyboard.c  # PS/2 keyboard driver
│   └── keyboard.h
└── tty/            # Terminal devices
    ├── console.c   # Framebuffer console
    ├── font.h      # PSF font rendering
    ├── serial.c    # Serial port (COM1) driver
    └── terminal.c  # Terminal emulation (VT100-like)
```

### `include/` - Global Headers

Project-wide headers that don't belong to a specific subsystem.

```
include/
└── seedos/
    ├── config.h    # Build configuration options
    ├── log.h       # Logging macros (LOG_INFO, LOG_DEBUG, etc.)
    └── types.h     # Basic type definitions (uint32_t, size_t, etc.)
```

### `init/` - Kernel Initialization

Contains the kernel entry point and early initialization.

| File | Description |
|------|-------------|
| `main.c` | `kernel_main()` - kernel entry point after boot |

### `kernel/` - Core Kernel Subsystems

The heart of the kernel: scheduling, printing, shell, and synchronization.

| File | Description |
|------|-------------|
| `kprintf.c` | Kernel printf implementation |
| `kshell.c` | Interactive kernel shell |
| `kthread.c` | Cooperative threading (kernel threads) |
| `kthread_switch.S` | Context switch assembly |
| `sync.c` | Spinlocks and synchronization primitives |

### `demos/` - Demo Programs

Demonstration programs that showcase kernel features but are not part of the core OS.

| File | Description |
|------|-------------|
| `matrix.c` | Matrix rain animation demonstrating preemptive multithreading |

### `lib/` - Helper Libraries

Utility code that isn't part of the core kernel.

| File | Description |
|------|-------------|
| `logo.c` | ASCII logo display |
| `sysinfo.c` | System information display |

### `mm/` - Memory Management

Physical and virtual memory management subsystems.

| File | Description |
|------|-------------|
| `pmm.c` | Physical memory manager (bitmap allocator) |
| `vmm.c` | Virtual memory manager (page tables) |
| `heap.c` | Kernel heap allocator (`kmalloc`/`kfree`) |
| `memory.h` | Memory region definitions |

### `scripts/` - Build and Utility Scripts

Helper scripts for building and development.

| File | Description |
|------|-------------|
| `convert-image.sh` | Convert images for bootloader |
| `download-font.sh` | Download PSF fonts |

### `data/` - Binary Assets

Non-code resources used by the kernel.

| File | Description |
|------|-------------|
| `font.bin` | Compiled font data |
| `spleen-8x16.psfu` | PSF2 console font |
| `seedos.png` | Boot splash image |

## Design Rationale

### Why Linux-Style Layout?

1. **Familiarity**: Developers familiar with Linux can navigate easily
2. **Scalability**: Structure supports adding new architectures and drivers
3. **Separation of concerns**: Clear boundaries between subsystems
4. **Header organization**: `arch/*/include/` for arch-specific, `include/` for global

### Key Conventions

- **Headers live with source**: Each `.c` file typically has a corresponding `.h` in the same directory
- **Architecture isolation**: All x86-specific code stays under `arch/x86/`
- **Driver categorization**: Drivers grouped by type (`input/`, `tty/`)
- **No mixing**: Memory management in `mm/`, core kernel in `kernel/`, etc.
