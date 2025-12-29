# SeedOS

A minimal x86-64 bare-metal operating system kernel written in C and assembly.

## Current Status

SeedOS boots via UEFI using the Limine bootloader and provides an interactive kernel shell with basic system functionality. The project contains approximately 5,500 lines of code across 38 source files.

### Features Implemented

**Boot & Hardware**
- UEFI boot via Limine bootloader
- Framebuffer graphics with custom font (8x16)
- PS/2 keyboard driver with interrupt-driven input
- ACPI parsing (RSDP, RSDT, XSDT, MADT)
- Local APIC and I/O APIC initialization
- Timer interrupts (100 Hz)

**Memory Management**
- Physical Memory Manager (PMM) - bitmap-based page allocator
- Virtual Memory Manager (VMM) - 4-level paging (PML4/PDPT/PD/PT)
- Heap allocator (kmalloc/kfree/krealloc)

**Interrupt Handling**
- Full IDT with 256 entries
- CPU exception handlers (divide by zero, page fault, etc.)
- Hardware IRQ routing through I/O APIC

**Console & Terminal**
- Framebuffer text rendering with color support
- Text scrollback history (Page Up/Down)
- Cursor blinking
- Boot splash with logo

**Kernel Shell (kshell)**
- Interactive command-line interface
- Built-in commands: `help`, `clear`, `echo`, `version`, `meminfo`, `reboot`
- Command history with up/down arrow navigation
- Line editing with backspace support

## Building

```bash
make          # Build bootable ISO
make run      # Run in QEMU
make debug    # Run in QEMU with GDB server
make clean    # Remove build artifacts
```

## Project Structure

```
seedos/
├── src/        # Kernel source code
├── data/       # Binary resources (font, logo)
├── config/     # Limine bootloader configuration
├── scripts/    # Build helper scripts
├── limine/     # External bootloader (not modified)
└── build/      # Build output
```

## Not Yet Implemented

- Multi-CPU/SMP support
- User-mode execution
- Process/task management
- File system or disk I/O
- Network stack
