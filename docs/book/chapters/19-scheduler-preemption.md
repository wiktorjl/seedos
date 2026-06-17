# Chapter 19 — The Scheduler & Preemption

> Part IV — Processes & Scheduling · Status: 🚧 outline

> **Source files:** `kernel/kthread.c` (scheduler), `arch/x86/kernel/apic.c` (timer)

## What this chapter covers

A cooperative system breaks the moment one thread misbehaves. This chapter adds a
**scheduler** and, crucially, the transition to **preemption**: the timer
interrupt from Chapter 16 forces a context switch whether or not the running
thread cooperates.

## Outline

1. **What scheduling is** — choosing which ready thread runs next (round-robin
   here).
2. **Transition: Cooperative → Preemptive** — the key idea: a timer IRQ can
   switch contexts from *inside* the running thread.
3. **Intuition** — the timer tick (Chapter 16) is the OS's `setInterval` that
   yanks the CPU back so no thread can hog it.
4. **In SeedOS** — `kthread_schedule()` called from the timer handler; the
   `CONFIG_KTHREAD_PREEMPTIVE` switch; waking sleepers.

## Reference & cross-links

- **Previous:** [Chapter 18 — Kernel Threads & Context Switching](18-threads-context-switch.md).
- **Next:** [Chapter 20 — Synchronization Primitives](20-synchronization.md).
- **The timer that drives preemption:** [Chapter 16 — The Local APIC Timer](16-lapic-timer.md).
