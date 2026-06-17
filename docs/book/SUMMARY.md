# The SeedOS Book — Table of Contents

A complete, top-to-bottom account of how SeedOS works and how it is built.
Each **Part** is a narrative arc; each **Chapter** mixes explanation with
reference material and points at the real source files.

> Status legend: ✅ drafted · 🚧 outline only · ⬜ not started

---

## Part I — Foundations

1. [Introduction & Design Philosophy](chapters/01-introduction.md) — 🚧
2. [Building & Running SeedOS](chapters/02-building-and-running.md) — 🚧
3. [A Tour of the Source Tree](chapters/03-source-tree.md) — 🚧

## Part II — Boot & Architecture

4. [The Boot Process: Limine to `kmain`](chapters/04-boot-process.md) — 🚧
5. [x86-64 CPU Setup: GDT, Long Mode, FPU](chapters/05-cpu-setup.md) — 🚧
6. [Interrupts & Exceptions: The IDT and ISRs](chapters/06-interrupts.md) — 🚧
7. [Hardware Discovery: ACPI, LAPIC & I/O APIC](chapters/07-acpi-apic.md) — 🚧

## Part III — Memory Management

8. [Physical Memory: The PMM](chapters/08-physical-memory.md) — 🚧
9. [Virtual Memory & 4-Level Paging](chapters/09-virtual-memory.md) — 🚧
10. [The Kernel Heap: `kmalloc`/`kfree`](chapters/10-kernel-heap.md) — 🚧

## Part IV — Kernel Core

11. [Console, Serial & the Terminal Layer](chapters/11-tty.md) — 🚧
12. [Formatted Output & Logging](chapters/12-printf-logging.md) — 🚧
13. [Kernel Threads & the Scheduler](chapters/13-threads-scheduler.md) — 🚧
14. [Synchronization Primitives](chapters/14-synchronization.md) — 🚧
15. [Input: The PS/2 Keyboard](chapters/15-keyboard.md) — 🚧
16. [The Kernel Shell](chapters/16-kshell.md) — 🚧

## Part V — Processes & Userspace

17. [User Mode & Privilege Separation](chapters/17-user-mode.md) — 🚧
18. [The Syscall Interface](chapters/18-syscalls.md) — 🚧
19. [The Process Model](chapters/19-processes.md) — 🚧
20. [`fork()` and Copy-on-Write](chapters/20-fork-cow.md) — 🚧
21. [ELF Loading & `exec`](chapters/21-elf-exec.md) — 🚧
22. [Per-CPU Data](chapters/22-percpu.md) — 🚧

## Part VI — Filesystem

23. [The VFS Layer](chapters/23-vfs.md) — 🚧
24. [ext2 & the RAM Disk](chapters/24-ext2.md) — 🚧
25. [File Descriptors & I/O](chapters/25-file-descriptors.md) — 🚧

## Part VII — Userland

26. [The C Runtime & Userspace ABI](chapters/26-c-runtime.md) — 🚧
27. [Userspace Programs & Tests](chapters/27-userspace-programs.md) — 🚧
28. [Toward BusyBox](chapters/28-toward-busybox.md) — 🚧

## Appendices

- [A. Memory Map Reference](appendices/a-memory-map.md) — ⬜
- [B. Syscall Table](appendices/b-syscall-table.md) — ⬜
- [C. Demos & Utilities (logo, matrix, sysinfo)](appendices/c-demos-utilities.md) — ⬜
- [D. Known Issues & Future Work](appendices/d-known-issues.md) — ⬜
- [E. Glossary](appendices/e-glossary.md) — ⬜
