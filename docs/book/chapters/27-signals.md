# Chapter 27 — Signals

> Part V — User Space · Status: ⬜ not started

> **Status:** planned build-path chapter — not yet implemented in SeedOS.

## What this chapter covers

**Signals** are the kernel's way of notifying a process of an event — a fault, a
kill request, a child's death. This chapter (a forward-looking part of the build
path) will add basic signal delivery: `SIGKILL`, `SIGSEGV`, and `SIGCHLD`.

## Outline

1. **What a signal is** — an asynchronous notification delivered to a process;
   the intuition is an interrupt for user space.
2. **Delivery** — pending/blocked masks, running a handler on the user stack,
   and the return trampoline.
3. **The three to start with** — `SIGSEGV` (from a real page fault, Chapter 10),
   `SIGKILL` (unblockable termination), `SIGCHLD` (notify a parent on `wait`).
4. **Planned implementation** — how this will hook into the process model
   (Chapter 26) and the syscall layer (Chapter 22).

## Reference & cross-links

- **Previous:** [Chapter 26 — `fork` & `exec`](26-fork-exec.md).
- **Next:** [Chapter 28 — PCI Enumeration](28-pci.md) begins Part VI.
