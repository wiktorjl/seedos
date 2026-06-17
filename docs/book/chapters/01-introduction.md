# Chapter 1 — Introduction & Design Philosophy

> Part I — Foundations · Status: ✅ drafted

> **Reference notes:** [root `README.md`](https://github.com/wiktorjl/seedos/blob/master/README.md)

## What this chapter covers

This is the on-ramp. If you have written plenty of software but never built an
operating system — if "the OS" has always been the thing your code runs *on top
of*, never something you'd write yourself — then start here. We begin from the
absolute basics: what an operating system even *is*, what it means to build one
"from scratch," and why doing so means stepping outside the comfortable world of
Python (or any high-level language). Then we'll lay out what SeedOS is, the five
design decisions that shape the whole codebase, and how to read the rest of the
book.

> 🧭 **How this book teaches.** Two kinds of callouts recur throughout:
> - 🐍 **From Python** — bridges a new idea to something you already know as an
>   application developer.
> - 🧠 **First principles** — defines a low-level term from the ground up the
>   first time it appears.
>
> You need **no** prior OS, C, or assembly knowledge. Every concept is
> introduced before it is used. When a chapter shows a few lines of C or
> assembly, the surrounding prose explains what each line does.

## 1. What *is* an operating system?

When you run `python app.py`, a remarkable stack of software springs into
action, and almost all of it is invisible. Let's make it visible.

Your `app.py` is executed by the **Python interpreter** — itself a program
(`python`, written in C). That interpreter does not talk to the hardware
directly. When your code opens a file or allocates a list, the interpreter asks
the **operating system** to do it. The operating system is the master program
that:

- talks to the hardware (CPU, memory, disk, keyboard, screen),
- shares those resources among all the running programs, and
- gives every program the *illusion* that it has the whole machine to itself.

Beneath the OS is the bare **hardware**: a CPU that executes instructions and
memory that holds bytes. That is genuinely all there is down there. There is no
`print`, no `list`, no `open` — those are conveniences that software builds.

```
┌──────────────────────────────┐
│  your app.py                 │   ← what you normally write
├──────────────────────────────┤
│  Python interpreter (C)      │
├──────────────────────────────┤
│  Operating system / kernel   │   ← *this* is what SeedOS is
├──────────────────────────────┤
│  Hardware (CPU + memory)     │   ← raw silicon
└──────────────────────────────┘
```

**SeedOS is that second-from-bottom layer.** Building it means writing the
program that everything else runs on top of.

> 🐍 **From Python.** Memory is automatic (the garbage collector frees it for
> you), files are a built-in `open()`, and launching another program is
> `subprocess.run()`. Every one of those is a *service the OS provides*. In this
> book we are the ones providing them — we will write the memory allocator, the
> file reader, and the code that starts programs.

## 2. "From scratch" means nothing is there yet

When SeedOS first runs, there is no Python, no operating system beneath it, and
no standard library. The machine is an empty room: a CPU and some RAM. If we
want to show a character on screen, we must write the pixels ourselves. If we
want memory to hand out, we must first discover how much RAM exists and then
track every page of it ourselves. That is what "bare metal" and "from scratch"
mean — there is no floor beneath us to stand on, so we pour our own.

> 🧠 **First principles: the kernel.** The *kernel* is the core of an operating
> system: the part that runs with full control of the hardware and stays
> resident in memory the entire time the machine is on. It manages memory,
> decides which program runs next, and responds to hardware events. SeedOS is
> essentially a kernel plus a handful of user programs. Throughout the book,
> "the kernel" means SeedOS itself.

> 🧠 **First principles: freestanding.** Ordinary C programs are *hosted*: the
> compiler assumes a standard library (`printf`, `malloc`, …) and an OS
> underneath. A *freestanding* program assumes none of that. You cannot
> `#include <stdio.h>` and call `printf`, because `printf` lives in a library
> that itself needs an OS to actually put characters on a screen. SeedOS is
> freestanding, so it ships its *own* everything — its own `kprintf`, its own
> `kmalloc`.

> 🐍 **From Python.** Picture Python stripped of its standard library *and* its
> built-ins — no `print`, no `len`, no `dict`, not even an `import` system — and
> you must define each one before you can use it. That is what writing
> freestanding C for a kernel feels like.

## 3. Why C and assembly, and not Python

You could not write SeedOS in Python, and understanding exactly why reveals what
an OS actually has to do.

- **Python needs a runtime that needs an OS.** The interpreter is a program that
  relies on an operating system to run. You cannot run Python before an OS
  exists — and we are building the OS.
- **An OS must command exact memory addresses and specific CPU instructions.**
  Setting up the hardware that translates memory addresses, or talking to a
  device, means writing particular values to particular addresses and running
  particular CPU instructions. Python deliberately hides addresses from you
  (a virtue for application code). C lets you treat a number as a memory address
  and read or write it. **Assembly** lets you issue the handful of CPU
  instructions that even C cannot express.
- **No hidden allocations, no garbage collector.** In a kernel you must know
  exactly when and where memory is touched. A surprise allocation inside an
  interrupt handler could wedge the whole machine.

> 🧠 **First principles: machine code & assembly.** A CPU only understands
> *machine code* — raw numbers that encode instructions like "add these two
> registers." *Assembly* is a thin, human-readable spelling of those
> instructions (`mov`, `add`, `call`). A C *compiler* translates C source into
> machine code directly. Python, by contrast, is compiled to *bytecode* that the
> interpreter executes — two layers above the CPU. SeedOS lives at the bottom:
> C compiled straight to machine code, with assembly wherever we need precise
> control.

> 🧠 **First principles: registers.** The CPU's fastest storage is a small,
> fixed set of named 64-bit slots called *registers* (on x86-64: `rax`, `rsp`,
> `rbp`, and a dozen-odd others). Think of them as the CPU's working variables —
> but unlike Python names, there are only a few, and some have dedicated jobs
> (for instance `rsp` always points at the top of the *stack*, the scratch area
> for function calls). We meet them properly in Chapter 4.

## 4. Two worlds: kernel mode and user mode

A modern CPU enforces *trust levels* in hardware, and this single idea underlies
the entire second half of the book.

> 🧠 **First principles: privilege rings.** The CPU runs code at one of several
> privilege levels. Code in *kernel mode* (called **ring 0**) may do anything:
> read or write any memory, talk to any device, execute any instruction. Code in
> *user mode* (**ring 3**) is sandboxed — it cannot touch hardware or another
> program's memory; if it tries, the CPU stops it and traps into the kernel. The
> kernel runs in ring 0 and polices the boundary; ordinary programs run in
> ring 3.

> 🐍 **From Python.** This is *why* a buggy Python script can't crash the whole
> machine or read another process's memory: it runs in user mode, sandboxed by
> the OS with the CPU's help. SeedOS is the thing on the *other* side of that
> wall. Part V (Chapters 17–22) is where we build the wall itself — user mode
> and the system-call gate through it.

## 5. What SeedOS is

With that vocabulary, here is the concrete system. SeedOS is a 64-bit x86
operating system written from scratch in C and a little assembly. It is small
enough to read end to end, yet complete enough to be real: it boots on modern
firmware, brings up its own memory management and interrupt handling, runs
multiple threads of execution, and lands you at a shell that can launch standalone
programs such as `ls`, `cat`, `grep`, and `sort`.

A booted SeedOS today provides — and **every term below is the subject of a later
chapter**, so don't worry if some are unfamiliar yet:

- **Boot & hardware** — starts via UEFI firmware and the Limine bootloader
  (Ch 4); runs as a *higher-half* kernel with a direct map of physical memory
  (Ch 9); uses modern APIC interrupt hardware instead of the legacy PIC (Ch 7);
  reads ACPI tables, and drives a PS/2 keyboard, a serial port, and a
  framebuffer console (Ch 7, 11, 15).
- **Memory** — a physical page allocator (Ch 8), 4-level paging with a private
  address space per program (Ch 9), a `kmalloc`/`kfree` heap (Ch 10), and
  copy-on-write memory sharing (Ch 20).
- **Processes** — a preemptive scheduler (Ch 13), `fork()`/`waitpid()`/`spawn()`
  (Ch 19), a loader for ELF programs (Ch 21), and per-process open files and
  working directory (Ch 25).
- **System calls** — 22 of them, the controlled doorway from user programs into
  the kernel (Ch 18).
- **Filesystem** — a VFS layer over an ext2 RAM disk baked into the boot image
  (Ch 23–24).
- **Userspace** — a small C library, an interactive shell, and ~20 programs
  (Ch 26–27).

The authoritative, always-current list lives in the
[`README.md`](https://github.com/wiktorjl/seedos/blob/master/README.md). The
rest of this book explains how each piece works.

## 6. Five design principles

Almost everything in SeedOS follows from five choices. Hold them in mind and the
code stops looking arbitrary.

### Modern hardware, not legacy

SeedOS targets a machine that already looks like 2020s hardware and ignores
decades of backward-compatibility scaffolding.

> 🧠 **First principles: the jargon in one place.**
> - **UEFI** is the modern firmware that runs the instant a PC powers on and
>   knows how to load an operating system (the successor to the old "BIOS").
> - **Long mode** is the CPU's full 64-bit operating mode. Older CPUs began in a
>   primitive 16-bit mode and had to climb up; SeedOS is handed a CPU already in
>   long mode.
> - **APIC** (Local APIC + I/O APIC) is the modern hardware that delivers
>   *interrupts* — a device's way of getting the CPU's attention (Ch 6–7). It
>   replaces the 1980s-era **8259 PIC**, which SeedOS never touches.

The payoff is a much shorter path to a working system; the price is that SeedOS
won't boot on legacy BIOS-only machines, which it gladly pays.

### Familiar structure

The tree follows **Linux kernel conventions** — `arch/` for CPU-specific code,
`kernel/` for the core, `mm/` for memory, `drivers/` for devices, `fs/` for
filesystems. Every file carries a `GPL-2.0-only` license header. Chapter 3 is the
full tour.

> 🐍 **From Python.** Think of these as the top-level packages of a large
> project: a predictable layout so you always know which "module" a feature
> lives in.

### Freestanding and self-contained

The kernel is compiled with no standard library and brings its own `kprintf`,
`kmalloc`, string routines, and a custom layout script for the final binary. It
even builds with your system's ordinary compiler rather than a special toolchain
(Chapter 2). This is the "empty room" principle from §2, turned into a build
setting.

### A higher-half kernel with a direct map

> 🧠 **First principles: virtual memory (preview).** Programs don't use real
> ("physical") memory addresses directly. The CPU translates each program's
> *virtual* addresses to physical ones through tables the kernel controls
> (Chapter 9). This lets every program have its own private view of memory.

SeedOS places its own code in the *top* of that virtual address space (the
"higher half"), leaving the entire lower half for user programs. It also keeps a
**direct map** — a straight, one-to-one window onto all physical RAM — so that
kernel code can reach any physical byte by simple arithmetic instead of a
table walk. Don't worry about the mechanics yet; just know the kernel lives "up
high" and can always see all of memory.

### Initialization *is* a checklist

There is no hidden start-up machinery. The kernel's entry function, `kmain()` in
`init/main.c`, is a flat, commented sequence of `*_init()` calls, and **the order
of that list is the dependency graph of the whole kernel**. You cannot set up the
memory allocator before paging works, or accept interrupts before there's a table
to route them to — so the list is arranged so each step's prerequisites are
already done. Reading `kmain()` top to bottom is the fastest way to see how the
system assembles itself.

## 7. The shape of a boot

Here is the entire control-flow spine of SeedOS, start to prompt, in one line:

```
UEFI firmware → Limine → _start (arch/x86/boot/boot.S) → kmain (init/main.c) → kshell_run()
```

Firmware loads the **Limine** bootloader; Limine loads the kernel and jumps to
`_start`, a tiny assembly stub; `_start` sets up a stack and calls `kmain()`,
which brings the machine up in dependency order:

```c
/* init/main.c — condensed; the real list is the authority */
console_init(fb); terminal_init();   /* 1. output: so we can see progress     */
gdt_init(); percpu_init(); fpu_init();
syscall_init(); idt_install();       /* 2. CPU tables & interrupt handling     */
pmm_init(...); vmm_init(...);        /* 3. physical + virtual memory           */
kheap_init(); page_init(...);        /*    the heap, and bookkeeping for memory */
acpi_init(); apic_init(); ioapic_init();
keyboard_init(); serial_irq_init();  /* 4. discover and start the hardware      */
cpu_enable_interrupts();             /* 5. only now is it safe to take an IRQ   */
sysinfo_init(); kthread_init();      /* 6. higher-level services: threads,      */
vfs_init(); tty_init(); process_init();/*    filesystem, processes …            */
/* … mount the ext2 RAM disk, then: */
kshell_init(); kshell_run();         /* 7. hand control to the shell            */
```

Each call is a later chapter. The point right now is the *shape*: output first so
we can watch it boot, then the CPU's own tables, then memory, then hardware, and
only once everything is in place are interrupts switched on and the shell
started. Reading this list, you are reading the table of contents of Parts II–V.

## 8. What SeedOS is *not* (yet)

Knowing the edges matters as much as knowing the features. As of this writing
SeedOS has **no**:

- multi-CPU support (it runs on a single core),
- writable filesystem or real disk I/O (the RAM disk is read in memory),
- pipes or shell I/O redirection,
- signal handling,
- network stack,
- second CPU architecture (x86-64 only).

These are deliberate scoping choices that keep the system readable. Chapter 28
("Toward BusyBox") and Appendix D ("Known Issues & Future Work") track where the
gaps are headed.

## 9. How to read this book

It's a hybrid: each **Part** is a story you can read straight through, while each
**chapter** mixes narrative with reference detail and links into the real source.

- **New to all of this?** Read Parts I–II in order — this chapter, the build
  (Chapter 2), the source map (Chapter 3), then the boot-and-architecture Part.
  The 🧠 and 🐍 callouts are there precisely for you.
- **Looking for one subsystem?** Jump straight to its chapter; each opens with
  the source files it covers.
- **Conventions.** Status markers (✅ drafted · 🚧 outline · ⬜ not started) in
  the [Table of Contents](https://github.com/wiktorjl/seedos/blob/master/docs/book/SUMMARY.md)
  show how complete each chapter is. File references look like `init/main.c:66`
  and are clickable in an editor. Code excerpts are illustrative; **the cited
  source file is always authoritative**.

## Reference & cross-links

- **Next:** [Chapter 2 — Building & Running SeedOS](02-building-and-running.md)
  turns this source into something that boots on your screen.
- **The source map:** [Chapter 3 — A Tour of the Source Tree](03-source-tree.md).
- **The boot chain in depth:** [Chapter 4 — The Boot Process](04-boot-process.md).
- **The virtual-memory model behind the higher-half/direct-map design:**
  [Chapter 9 — Virtual Memory & 4-Level Paging](09-virtual-memory.md).
