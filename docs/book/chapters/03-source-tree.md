# Chapter 3 — A Tour of the Source Tree

> Part I — Foundations · Status: ✅ drafted

> **Reference notes:** [`directory-tree-structure.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/directory-tree-structure.md)

## What this chapter covers

This chapter is a map. It walks the SeedOS source tree directory by directory so
that, for any feature in the book, you know which folder to open — and for any
file you stumble across, you know roughly what it does and which later chapter
explains it. The layout follows Linux kernel conventions, so the shape will feel
familiar if you have read kernel source before.

## Source files

The subject of this chapter is the tree itself. You can browse it on
[GitHub](https://github.com/wiktorjl/seedos) or list it locally with
`git ls-files`.

## 1. Why a Linux-style layout

SeedOS borrows Linux's top-level organization on purpose: architecture-specific
code is quarantined under `arch/`, memory management has its own `mm/`, devices
live in `drivers/`, and so on. The payoff is **separation of concerns** (each
subsystem has one home and clear boundaries) and **familiarity** (kernel
developers already know where to look). Two conventions run throughout:

- **Headers live next to their source.** `mm/pmm.c` is declared by `mm/pmm.h` in
  the same directory; there is no separate mirrored include tree except for
  genuinely global headers.
- **Architecture code stays in `arch/`.** Everything that knows about x86-64
  specifics — the GDT, IDT, APIC, port I/O — is under `arch/x86/`. The rest of
  the kernel is written against portable interfaces.

Every file opens with an `SPDX-License-Identifier: GPL-2.0-only` line.

## 2. The top-level map

```
seedos/
├── arch/x86/       # x86-64 specifics: boot, CPU tables, interrupts, APIC, syscalls
├── kernel/         # Core: printf, shell, threads, scheduler, processes, sync, ELF
├── mm/             # Memory management: PMM, VMM, heap, page refcounts
├── fs/             # Filesystems: VFS + ext2
├── drivers/        # Device drivers: tty (console/serial/terminal), input (keyboard)
├── init/           # Kernel entry point (main.c → kmain)
├── include/seedos/ # Global headers (types, log, config)
├── lib/            # Helper utilities (boot logo, sysinfo)
├── demos/          # Demos that show off kernel features (matrix rain)
├── userspace/      # User C runtime (crt0), syscall header, test programs
├── scripts/        # Build helpers (font, logo, initrd)
├── data/           # Binary assets (boot splash PNG; generated font)
├── docs/           # This book, reference notes, and design plans
└── build/          # Build output (generated; git-ignored)
```

The rest of this chapter walks each directory. The right-hand "covered in"
column is your jump table into the rest of the book.

## 3. `arch/x86/` — the machine-specific layer

Everything that depends on running on a 64-bit x86 CPU. It splits three ways.

**`arch/x86/boot/`** — the hand-off from the bootloader.

| File | Role | Covered in |
|------|------|-----------|
| `boot.S` | `_start`: validate Limine, set the stack, call `kmain`; embeds font/logo | Ch 4 |
| `limine.c`, `limine.h`, `limine_asm.S` | Limine protocol requests and accessors | Ch 4 |
| `limine.conf`, `startup.nsh` | Bootloader config + UEFI shell startup | Ch 4 |
| `linker.ld` | Places the kernel in the higher half | Ch 4 |

**`arch/x86/kernel/`** — CPU structures, interrupts, and the syscall gate.

| File | Role | Covered in |
|------|------|-----------|
| `gdt.c`, `gdt_asm.S` | Global Descriptor Table + TSS/IST | Ch 5 |
| `fpu.c` | FPU/SSE state init | Ch 5 |
| `idt.c`, `isr.S` | Interrupt Descriptor Table and ISR stubs | Ch 6 |
| `acpi.c` | ACPI table parsing (RSDP/RSDT/XSDT/MADT) | Ch 7 |
| `apic.c`, `ioapic.c` | Local APIC + I/O APIC | Ch 7 |
| `syscall.c`, `syscall_entry.S` | Syscall entry path | Ch 18 |
| `usermode.S`, `usermode.h` | Drop to ring 3 | Ch 17 |
| `percpu.c` | Per-CPU data area | Ch 22 |
| `cpu.h` | CPU helpers (`cpu_enable_interrupts`, stack accessors) | Ch 5 |

**`arch/x86/include/asm/io.h`** — the port I/O primitives (`inb`/`outb`) used by
the legacy-ish devices.

## 4. `mm/` — memory management

The three-layer memory stack, plus page reference counting for copy-on-write.

| File | Role | Covered in |
|------|------|-----------|
| `pmm.c` | Physical Memory Manager — bitmap page allocator | Ch 8 |
| `vmm.c` | Virtual Memory Manager — 4-level paging | Ch 9 |
| `heap.c` | `kmalloc`/`kfree`/`krealloc` | Ch 10 |
| `page.c` | Per-page reference counts (used by COW `fork`) | Ch 20 |
| `memory.h` | Memory-region and address constants | Ch 8–9 |

## 5. `kernel/` — the core

The architecture-independent heart of the kernel.

| File | Role | Covered in |
|------|------|-----------|
| `kprintf.c` | Formatted kernel output | Ch 12 |
| `kthread.c`, `kthread_switch.S` | Kernel threads + the scheduler/context switch | Ch 13 |
| `sync.c` | Spinlocks, mutexes, condition variables | Ch 14 |
| `kshell.c` | The interactive kernel shell | Ch 16 |
| `process.c` | The process model: `fork`, `waitpid`, `spawn` | Ch 19 |
| `elf.c` | ELF64 loader | Ch 21 |
| `kinit.c` | Launching `/init` from the initrd | Ch 19, 21 |
| `syscall_table.h` | The syscall number → handler table | Ch 18 |

## 6. `fs/` — filesystems

| File | Role | Covered in |
|------|------|-----------|
| `vfs.c` | The VFS layer and path resolution | Ch 23 |
| `ext2.c` | The read-only ext2 reader for the initrd | Ch 24 |

(File-descriptor management — `open`/`read`/`write`/`close` and the per-process
table — is covered in Ch 25.)

## 7. `drivers/` — devices

| File | Role | Covered in |
|------|------|-----------|
| `tty/console.c` | Framebuffer console (pixels, scrollback, cursor) | Ch 11 |
| `tty/terminal.c` | VT100-style terminal emulation | Ch 11 |
| `tty/serial.c` | COM1 serial driver (IRQ-driven receive) | Ch 11 |
| `tty/tty_dev.c` | The TTY device layer tying them together | Ch 11 |
| `tty/font.h` | Glyph rendering for the embedded font | Ch 11 |
| `input/keyboard.c` | PS/2 keyboard driver | Ch 15 |

## 8. `init/`, `include/`, `lib/`, `demos/`

- **`init/main.c`** — `kmain()`, the ordered bring-up sequence from Chapter 1.
  Short, and the best single file to read first. (Ch 1, Ch 4)
- **`include/seedos/`** — the only project-wide headers: `types.h` (fixed-width
  integers, `size_t`, …), `log.h` (`log_info`/`log_debug`/… macros, Ch 12), and
  `config.h` (the compile-time switches from Chapter 1).
- **`lib/`** — utilities that are not core kernel: `logo.c` (draws the boot
  splash) and `sysinfo.c` (the system summary printed at boot). (Appendix C)
- **`demos/matrix.c`** — a "matrix rain" animation that shows off preemptive
  multithreading. (Appendix C)

## 9. `userspace/` — the other side of the syscall boundary

Everything that runs in ring 3 lives here and builds with its own `Makefile`.

| Path | Role | Covered in |
|------|------|-----------|
| `crt/crt0.S` | The C runtime startup stub for user programs | Ch 26 |
| `include/syscall.h` | The userspace syscall wrappers | Ch 26 |
| `tests/*.c` | Standalone programs (`00_exit`, `01_write`, `08_fork_open`, …) | Ch 27 |

## 10. `scripts/`, `data/`, and `docs/`

- **`scripts/`** — `download-font.sh` and `convert-image.sh` produce the
  embedded assets, `mkinitrd.sh` builds the ext2 initrd, and `loc.sh` counts
  lines. (Ch 2)
- **`data/`** — binary assets. `seedos.png` (the boot splash) is tracked;
  `font.bin` and the source `.psfu` are generated and git-ignored. (Ch 2)
- **`docs/`** — `book/` (this book), `reference/` (terse per-subsystem notes
  being folded into the book), and `plans/` (forward-looking design docs).

Top-level, you will also find the `Makefile` and `configure` (Chapter 2), the
`README.md` and `CLAUDE.md`, and editor/tooling config (`.clang-format`,
`.gdbinit`, `.vscode/`).

## 11. Conventions worth remembering

- **Find a subsystem by its directory; find its detail in its header.** The
  `.h` next to a `.c` is the contract.
- **The build flattens objects.** Source at `mm/pmm.c` compiles to
  `build/pmm.o`; the `Makefile`'s `VPATH` keeps track of where each source
  lives, so dropping a new `.c` into a watched directory is enough to include
  it (Chapter 2).
- **Global vs. local headers.** Truly shared definitions go in
  `include/seedos/`; everything else stays beside its source or under
  `arch/x86/include/`.

## Reference & cross-links

- **Previous:** [Chapter 2 — Building & Running SeedOS](02-building-and-running.md).
- **Next:** [Chapter 4 — The Boot Process: Limine to `kmain`](04-boot-process.md)
  begins Part II and follows the very first files in this tree.
- **The bring-up order that ties these directories together:**
  [Chapter 1 §3](01-introduction.md).
