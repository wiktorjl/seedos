# Kernel Context Switching and Blocking I/O

This document explains blocking I/O, what SeedOS currently supports, and what's needed for full implementation.

## What is Blocking I/O?

**Blocking I/O** means a process is completely removed from the scheduler when waiting for I/O, consuming zero CPU until data arrives.

### Current Implementation (Polling with Yield)

```
Process: read() → no data → hlt → timer fires → wake → check → no data → hlt → ...
         Still in ready queue, still gets scheduled, just halts each time
```

The process remains in the scheduler's ready queue. Each time it's scheduled, it checks for data, finds none, and halts. This wastes scheduler cycles.

### True Blocking I/O

```
Process: read() → no data → remove from scheduler → process sleeps (0 CPU)
         ...time passes, other processes run...
Keyboard IRQ: data arrives → wake process → add to scheduler
Process: continues from read(), returns data
```

The process is completely removed from scheduling. Zero CPU is consumed while waiting. When data arrives, the interrupt handler wakes the process.

## Components Needed for Blocking I/O

### 1. Wait Queues

Each resource that can block needs a wait queue - a list of processes waiting for that resource.

```c
struct wait_queue {
    struct process *waiters[MAX_WAITERS];
    int count;
};

/* Each blockable resource has a wait queue */
static struct wait_queue keyboard_wq;
static struct wait_queue pipe_wq[MAX_PIPES];
static struct wait_queue disk_wq[MAX_DISKS];
```

### 2. Sleep/Wake Primitives

```c
/* Called by process that needs to wait for a resource */
void sleep_on(struct wait_queue *wq) {
    uint64_t flags = irq_save();

    /* Add ourselves to the wait queue */
    add_to_waitqueue(wq, current_process);

    /* Mark as blocked and remove from scheduler */
    current_process->state = PROC_BLOCKED;
    sched_remove(current_process);

    irq_restore(flags);

    /* Yield to scheduler - switch to another process */
    schedule();

    /* When we return here, we've been woken up */
}

/* Called by interrupt handler or another process to wake waiters */
void wake_up(struct wait_queue *wq) {
    uint64_t flags = irq_save();

    for (int i = 0; i < wq->count; i++) {
        struct process *p = wq->waiters[i];
        p->state = PROC_READY;
        sched_add(p);
    }
    wq->count = 0;  /* Clear the wait queue */

    irq_restore(flags);
}
```

### 3. Scheduler That Can Yield Mid-Syscall

This is the critical piece. Currently in SeedOS:
- Syscalls run to completion
- Context switches only happen on timer interrupts
- We only save/restore user-mode context

For blocking I/O, syscalls must be able to yield partway through:

```c
ssize_t sys_read(int fd, void *buf, size_t count) {
    struct file *f = get_file(fd);

    while (buffer_empty(f)) {
        /* This must yield to scheduler and resume here later */
        sleep_on(&f->wait_queue);

        /* When we return here, we were woken up */
        /* Check again in case of spurious wake */
    }

    /* Data available - copy to user buffer */
    return copy_to_user(buf, f->buffer, count);
}
```

The `sleep_on()` call must:
1. Save where we are in sys_read (local variables, return addresses)
2. Switch to another process entirely
3. Later, when woken, restore state and continue from this exact point

### 4. Interrupt-Safe Wake from IRQ Context

Interrupt handlers run in a special context with interrupts disabled. Waking processes from this context requires care:

```c
void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);
    char c = translate_scancode(scancode);

    if (c != 0) {
        buffer_add(c);

        /* Option A: Direct wake (needs interrupt-safe scheduler) */
        wake_up(&keyboard_wq);

        /* Option B: Deferred wake (safer, more complex) */
        mark_pending_wake(&keyboard_wq);
        /* Scheduler processes pending wakes on next schedule() call */
    }
}
```

Option A is simpler but requires all scheduler operations to be interrupt-safe (disable interrupts around critical sections).

Option B defers the actual wake-up to a safe context, avoiding races but adding complexity.

### 5. Kernel Context Switching (The Hard Part)

This is what SeedOS is missing for true blocking I/O.

#### Current State: User Context Only

SeedOS currently saves/restores only user-mode context:

```c
struct process {
    /* User-mode context (saved on syscall/interrupt entry) */
    uint64_t saved_rip;      /* User instruction pointer */
    uint64_t saved_rsp;      /* User stack pointer */
    uint64_t saved_rflags;
    uint64_t saved_rax, saved_rbx, ...;
};
```

When a timer interrupt fires:
1. CPU pushes user RIP, RSP, RFLAGS to kernel stack
2. We save user registers to process struct
3. Switch to new process
4. Restore new process's user registers
5. IRET back to user mode

#### What's Needed: Kernel Context Too

For blocking mid-syscall, we need to save kernel-mode context:

```c
struct process {
    /* User-mode context */
    uint64_t saved_user_rip;
    uint64_t saved_user_rsp;
    ...

    /* Kernel-mode context (NEW) */
    uint64_t saved_kernel_rsp;  /* Where we were in the syscall */
    /* The kernel stack itself contains:
     * - Local variables of sys_read(), sleep_on(), etc.
     * - Return addresses through the call chain
     * - Saved registers from function calls
     */
};
```

#### How Linux Does It

Each process has its own kernel stack (typically 8KB or 16KB). When a process blocks:

```
Process A's kernel stack:          Process B's kernel stack:
┌─────────────────────┐            ┌─────────────────────┐
│ sys_read() locals   │            │ sys_write() locals  │
│ return addr         │            │ return addr         │
├─────────────────────┤            ├─────────────────────┤
│ vfs_read() locals   │            │ vfs_write() locals  │
│ return addr         │            │ return addr         │
├─────────────────────┤            ├─────────────────────┤
│ sleep_on() locals   │            │ (not blocked)       │
│ return addr         │            │                     │
├─────────────────────┤            │                     │
│ schedule()          │◄─ RSP      │                     │◄─ RSP
└─────────────────────┘            └─────────────────────┘
```

Context switch just swaps kernel stack pointers:
1. Save current RSP to process A's struct
2. Load process B's RSP from its struct
3. Return - now executing on B's stack, in B's syscall

When process A is woken and scheduled again:
1. Load A's kernel RSP
2. Return from schedule()
3. Return from sleep_on()
4. Continue in sys_read() with all local variables intact

#### Implementation Requirements

To add kernel context switching to SeedOS:

1. **Per-process kernel stacks**: Allocate a kernel stack for each process (currently we use a single shared kernel stack)

2. **Save kernel RSP on block**: When a process blocks, save its kernel stack pointer

3. **Switch stacks in scheduler**: The context switch must swap kernel stacks, not just save/restore registers

4. **Careful with TSS**: The TSS RSP0 field (kernel stack for ring transitions) must be updated on each context switch

```c
void context_switch(struct process *prev, struct process *next) {
    /* Save prev's kernel stack pointer */
    prev->kernel_rsp = get_current_rsp();

    /* Update TSS for ring 3 → ring 0 transitions */
    tss_set_rsp0(next->kernel_stack_top);

    /* Switch to next's kernel stack */
    set_current_rsp(next->kernel_rsp);

    /* Switch address space */
    vmm_switch_address_space(next->pml4);

    /* Return - now on next's stack, will return through next's call chain */
}
```

## SeedOS Current Status

| Component | Status | Notes |
|-----------|--------|-------|
| Wait queues | Easy to add | Just data structures |
| Sleep/wake primitives | Partial | Have `sched_block_on_pid`, need generalization |
| Interrupt-safe scheduler | ✅ Done | Added `irq_save`/`irq_restore` |
| Per-process kernel stacks | ❌ Missing | Currently use shared kernel stack |
| Kernel context save/restore | ❌ Missing | Only save user context |
| Yield mid-syscall | ❌ Missing | Requires kernel stacks |
| IRQ-safe wake | ✅ Done | Scheduler ops are interrupt-safe |

## Incremental Implementation Path

### Phase 1: Generalized Wait Queues (Easy)
- Add `struct wait_queue`
- Generalize `sched_block_on_pid` to `sleep_on(wait_queue)`
- Add `wake_up(wait_queue)`
- Still uses polling/hlt, but cleaner API

### Phase 2: Per-Process Kernel Stacks (Medium)
- Allocate 4KB-8KB kernel stack per process
- Update TSS RSP0 on context switch
- Processes still can't block mid-syscall yet

### Phase 3: Kernel Context Switching (Hard)
- Save/restore kernel RSP in context switch
- Modify `sleep_on()` to actually yield via `schedule()`
- Test with simple blocking read

### Phase 4: Full Blocking I/O (Integration)
- Keyboard blocking with wait queue
- Pipe read/write blocking
- Sleep syscall (block on timer)

## References

- Linux kernel `wait_queue_head_t` and `wait_event()` macros
- xv6 `sleep()` and `wakeup()` implementation
- OSDev wiki: Context Switching
