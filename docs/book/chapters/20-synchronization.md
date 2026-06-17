# Chapter 20 — Synchronization Primitives

> Part IV — Processes & Scheduling · Status: 🚧 outline

> **Source files:** `kernel/sync.c`, `kernel/sync.h`

## What this chapter covers

The instant two threads can preempt each other, shared data needs protection. This
chapter builds the locks — spinlocks, mutexes, and condition variables — and walks
the transition from wasteful busy-waiting to efficient sleeping.

## Outline

1. **The race condition** — why preemption (Chapter 19) makes "read-modify-write"
   unsafe.
2. **Intuition** — `threading.Lock`, but we implement it on the CPU's atomic
   instructions.
3. **Transition: Busy-wait → Sleep** — spinlock (spin) → mutex (sleep) →
   condition variable (sleep until signaled).
4. **In SeedOS** — `spinlock_t`, `mutex_t`, `cond_t`, and when to use each.

## Reference & cross-links

- **Previous:** [Chapter 19 — The Scheduler & Preemption](19-scheduler-preemption.md).
- **Next:** [Chapter 21 — User Mode: Rings, the GDT & the TSS](21-user-mode.md) begins Part V.
