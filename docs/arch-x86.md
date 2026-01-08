# SeedOS x86-64 Architecture Subsystem

This document describes the x86-64 architecture support in SeedOS, covering the boot process, CPU initialization, interrupt handling, ACPI parsing, and APIC configuration.

## Overview

SeedOS targets x86-64 (long mode) exclusively and uses modern hardware features:

- **Limine Boot Protocol**: A modern bootloader protocol that handles the transition to long mode, sets up paging, and provides boot information.
- **APIC-based Interrupts**: Uses the Local APIC and I/O APIC instead of the legacy 8259 PIC.
- **ACPI for Hardware Discovery**: Parses ACPI tables to discover CPUs, APICs, and interrupt routing.
- **Higher-Half Kernel**: The kernel is loaded at virtual address `0xFFFFFFFF80000000`.

## Directory Structure

```
arch/x86/
├── boot/
│   ├── boot.S           # Entry point, stack, embedded data
│   ├── limine.c         # Limine protocol request handlers
│   ├── limine.h         # Limine protocol type definitions
│   ├── limine.conf      # Bootloader configuration
│   └── linker.ld        # Kernel linker script
├── kernel/
│   ├── acpi.c/h         # ACPI table parser
│   ├── apic.c/h         # Local APIC and timer driver
│   ├── cpu.h            # CPU primitives (cli, sti, hlt)
│   ├── idt.c/h          # Interrupt Descriptor Table setup
│   ├── ioapic.c/h       # I/O APIC driver
│   └── isr.S            # ISR assembly stubs
└── include/asm/
    └── io.h             # Port I/O functions (inb, outb)
```

## Boot Process

### Limine Bootloader Integration

SeedOS uses Limine Boot Protocol revision 3, which provides:
- **Framebuffer**: For graphical console output
- **HHDM**: Higher Half Direct Map offset for physical-to-virtual translation
- **Memory Map**: Physical memory regions
- **RSDP**: Root System Description Pointer for ACPI

### Entry Point (`_start` in boot.S)

```asm
_start:
    # Verify Limine protocol version
    mov limine_base_revision+8(%rip), %rax
    cmp %rcx, %rax
    je .halt

    # Set up kernel stack (16 KiB)
    lea stack_top(%rip), %rsp
    xor %rbp, %rbp
    call kmain
```

## CPU Primitives (`cpu.h`)

```c
static inline void cpu_enable_interrupts(void)  { __asm__ volatile("sti"); }
static inline void cpu_disable_interrupts(void) { __asm__ volatile("cli"); }
static inline void cpu_halt(void)               { __asm__ volatile("hlt"); }
```

## Interrupt Handling (IDT)

The IDT routes CPU exceptions and hardware interrupts:
- **Vectors 0-31**: CPU exceptions (divide error, page fault, etc.)
- **Vectors 32-47**: Hardware IRQs via I/O APIC
- **Vector 255**: Spurious interrupt

### Registering IRQ Handlers

```c
void idt_register_irq(int irq, irq_handler_t handler);
// Example: idt_register_irq(33, keyboard_irq_handler);
```

## ACPI Table Parsing

Parses RSDP, RSDT/XSDT, and MADT to discover:
- Local APIC address
- I/O APIC address and ID
- CPU count and APIC IDs
- IRQ source overrides

## Local APIC Configuration

The LAPIC handles local timer interrupts, IPIs, and interrupt delivery:

```c
void apic_init(void);           // Initialize APIC and timer at 100 Hz
void apic_eoi(void);            // End of interrupt
uint64_t apic_get_ticks(void);  // Tick count since boot
```

## I/O APIC Configuration

Routes external device interrupts to CPUs:

```c
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t apic_id);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);
```

## Initialization Order

```c
console_init(fb);           // 1. Console output
idt_install();              // 2. Interrupt handling
pmm_init(...);              // 3. Physical memory
vmm_init(...);              // 4. Virtual memory
kheap_init();               // 5. Dynamic allocation
acpi_init();                // 6. ACPI (needs VMM)
apic_init();                // 7. Local APIC (needs ACPI)
ioapic_init();              // 8. I/O APIC (needs ACPI)
keyboard_init();            // 9. Devices
cpu_enable_interrupts();    // 10. Enable interrupts
```

## References

- Intel 64 and IA-32 Architectures Software Developer Manuals
- ACPI Specification
- Limine Boot Protocol
