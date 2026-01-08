# SeedOS Init/Boot Subsystem

## Boot Flow

```
BIOS/UEFI -> Limine -> _start (boot.S) -> kmain (main.c) -> kshell_run()
```

## Kernel Entry Point

### Assembly Entry (`_start`)

1. Validates Limine protocol revision 3
2. Sets up 16KB kernel stack
3. Calls `kmain()`

### C Entry (`kmain`)

Located in `init/main.c`. Initializes subsystems in dependency order.

## Initialization Sequence

| Order | Subsystem | Function |
|-------|-----------|----------|
| 1 | Console | `console_init(fb)` |
| 2 | Terminal | `terminal_init()` |
| 3 | IDT | `idt_install()` |
| 4 | PMM | `pmm_init(memmap, hhdm)` |
| 5 | VMM | `vmm_init(hhdm)` |
| 6 | Heap | `kheap_init()` |
| 7 | ACPI | `acpi_init()` |
| 8 | APIC | `apic_init()` |
| 9 | I/O APIC | `ioapic_init()` |
| 10 | Keyboard | `keyboard_init()` |
| 11 | Interrupts | `cpu_enable_interrupts()` |
| 12 | Sysinfo | `sysinfo_init()` |
| 13 | Threads | `kthread_init()` |
| 14 | Shell | `kshell_run()` |

## Configuration (`config.h`)

### Log Levels

```c
#define LOG_PANIC 0
#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3
#define LOG_DEBUG 4
#define LOG_TRACE 5
#define CONFIG_LOG_LEVEL LOG_TRACE
```

### Output Backends

```c
#define CONFIG_OUTPUT_CONSOLE 1
#define CONFIG_OUTPUT_SERIAL  1
```

### Thread Scheduling

```c
#define CONFIG_KTHREAD_PREEMPTIVE 1  // 0=cooperative, 1=preemptive
```

## Limine Boot Protocol

Requests: Framebuffer, HHDM, Memory Map, RSDP

What Limine provides:
- CPU in 64-bit long mode, paging enabled
- Kernel at `0xFFFFFFFF80000000`
- HHDM at `~0xFFFF800000000000`

## Adding a New Subsystem

1. Create init function in your source file
2. Add header include to `init/main.c`
3. Call init function in `kmain()` respecting dependencies
