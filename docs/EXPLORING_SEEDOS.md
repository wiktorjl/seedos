# Exploring Seed OS Source Code

This guide suggests an order for reading the source code to understand how Seed OS works. Each section builds on the previous ones, so reading in order will give you a clear picture of how everything fits together.

## Prerequisites

Before diving in, you should be familiar with:
- C programming
- Basic x86-64 assembly (AT&T syntax)
- Hexadecimal numbers and bitwise operations
- Basic OS concepts (kernel vs userspace, virtual memory, interrupts)

## Reading Order

### Phase 1: Boot and Entry

Start here to understand how the system starts up.

| Order | File | Purpose |
|-------|------|---------|
| 1 | `config/limine.conf` | Bootloader configuration - how Limine finds and loads the kernel |
| 2 | `src/linker.ld` | Memory layout - where code and data are placed |
| 3 | `src/limine.h` | Bootloader protocol - structures the bootloader fills in |
| 4 | `src/boot.S` | Entry point - first code that runs, sets up stack |
| 5 | `src/kernel.c` | Main function - initialization sequence and main loop |

**Key concepts:** Limine bootloader protocol, higher-half kernel, HHDM (Higher Half Direct Map), kernel stack.

**Related docs:** `docs/BOOT.md`

### Phase 2: Low-Level I/O

These are the building blocks used by everything else.

| Order | File | Purpose |
|-------|------|---------|
| 6 | `src/io.h` | Port I/O - `inb`/`outb` for talking to hardware |
| 7 | `src/memory.h` | Memory operations - `memset`/`memcpy` implementations |

**Key concepts:** x86 I/O ports, inline assembly, `volatile` keyword.

### Phase 3: Output Systems

Understand how the kernel displays text.

| Order | File | Purpose |
|-------|------|---------|
| 8 | `src/kernel.c` (serial section) | Serial driver - UART 16550 on COM1 |
| 9 | `src/fb.h` | Framebuffer API |
| 10 | `src/fb.c` | Framebuffer driver - pixel drawing and bitmap font |
| 11 | `src/console.h` | Console API |
| 12 | `src/console.c` | Console - unified serial + framebuffer output |

**Key concepts:** UART registers, framebuffer graphics, bitmap fonts, dual output.

**Related docs:** `docs/SERIAL.md`

### Phase 4: Memory Management

This is critical - nearly everything depends on memory allocation.

| Order | File | Purpose |
|-------|------|---------|
| 13 | `src/pmm.h` | Physical memory API |
| 14 | `src/pmm.c` | Physical Memory Manager - bitmap allocator for 4KB pages |
| 15 | `src/vmm.h` | Virtual memory API |
| 16 | `src/vmm.c` | Virtual Memory Manager - page tables, address spaces |

**Key concepts:** Physical vs virtual addresses, page frames, bitmap allocation, 4-level paging (PML4/PDPT/PD/PT), page table entries, HHDM for physical access.

**Related docs:** `docs/PMM.md`, `docs/VMM.md`, `docs/MEMORY_LAYOUT.md`

### Phase 5: CPU Structures

GDT must be understood before interrupts and userspace.

| Order | File | Purpose |
|-------|------|---------|
| 17 | `src/gdt.h` | GDT/TSS structures and segment selectors |
| 18 | `src/gdt.c` | Global Descriptor Table - segments for ring 0/3 |
| 19 | `src/gdt_load.S` | Assembly to load GDT and TSS |

**Key concepts:** Segment descriptors, privilege rings (ring 0 vs ring 3), Task State Segment, kernel stack pointer (RSP0).

### Phase 6: Interrupts

Interrupts enable hardware interaction and system calls.

| Order | File | Purpose |
|-------|------|---------|
| 20 | `src/idt.h` | IDT structures |
| 21 | `src/idt.c` | Interrupt Descriptor Table - exception and IRQ handlers |
| 22 | `src/isr.S` | Interrupt Service Routines - assembly stubs |
| 23 | `src/pic.h` | PIC API |
| 24 | `src/pic.c` | Programmable Interrupt Controller - IRQ routing |

**Key concepts:** Interrupt gates, exception handling, IRQ remapping, EOI (End of Interrupt), CPU state saving/restoring.

**Related docs:** `docs/IRQ.md`

### Phase 7: Device Drivers

Currently just keyboard, but follows the interrupt foundation.

| Order | File | Purpose |
|-------|------|---------|
| 25 | `src/keyboard.h` | Keyboard API |
| 26 | `src/keyboard.c` | PS/2 keyboard driver - scancodes to ASCII |

**Key concepts:** IRQ handlers, scancode sets, circular buffer, polling vs interrupt-driven.

### Phase 8: System Calls

The interface between userspace and kernel.

| Order | File | Purpose |
|-------|------|---------|
| 27 | `src/syscall.h` | Syscall numbers and API |
| 28 | `src/syscall.c` | System call handler - dispatches `int 0x80` |

**Key concepts:** Software interrupts, syscall convention (registers), `sys_write`, `sys_exit`.

**Related docs:** `docs/SYSCALLS.md`

### Phase 9: Context Switching

How the kernel enters and exits userspace.

| Order | File | Purpose |
|-------|------|---------|
| 29 | `src/context.h` | User context structure |
| 30 | `src/context_switch.S` | Assembly for ring transitions |

**Key concepts:** `iretq` instruction, stack frames for privilege transitions, saving/restoring register state.

### Phase 10: User Programs

The other side of the syscall interface.

| Order | File | Purpose |
|-------|------|---------|
| 31 | `src/userspace/user_program.s` | User program source - "Hello World" in assembly |
| 32 | `src/userspace/user_program.h` | User binary header |
| 33 | `src/userspace/user_program.c` | Generated - binary embedded as C array |

**Key concepts:** User-mode assembly, syscall invocation from userspace, position-independent code.

### Phase 11: Shell and Tests

Interactive functionality built on top of everything.

| Order | File | Purpose |
|-------|------|---------|
| 34 | `src/shell.h` | Shell API |
| 35 | `src/shell.c` | Interactive shell - command parsing and execution |
| 36 | `src/tests.h` | Test API |
| 37 | `src/tests.c` | Test commands - memory map, VMM, userspace tests |

**Key concepts:** Command parsing, string handling without libc, interactive I/O.

## Suggested Deep Dives

After reading through all files, consider studying these topics in depth:

### Memory Management Path
`pmm.c` -> `vmm.c` -> `context_switch.S` -> `syscall.c`

Follow how physical pages are allocated, mapped into virtual address spaces, and used for user processes.

### Interrupt Path
`isr.S` -> `idt.c` -> `pic.c` -> `keyboard.c`

Follow an IRQ from hardware through to the driver handler.

### Syscall Path
`user_program.s` -> `isr.S` -> `syscall.c` -> `console.c`

Follow a `sys_write` call from userspace through to screen output.

### Boot Path
`limine.conf` -> `boot.S` -> `kernel.c` -> `shell.c`

Follow the system from power-on to interactive shell.

## File Dependency Graph

```
                    limine.h
                       |
    boot.S -------> kernel.c
                    /   |   \
                   /    |    \
              pmm.c  gdt.c  fb.c
                |      |      |
              vmm.c  idt.c  console.c
                |      |      |
                +------+------+
                       |
                   syscall.c
                       |
               context_switch.S
                       |
                 user_program.s
```

## Tips for Reading

1. **Start with headers** - Read the `.h` file before the `.c` file to understand the API.

2. **Follow the init order** - `kernel_main()` calls init functions in dependency order. This is intentional.

3. **Use the docs** - The `docs/` folder has detailed explanations of specific subsystems.

4. **Run tests** - Boot Seed OS and run `test memmap`, `test vmm`, `test user` to see the code in action.

5. **Read comments** - The source is heavily commented to explain the "why" not just the "what".

6. **Trace execution** - Add `puts()` calls to trace code paths if something is unclear.
