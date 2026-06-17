# The SeedOS Book

> Build a modern x86-64 operating system from the power button up.

This book teaches operating-system internals **from the ground up** by building
one. It starts at the very first code that runs when a machine powers on, and
works upward — firmware, boot, memory, hardware, processes, user space, storage —
introducing each concept from first principles before mapping it to real,
working code in [SeedOS](https://github.com/wiktorjl/seedos).

It is written for **application developers who have never done OS or low-level
work**. If "the OS" has always been the thing your Python (or JavaScript, or Go)
runs *on top of* — never something you'd write yourself — this is for you. No
prior C, assembly, or OS knowledge is assumed; every term is defined before it's
used.

## What an operating system even is

When you run `python app.py`, an invisible stack is already in motion: your code
runs on the Python interpreter, which runs on the **operating system**, which
runs on the bare **hardware** (a CPU and memory). The OS is the master program
that drives the hardware, shares it among programs, and gives each one the
illusion of having the machine to itself.

```
┌──────────────────────────────┐
│  your app.py                 │   ← what you normally write
├──────────────────────────────┤
│  language runtime (e.g. CPython) │
├──────────────────────────────┤
│  Operating system / kernel   │   ← *this* is what we build
├──────────────────────────────┤
│  Hardware (CPU + memory)     │   ← raw silicon
└──────────────────────────────┘
```

This book builds that second-from-bottom layer. The services you take for
granted — memory allocation, threads, files, launching programs — are things
*we* implement here, by hand, in C and a little assembly.

## How the book teaches

Two recurring callouts carry the ground-up explanation:

- 🐍 **From Python** — bridges a new idea to something you already know.
- 🧠 **First principles** — defines a low-level term the first time it appears.

Technical chapters follow a consistent arc per concept: a brief **textbook**
definition, a programmer's **intuition** for it, a small **example**, then the
mapping to **SeedOS code**.

## The shape of the journey

- **Part 0 — Understanding the Boot Environment.** What UEFI firmware is; write
  and boot your own EFI program; discover that the Linux kernel is just a PE
  executable; decide what to delegate to a bootloader.
- **Part I — Foundation.** Boot via Limine, draw text, set up the toolchain, get
  serial output and `kprintf`, and install interrupt/exception handling.
- **Part II — Memory Management.** Physical pages, virtual memory and paging, and
  a kernel heap (`kmalloc`/`kfree`).
- **Part III — Hardware Discovery.** ACPI, the APIC timer, and the keyboard —
  each first by *polling*, then converted to *interrupts*.
- **Part IV — Processes & Scheduling.** Threads and context switching, the
  scheduler (cooperative → preemptive), and synchronization (busy-wait → sleep).
- **Part V — User Space.** Ring 3, system calls, the VFS, the ELF loader, `fork`
  and `exec`, and signals.
- **Part VI — Storage & Filesystem.** PCI, a virtio-blk driver, and ext2.

The [Table of Contents](https://github.com/wiktorjl/seedos/blob/master/docs/book/SUMMARY.md)
shows every chapter and how complete it is (✅ drafted · 🚧 outline · ⬜ not
started).

## What SeedOS is, concretely

SeedOS is a single-CPU, x86-64-only kernel small enough to read end to end: it
boots via UEFI/Limine, manages its own memory with 4-level paging, schedules
preemptively, exposes ~22 Linux-style system calls, and runs ELF programs (`ls`,
`cat`, `grep`, …) from a shell. It deliberately has **no** SMP, writable disk,
pipes, signals, or network *yet* — the later Parts of this book are the path to
filling those in.

## Conventions

- Code excerpts are illustrative; the cited source file (e.g. `init/main.c:66`)
  is always authoritative, and clickable in an editor.
- Some chapters in Parts 0, V, and VI describe the **planned build path** and go
  beyond what SeedOS implements today; their status markers say so.
