# The SeedOS Book — Table of Contents

[Introduction](README.md)

A ground-up guide to building a modern x86-64 operating system. The book is a
**build path**: it starts at the firmware that runs the instant you power on,
and works upward — boot, memory, hardware, processes, user space, storage —
explaining each OS concept from first principles before mapping it to real code.

> Status legend: ✅ drafted · 🚧 outline only · ⬜ not started

# Part 0 — Understanding the Boot Environment

1. [UEFI: The Firmware Beneath Everything](chapters/01-uefi-firmware.md) — ✅
2. [Hello, Bare Metal: Your First EFI Application](chapters/02-efi-hello-world.md) — ✅
3. [vmlinuz Is Just a PE Executable](chapters/03-vmlinuz-pe.md) — 🚧
4. [From UEFI to Limine: What We Delegate](chapters/04-uefi-to-limine.md) — 🚧

# Part I — Foundation

5. [Minimal Boot via Limine](chapters/05-limine-boot.md) — ✅
6. [Displaying Text on the Framebuffer](chapters/06-framebuffer.md) — 🚧
7. [The Toolchain: Cross-Compiler, Build, GDB & QEMU](chapters/07-toolchain.md) — ✅
8. [Serial Output for Debugging](chapters/08-serial.md) — 🚧
9. [The Terminal Abstraction & `kprintf`](chapters/09-terminal-kprintf.md) — 🚧
10. [The IDT & Exception Handlers](chapters/10-idt-exceptions.md) — ✅
11. [A Panic Handler with Backtrace](chapters/11-panic-backtrace.md) — ✅

# Part II — Memory Management

12. [The Physical Memory Manager](chapters/12-pmm.md) — 🚧
13. [Virtual Memory & Higher-Half Ownership](chapters/13-vmm.md) — 🚧
14. [The Kernel Heap: `kmalloc`/`kfree`](chapters/14-heap.md) — 🚧

# Part III — Hardware Discovery

15. [ACPI Table Parsing (RSDP, MADT)](chapters/15-acpi.md) — ✅
16. [The Local APIC Timer](chapters/16-lapic-timer.md) — ✅
17. [The I/O APIC & PS/2 Keyboard](chapters/17-ioapic-keyboard.md) — ✅

# Part IV — Processes & Scheduling

18. [Kernel Threads & Context Switching](chapters/18-threads-context-switch.md) — 🚧
19. [The Scheduler & Preemption](chapters/19-scheduler-preemption.md) — 🚧
20. [Synchronization Primitives](chapters/20-synchronization.md) — 🚧

# Part V — User Space

21. [User Mode: Rings, the GDT & the TSS](chapters/21-user-mode.md) — ✅
22. [System Calls: `syscall`/`sysret` & the Linux ABI](chapters/22-syscalls.md) — 🚧
23. [The VFS & File Descriptors](chapters/23-vfs-fds.md) — 🚧
24. [Early Userspace: Loading Programs from a Module](chapters/24-early-userspace.md) — 🚧
25. [The ELF Loader](chapters/25-elf-loader.md) — 🚧
26. [`fork` & `exec`](chapters/26-fork-exec.md) — 🚧
27. [Signals](chapters/27-signals.md) — ⬜

# Part VI — Storage & Filesystem

28. [PCI Enumeration](chapters/28-pci.md) — ⬜
29. [The virtio-blk Driver](chapters/29-virtio-blk.md) — ⬜
30. [The ext2 Filesystem (Read-Only)](chapters/30-ext2.md) — 🚧

## Appendices

- [A. Memory Map Reference](appendices/a-memory-map.md) — ⬜
- [B. Syscall Table](appendices/b-syscall-table.md) — ⬜
- [E. Glossary](appendices/e-glossary.md) — ⬜
