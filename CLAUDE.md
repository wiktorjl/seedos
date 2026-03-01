# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make          # Build bootable ISO (build/seed.iso)
make run      # Run in QEMU with serial to stdout
make debug    # Run in QEMU with GDB server (-s -S)
make clean    # Remove build artifacts
make compdb   # Generate compile_commands.json for clangd
```

First build will auto-clone Limine bootloader. Requires: `xorriso`, `qemu-system-x86_64`, OVMF firmware at `/usr/share/ovmf/OVMF.fd`.

## Architecture Overview

SeedOS is a minimal x86-64 bare-metal kernel using:
- **Limine Boot Protocol v3**: Provides framebuffer, HHDM, memory map, RSDP
- **APIC-based interrupts**: Local APIC + I/O APIC (no legacy 8259 PIC)
- **Higher-half kernel**: Loaded at `0xFFFFFFFF80000000`, HHDM at `~0xFFFF800000000000`

### Boot Flow

```
UEFI → Limine → _start (arch/x86/boot/boot.S) → kmain (init/main.c) → kshell_run()
```

### Initialization Order (in kmain)

Subsystems must initialize in dependency order:
1. Console/Terminal → 2. IDT → 3. PMM → 4. VMM → 5. Heap → 6. ACPI → 7. APIC → 8. I/O APIC → 9. Keyboard → 10. Enable interrupts → 11. Threads → 12. Shell

### Memory Management Stack

```
PMM (mm/pmm.c)     - Bitmap allocator for 4KB physical pages
VMM (mm/vmm.c)     - 4-level paging (PML4→PDPT→PD→PT)
Heap (mm/heap.c)   - kmalloc/kfree with free-list allocator
```

Physical-to-virtual translation uses HHDM: `phys_to_virt()` / `virt_to_phys()` macros.

### Key APIs

**Logging** (`include/seedos/log.h`):
```c
log_info("message");   // Also: log_debug, log_warn, log_error, log_panic
```

**Memory**:
```c
void *kmalloc(size_t size);
void kfree(void *ptr);
uint64_t pmm_alloc(void);  // Returns physical address of 4KB page
```

**Threading** (`kernel/kthread.c`):
```c
kthread_create(name, entry_fn, arg);
kthread_yield();
kthread_sleep(ms);  // 10ms granularity
```

**Synchronization** (`kernel/sync.c`):
```c
spinlock_t lock = SPINLOCK_INIT;  // For short critical sections
mutex_t m = MUTEX_INIT;           // Sleep-based locking
cond_t c = COND_INIT;             // Condition variables
```

**IRQ Registration** (`arch/x86/kernel/idt.c`):
```c
idt_register_irq(irq_number, handler_function);
```

## Directory Layout

Follows Linux kernel conventions:
- `arch/x86/` - x86-64 specific: boot, ACPI, APIC, IDT, ISRs
- `kernel/` - Core: kprintf, kshell, kthread, sync
- `mm/` - Memory management: PMM, VMM, heap
- `drivers/` - Device drivers: `tty/` (console, serial, terminal), `input/` (keyboard)
- `init/` - Kernel entry point (`main.c`)
- `include/seedos/` - Global headers (types.h, log.h, config.h)
- `lib/` - Utilities (logo, sysinfo)

## Adding Features

**New shell command**: Add to `commands[]` array in `kernel/kshell.c`

**New subsystem**: Create init function, add call to `kmain()` in `init/main.c` respecting init order

**New IRQ handler**: Use `idt_register_irq()`, call `apic_eoi()` at end of handler

## Configuration

Edit `include/seedos/config.h` for:
- `CONFIG_LOG_LEVEL` - Verbosity (0=PANIC to 5=TRACE)
- `CONFIG_OUTPUT_SERIAL` - Enable/disable serial output
- `CONFIG_KTHREAD_PREEMPTIVE` - Preemptive (1) vs cooperative (0) scheduling

## Debugging

```bash
make debug                    # Start QEMU paused with GDB server on :1234
gdb build/kernel.elf -ex "target remote :1234"
```

Serial output goes to stdout when using `make run` or `make debug`.

## Code comments
## Additional notes

README.md should be always kept up to date.

