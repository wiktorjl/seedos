# Chapter 3 — A Tour of the Source Tree

> Part I — Foundations · Status: ✅ drafted

> **Reference notes:** [`directory-tree-structure.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/directory-tree-structure.md)

## What this chapter covers

This chapter is a map. It walks the SeedOS source tree directory by directory so
that, for any feature in the book, you know which folder to open — and for any
file you stumble across, you know roughly what it does and which later chapter
explains it. You do not need to understand each subsystem yet; this is the
"where things live" chapter, and every entry points at the chapter that explains
the "how."

> 🐍 **From Python.** A large Python project organizes code into packages —
> `models/`, `views/`, `utils/`. A kernel does the same, but it slices along
> different lines, because its problems are different: code that is specific to
> one CPU, code that manages raw memory, code that drives hardware devices, and
> so on. Once you learn the slicing, navigating is easy.

> 🧠 **First principles: `.c` and `.h` files.** C splits each module in two. The
> `.h` *header* declares *what* a module offers (its function signatures and
> types) — like the public interface; the `.c` *source* contains the actual
> implementation. Other files `#include` the header to use the module without
> seeing its internals. So `mm/pmm.h` is "what the physical allocator offers"
> and `mm/pmm.c` is "how it works."

## 1. Why a Linux-style layout

SeedOS borrows Linux's top-level organization on purpose: architecture-specific
code is quarantined under `arch/`, memory management has its own `mm/`, devices
live in `drivers/`, and so on. The payoff is **separation of concerns** (each
subsystem has one home) and **familiarity** (anyone who has seen kernel source
knows where to look). Two conventions run throughout:

- **Headers live next to their source.** `mm/pmm.c` is declared by `mm/pmm.h` in
  the same directory.
- **Architecture code stays in `arch/`.** Everything that knows about x86-64
  specifics — the CPU's tables, the interrupt hardware, port I/O — lives under
  `arch/x86/`. The rest of the kernel is written against portable interfaces, so
  a second CPU port would mostly mean a new `arch/` subtree.

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

> 🧠 **What this is.** Everything that depends on running on a 64-bit x86 CPU:
> talking to the bootloader, setting up the CPU's own data tables, and handling
> interrupts. If SeedOS were ever ported to another chip, this is the part that
> would be rewritten.

**`arch/x86/boot/`** — the hand-off from the bootloader to C.

| File | Role | Covered in |
|------|------|-----------|
| `boot.S` | `_start`: validate the bootloader, set the stack, call `kmain`; embeds font/logo | Ch 4 |
| `limine.c`, `limine.h`, `limine_asm.S` | Bootloader protocol requests and accessors | Ch 4 |
| `limine.conf`, `startup.nsh` | Bootloader config + UEFI shell startup | Ch 4 |
| `linker.ld` | Places the kernel in the higher half | Ch 4 |

**`arch/x86/kernel/`** — the CPU's tables, interrupt handling, and the syscall gate.

| File | Role | Covered in |
|------|------|-----------|
| `gdt.c`, `gdt_asm.S` | The Global Descriptor Table + TSS | Ch 5 |
| `fpu.c` | Floating-point / SSE setup | Ch 5 |
| `idt.c`, `isr.S` | The Interrupt Descriptor Table and handler stubs | Ch 6 |
| `acpi.c` | Reading firmware tables to discover hardware | Ch 7 |
| `apic.c`, `ioapic.c` | The modern interrupt controllers | Ch 7 |
| `syscall.c`, `syscall_entry.S` | The user→kernel call gate | Ch 18 |
| `usermode.S`, `usermode.h` | Dropping into ring 3 | Ch 17 |
| `percpu.c` | Per-CPU data area | Ch 22 |
| `cpu.h` | Small CPU helpers (`sti`/`cli`/`hlt`) | Ch 5 |

`arch/x86/include/asm/io.h` holds the `inb`/`outb` *port I/O* primitives (a way
of talking to certain devices, Chapter 7).

## 4. `mm/` — memory management

> 🧠 **What this is.** The code that decides how physical RAM is handed out and
> how each program gets its own private virtual view of memory. In Python the
> garbage collector and the OS handle all of this for you; here it's three layers
> we build ourselves.

| File | Role | Covered in |
|------|------|-----------|
| `pmm.c` | Physical Memory Manager — tracks which 4 KB pages of RAM are free | Ch 8 |
| `vmm.c` | Virtual Memory Manager — the page tables that translate addresses | Ch 9 |
| `heap.c` | `kmalloc`/`kfree` — the kernel's own `malloc` | Ch 10 |
| `page.c` | Per-page reference counts (used for copy-on-write `fork`) | Ch 20 |
| `memory.h` | Memory-region and address constants | Ch 8–9 |

## 5. `kernel/` — the core

> 🧠 **What this is.** The architecture-independent heart: printing, threads and
> scheduling, locks, the shell, and the process model. This is the code that
> would survive a port to a new CPU unchanged.

| File | Role | Covered in |
|------|------|-----------|
| `kprintf.c` | Formatted kernel output (our `printf`) | Ch 12 |
| `kthread.c`, `kthread_switch.S` | Threads and the scheduler that switches between them | Ch 13 |
| `sync.c` | Locks: spinlocks, mutexes, condition variables | Ch 14 |
| `kshell.c` | The interactive kernel shell | Ch 16 |
| `process.c` | The process model: `fork`, `waitpid`, `spawn` | Ch 19 |
| `elf.c` | Loading executable programs into memory | Ch 21 |
| `kinit.c` | Launching the first user program (`/init`) | Ch 19, 21 |
| `syscall_table.h` | The list mapping syscall numbers to handlers | Ch 18 |

## 6. `fs/` — filesystems

> 🧠 **What this is.** The code behind `open`/`read`/`close`. A *VFS* (virtual
> filesystem) is a uniform interface that real filesystems plug into, so the rest
> of the kernel doesn't care whether a file lives in ext2 or anywhere else.

| File | Role | Covered in |
|------|------|-----------|
| `vfs.c` | The VFS layer and path resolution (`/a/b/c`) | Ch 23 |
| `ext2.c` | The read-only ext2 reader for the RAM disk | Ch 24 |

(File descriptors — the small integers `open` returns and `read`/`write` take —
are Chapter 25.)

## 7. `drivers/` — devices

> 🧠 **What this is.** Code that talks to specific hardware. A *driver* is the
> translation layer between the kernel's generic requests ("show this text,"
> "give me a keystroke") and a particular device's registers and quirks.

| File | Role | Covered in |
|------|------|-----------|
| `tty/console.c` | Framebuffer console — drawing characters as pixels | Ch 11 |
| `tty/terminal.c` | VT100-style terminal behavior (escape codes, scrolling) | Ch 11 |
| `tty/serial.c` | The COM1 serial port driver | Ch 11 |
| `tty/tty_dev.c` | The TTY device layer tying console + serial together | Ch 11 |
| `tty/font.h` | Turning font bytes into on-screen glyphs | Ch 11 |
| `input/keyboard.c` | The PS/2 keyboard driver | Ch 15 |

## 8. `init/`, `include/`, `lib/`, `demos/`

- **`init/main.c`** — `kmain()`, the ordered bring-up sequence from Chapter 1.
  Short, and the single best file to read first. (Ch 1, Ch 4)
- **`include/seedos/`** — the only project-wide headers: `types.h` (fixed-width
  integer types and `size_t`), `log.h` (the `log_info`/`log_debug`/… macros,
  Ch 12), and `config.h` (compile-time switches like the log level and whether
  scheduling is preemptive).
- **`lib/`** — utilities that aren't core kernel: `logo.c` (draws the boot
  splash) and `sysinfo.c` (the system summary printed at boot). (Appendix C)
- **`demos/matrix.c`** — a "matrix rain" animation that shows off preemptive
  multithreading. (Appendix C)

## 9. `userspace/` — the other side of the wall

> 🧠 **What this is.** Everything that runs in *user mode* (ring 3, Chapter 1
> §4): programs that must ask the kernel — via system calls — for anything
> privileged. It builds with its own `Makefile`, separate from the kernel.

| Path | Role | Covered in |
|------|------|-----------|
| `crt/crt0.S` | The startup stub that runs before `main()` in a user program | Ch 26 |
| `include/syscall.h` | The userspace wrappers that invoke system calls | Ch 26 |
| `tests/*.c` | Standalone programs (`00_exit`, `01_write`, `08_fork_open`, …) | Ch 27 |

## 10. `scripts/`, `data/`, and `docs/`

- **`scripts/`** — `download-font.sh` and `convert-image.sh` produce the embedded
  assets, `mkinitrd.sh` builds the ext2 RAM disk, and `loc.sh` counts lines.
  (Ch 2)
- **`data/`** — binary assets. `seedos.png` (the boot splash) is tracked;
  `font.bin` and the source `.psfu` are generated and git-ignored. (Ch 2)
- **`docs/`** — `book/` (this book), `reference/` (terse per-subsystem notes
  being folded into the book), and `plans/` (forward-looking design docs).

Top-level, you'll also find the `Makefile` and `configure` (Chapter 2), the
`README.md` and `CLAUDE.md`, and editor/tooling config (`.clang-format`,
`.gdbinit`, `.vscode/`).

## 11. Conventions worth remembering

- **Find a subsystem by its directory; find its contract in its header.** The
  `.h` next to a `.c` tells you what it offers without reading the implementation.
- **The build flattens objects.** Source at `mm/pmm.c` compiles to `build/pmm.o`;
  the Makefile's `VPATH` tracks where each source lives, so dropping a new `.c`
  into a watched directory is enough to include it (Chapter 2).
- **Global vs. local headers.** Truly shared definitions go in `include/seedos/`;
  everything else stays beside its source or under `arch/x86/include/`.

## Reference & cross-links

- **Previous:** [Chapter 2 — Building & Running SeedOS](02-building-and-running.md).
- **Next:** [Chapter 4 — The Boot Process: Limine to `kmain`](04-boot-process.md)
  begins Part II and follows the very first files in this tree.
- **The bring-up order that ties these directories together:**
  [Chapter 1 §7](01-introduction.md).
