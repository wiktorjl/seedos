# Chapter 18 — Kernel Threads & Context Switching

> Part IV — Processes & Scheduling · Status: 🚧 outline

> **Source files:** `kernel/kthread.c`, `kernel/kthread.h`, `kernel/kthread_switch.S`

## What this chapter covers

Multitasking starts here: multiple **threads** of execution inside the kernel, and
the **context switch** that saves one thread's CPU state and restores another's.
We begin with the simplest model — *cooperative* multitasking, where a thread runs
until it explicitly yields.

## Outline

1. **What a thread is** — an independent stream of execution with its own stack
   and saved registers.
2. **Intuition** — like green threads / coroutines: switching is just swapping
   which set of saved registers is "live."
3. **The context switch** — `kthread_switch.S`: save callee-saved registers and
   the stack pointer, load the next thread's.
4. **Cooperative first** — `kthread_yield()`; a thread runs until it calls it.
   *(Transition to preemption comes in Chapter 19.)*

## Reference & cross-links

- **Previous:** [Chapter 17 — The I/O APIC & PS/2 Keyboard](17-ioapic-keyboard.md).
- **Next:** [Chapter 19 — The Scheduler & Preemption](19-scheduler-preemption.md).
