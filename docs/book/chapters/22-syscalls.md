# Chapter 22 — System Calls: `syscall`/`sysret` & the Linux ABI

> Part V — User Space · Status: 🚧 outline

> **Source files:** `arch/x86/kernel/syscall.c`, `arch/x86/kernel/syscall_entry.S`, `kernel/syscall_table.h`

## What this chapter covers

A user program can't touch hardware directly (Chapter 21), so it asks the kernel
through **system calls**. This chapter builds the fast `syscall`/`sysret`
mechanism, the entry assembly that switches to the kernel stack, the dispatch
table, and SeedOS's Linux-compatible ABI.

## Outline

1. **What a system call is** — the controlled doorway from ring 3 to ring 0.
2. **Intuition** — a function call across a security boundary; the number selects
   the service, registers carry the arguments.
3. **`syscall`/`sysret`** — the MSRs (`STAR`, `LSTAR`, `SFMASK`), the entry stub,
   the per-CPU kernel-stack swap.
4. **Basic syscalls** — `read`, `write`, `exit`; the dispatch table.
5. **Linux ABI compatibility** — matching Linux's syscall numbers and calling
   convention so standard programs work.

## Reference & cross-links

- **Previous:** [Chapter 21 — User Mode: Rings, the GDT & the TSS](21-user-mode.md).
- **Next:** [Chapter 23 — The VFS & File Descriptors](23-vfs-fds.md).
- **Full list:** [Appendix B — Syscall Table](../appendices/b-syscall-table.md).
