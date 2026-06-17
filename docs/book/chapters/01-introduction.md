# Chapter 1 — Introduction & Design Philosophy

> Part I — Foundations · Status: ✅ drafted

> **Reference notes:** [root `README.md`](https://github.com/wiktorjl/seedos/blob/master/README.md)

## What this chapter covers

After this chapter you will know what SeedOS *is*, what it can do today, and —
more importantly — the handful of design decisions that shape every other
chapter in this book. Why does it target modern UEFI/APIC hardware instead of
legacy BIOS and the 8259 PIC? Why does the source tree look like Linux's? Why
is it built as *freestanding* C with no standard library? Those choices are the
"why" behind almost every "how" that follows.

## Source files

- `README.md` — the project's own feature summary
- `init/main.c` — `kmain()`, the ordered list of subsystems that *is* the system
- `include/seedos/config.h` — the compile-time switches that define a build

## 1. What SeedOS is

SeedOS is a 64-bit x86 operating system written from scratch in C and a little
assembly. It is small enough to read end to end, but complete enough to be a
real system: it boots on UEFI firmware, brings up its own memory management and
interrupt handling, schedules preemptively, and drops you into a userspace
shell that can run standalone ELF programs such as `ls`, `cat`, `grep`, and
`sort`.

It is deliberately a *single-CPU, single-architecture* kernel. There is no SMP,
no second port, and no sprawling driver collection to wade through. What you get
instead is one coherent path from the first instruction the bootloader hands off
to a shell prompt — every link in that chain visible and documented.

Concretely, a booted SeedOS today provides:

- **Boot & hardware** — UEFI boot through the Limine protocol (revision 3); a
  higher-half kernel at `0xFFFFFFFF80000000` with the full physical address
  space mirrored at the HHDM base `0xFFFF800000000000`; Local APIC + I/O APIC
  interrupts (no legacy 8259 PIC); ACPI table parsing; a PS/2 keyboard; a
  COM1 serial port with interrupt-driven receive; and a framebuffer console
  with an 8×16 bitmap font.
- **Memory** — a bitmap physical page allocator (PMM), 4-level paging (VMM)
  with a private address space per process, a `kmalloc`/`kfree`/`krealloc`
  heap, and copy-on-write page-fault handling for `fork()`.
- **Processes** — a preemptive round-robin scheduler, `fork()`/`waitpid()`/
  `spawn()`, an ELF64 loader, and per-process file-descriptor tables and
  working directories.
- **System call interface** — 22 Unix-style syscalls (file I/O, directory
  operations, process control, and a few system calls like `uptime` and
  `shutdown`).
- **Filesystem** — a VFS layer over an ext2 RAM disk (the *initrd*) embedded in
  the boot image.
- **Userspace** — a small C library, an interactive shell, and roughly twenty
  user programs.

If you want the authoritative, always-current list, it lives in the project
[`README.md`](https://github.com/wiktorjl/seedos/blob/master/README.md). The
rest of this book explains how each of those bullet points actually works.

## 2. Five design principles

Almost everything in SeedOS follows from five choices. Keep them in mind and the
code stops looking arbitrary.

### Modern hardware, not legacy

SeedOS only targets a machine that already looks like 2020s hardware. It boots
via **UEFI** (handed a 64-bit long-mode CPU with paging already on), routes
interrupts through the **Local APIC and I/O APIC**, and never touches the
8259 PIC, BIOS calls, or real-mode startup. This removes a large amount of
historical scaffolding — there is no A20 gate, no real-to-protected-to-long
mode climb, no PIC remapping — and lets the code start where a modern kernel
actually lives. The trade-off is that SeedOS will not boot on a legacy BIOS-only
machine, which is a price it happily pays.

### Familiar structure

The tree follows **Linux kernel conventions**: `arch/` for the
architecture-specific layer, `kernel/` for the core, `mm/` for memory, `drivers/`
for devices, `fs/` for filesystems, `init/` for startup, and `include/` for
global headers. Every file carries an `SPDX-License-Identifier: GPL-2.0-only`
header. If you have ever navigated the Linux source, you already know where to
look. Chapter 3 gives the full tour.

### Freestanding and self-contained

The kernel is compiled `-ffreestanding` with **no standard library** and no
floating-point/SIMD register usage (`-mno-sse -mno-sse2 -mno-mmx -mno-80387`),
in the kernel code model (`-mcmodel=kernel`) with the red zone disabled. It
provides its *own* everything: `kprintf` instead of `printf`, `kmalloc` instead
of `malloc`, its own string routines, and a custom linker script. It builds with
the host's ordinary `cc` and `ld` — no special cross-compiler toolchain
required. Chapter 2 walks through exactly what that build looks like.

### A higher-half kernel with a direct map

The kernel is linked to live in the top of the canonical address space
(`0xFFFFFFFF80000000`), leaving the entire lower half for user programs. On top
of that, Limine sets up a **Higher-Half Direct Map (HHDM)**: a linear window,
based at `0xFFFF800000000000`, through which the kernel can reach any physical
address by simple arithmetic. This is why translating between physical and
virtual addresses in kernel space is just an add or subtract, not a page-table
walk. Chapter 9 develops the full memory model.

### Initialization *is* a checklist

There is no hidden init-call machinery. `kmain()` in `init/main.c` is a flat,
commented sequence of `*_init()` calls, and the **order of that list is the
dependency graph of the kernel**. You cannot set up the heap before paging, or
take interrupts before the IDT exists, so the list is arranged so each step's
prerequisites are already done. Reading `kmain()` top to bottom is the fastest
way to understand how the system assembles itself.

## 3. The shape of a boot

The control-flow spine of SeedOS is short enough to state in one line:

```
UEFI firmware → Limine → _start (arch/x86/boot/boot.S) → kmain (init/main.c) → kshell_run()
```

Firmware loads Limine; Limine loads the kernel, sets up the framebuffer, the
HHDM, the memory map, and long mode, then jumps to `_start`. The assembly stub
validates the Limine protocol revision, installs a 16 KiB stack, and calls
`kmain()`. From there, `kmain()` brings the machine up in dependency order:

```c
/* init/main.c — condensed; the real list is the authority */
console_init(fb); terminal_init();   /* 1. output: framebuffer + serial   */
gdt_init(); percpu_init(); fpu_init();
syscall_init(); idt_install();       /* 2. CPU tables & traps             */
pmm_init(...); vmm_init(...);        /* 3. physical + virtual memory      */
kheap_init(); page_init(...);        /*    heap + COW page refcounts      */
acpi_init(); apic_init(); ioapic_init();
keyboard_init(); serial_irq_init();  /* 4. hardware discovery & devices   */
cpu_enable_interrupts();             /* 5. now it is safe to take IRQs    */
sysinfo_init(); kthread_init();      /* 6. services: threads, VFS, TTY,   */
vfs_init(); tty_init(); process_init();/*    processes …                  */
/* … mount the ext2 initrd, then: */
kshell_init(); kshell_run();         /* 7. hand control to the shell      */
```

Each call is the subject of a later chapter — Chapter 4 covers the boot stub and
hand-off, Chapter 6 the IDT, Chapters 8–10 the memory stack, Chapter 7 the
APIC/ACPI hardware, and so on. The point here is the *shape*: output first so we
can see progress, then CPU structures, then memory, then hardware, then — only
once everything is in place — interrupts are enabled and the higher-level
services and shell start.

## 4. What SeedOS is *not* (yet)

It is just as important to know the edges. As of this writing SeedOS has **no**:

- multi-CPU / SMP support (it runs on a single core),
- writable filesystem or real disk I/O (the ext2 initrd is read in memory),
- pipes or shell I/O redirection,
- signal handling,
- network stack,
- second architecture (x86-64 only).

These are scoping decisions, not accidents — they keep the system readable.
Chapter 28 ("Toward BusyBox") and Appendix D ("Known Issues & Future Work")
track where these gaps are headed.

## 5. How to read this book

The book is a hybrid: each **Part** tells a story you can read straight through,
while each **chapter** mixes explanation with reference detail and links into the
real source tree.

- **New to the codebase?** Read Parts I–II in order — this chapter, then the
  build (Chapter 2), the source map (Chapter 3), and the boot/architecture
  Part.
- **Looking for one subsystem?** Jump straight to its chapter; each opens with
  the source files it covers.
- **Conventions.** Status markers (✅ drafted · 🚧 outline · ⬜ not started)
  in the [Table of Contents](https://github.com/wiktorjl/seedos/blob/master/docs/book/SUMMARY.md)
  show how complete each chapter is. File references look like
  `init/main.c:66` and are clickable in an editor. Code excerpts are
  illustrative; **the cited source file is always authoritative**.

## Reference & cross-links

- **Next:** [Chapter 2 — Building & Running SeedOS](02-building-and-running.md)
  gets a kernel onto your screen.
- **The source map:** [Chapter 3 — A Tour of the Source Tree](03-source-tree.md).
- **The boot chain in depth:** [Chapter 4 — The Boot Process](04-boot-process.md).
- **The memory model behind the higher-half/HHDM design:**
  [Chapter 9 — Virtual Memory & 4-Level Paging](09-virtual-memory.md).
