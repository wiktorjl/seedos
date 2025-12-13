# Preemptive Scheduler Implementation Plan

## Overview

Implement a preemptive round-robin scheduler that allows multiple processes to run concurrently, with the timer interrupt forcing context switches. This solves the immediate problem (`run loop` hangs forever) and enables true multitasking.

## Current State Analysis

### What Exists
- **Single process**: Static `struct process current_process` in process.c
- **Blocking execution**: `context_switch_to_user()` doesn't return until `sys_exit()`
- **Timer**: PIT fires at 100Hz (10ms), but only increments a counter
- **Context save**: ISR saves all GPRs on kernel stack, but doesn't support switching

### The Problem
1. `run loop` hangs forever - no way to interrupt
2. Only one process at a time
3. No time-slicing between processes

---

## Architecture Design

### Process States
```
UNUSED  ──create──▶  READY  ◀──────────────────┐
                       │                        │
                    schedule                    │
                       ▼                        │
                   RUNNING  ──timer/yield──▶  READY
                       │
                    sys_exit
                       ▼
                    ZOMBIE  ──wait/reap──▶  UNUSED
```

### Context Switch Mechanism

When timer fires during userspace execution:

```
┌─────────────────────────────────────────────────────────────────┐
│  Process A's Kernel Stack (after timer interrupt)               │
├─────────────────────────────────────────────────────────────────┤
│  SS, RSP, RFLAGS, CS, RIP    ← CPU pushed (user state)          │
│  error_code, int_no          ← ISR stub pushed                  │
│  RAX..R15, RBP               ← isr_common pushed (15 GPRs)      │
│  ─────────────────────────── ← RSP here when schedule() called  │
│  (schedule's local vars)                                        │
│  return address              ← where to resume in schedule()    │
│  RBP, RBX, R12-R15           ← switch_context() saves these     │
└─────────────────────────────────────────────────────────────────┘
         │
         │  switch_context(A->saved_rsp, B->saved_rsp)
         ▼
┌─────────────────────────────────────────────────────────────────┐
│  Process B's Kernel Stack (previously saved)                    │
├─────────────────────────────────────────────────────────────────┤
│  SS, RSP, RFLAGS, CS, RIP    ← B's saved user state             │
│  error_code, int_no                                             │
│  RAX..R15, RBP               ← B's saved GPRs                   │
│  ───────────────────────────                                    │
│  return address              ← returns into B's schedule() call │
│  RBP, RBX, R12-R15           ← switch_context() restores these  │
└─────────────────────────────────────────────────────────────────┘
```

**Key insight**: The context switch happens in C code (schedule()), not in the ISR. The ISR returns normally to whatever process is current when it returns.

---

## Implementation Phases

### Phase 1: Process Table and States

**Files**: `src/process.h`, `src/process.c`

1. Add process state enum:
```c
enum process_state {
    PROC_UNUSED = 0,    // Slot is free
    PROC_READY,         // Can be scheduled
    PROC_RUNNING,       // Currently executing
    PROC_ZOMBIE         // Exited, waiting for cleanup
};
```

2. Extend process struct:
```c
struct process {
    enum process_state state;
    uint64_t pml4;
    uint64_t code_page;
    uint64_t stack_page;
    uint64_t entry;
    uint64_t brk;
    uint64_t stack;           // User stack pointer
    uint64_t kernel_stack;    // Physical addr of kernel stack page
    uint64_t saved_rsp;       // Saved kernel RSP when not running
    int exit_code;
    int pid;
};
```

3. Change to process table:
```c
#define MAX_PROCESSES 16
static struct process processes[MAX_PROCESSES];
static struct process *current = NULL;
```

4. Update `process_create()`:
   - Find free slot (state == PROC_UNUSED)
   - Allocate kernel stack page
   - Set state to PROC_READY

5. Add accessor functions:
```c
struct process *process_current(void);
struct process *process_by_pid(int pid);
```

### Phase 2: Per-Process Kernel Stacks

**Files**: `src/process.c`, `src/gdt.c`

Each process needs its own kernel stack because:
- Timer interrupt uses TSS.RSP0 to find kernel stack
- If we switch processes, each needs its own stack for saved state

1. In `process_create()`:
```c
p->kernel_stack = pmm_alloc();  // 4KB kernel stack
```

2. In `process_destroy()`:
```c
pmm_free(p->kernel_stack);
```

3. The scheduler will call `tss_set_kernel_stack()` when switching.

### Phase 3: Initial Process Context Setup

**Files**: `src/process.c`

New processes have never run, so we must set up a "fake" interrupt frame on their kernel stack. When the scheduler switches to them, it looks like they were interrupted at their entry point.

```c
static void setup_initial_context(struct process *p) {
    uint64_t *kstack = (uint64_t *)phys_to_virt(p->kernel_stack);
    uint64_t *sp = kstack + (PAGE_SIZE / sizeof(uint64_t));  // Top of stack

    // Interrupt frame (what CPU pushes)
    *--sp = GDT_USER_DATA | 3;      // SS
    *--sp = p->stack;               // RSP (user stack)
    *--sp = 0x202;                  // RFLAGS (IF=1)
    *--sp = GDT_USER_CODE | 3;      // CS
    *--sp = p->entry;               // RIP (entry point)

    // ISR stub additions
    *--sp = 0;                      // error_code
    *--sp = 0;                      // int_no (doesn't matter)

    // GPRs (isr_common pushes these: rax,rbx,rcx,rdx,rsi,rdi,rbp,r8-r15)
    for (int i = 0; i < 15; i++) {
        *--sp = 0;                  // All GPRs zeroed
    }

    // Callee-saved registers for switch_context (rbp,rbx,r12-r15)
    // Plus return address
    *--sp = (uint64_t)schedule_tail;  // "return" to schedule_tail
    *--sp = 0;  // r15
    *--sp = 0;  // r14
    *--sp = 0;  // r13
    *--sp = 0;  // r12
    *--sp = 0;  // rbx
    *--sp = 0;  // rbp

    p->saved_rsp = (uint64_t)sp;
}
```

The `schedule_tail` function handles first-time process startup:
```c
void schedule_tail(void) {
    // First time this process runs - return through normal ISR path
    // Stack has the fake interrupt frame we set up
}
```

### Phase 4: Context Switch Assembly

**Files**: `src/context_switch.S` (add to existing)

```asm
/*
 * switch_context(uint64_t *old_rsp_ptr, uint64_t new_rsp)
 *
 * Save current kernel context, switch to new kernel stack.
 * Called from schedule() in C.
 */
.global switch_context
switch_context:
    // Save callee-saved registers on current stack
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    // Save current RSP to *old_rsp_ptr
    movq %rsp, (%rdi)

    // Switch to new stack
    movq %rsi, %rsp

    // Restore callee-saved registers from new stack
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    // Return (to wherever new process was)
    ret
```

### Phase 5: Scheduler Core

**Files**: `src/scheduler.h` (new), `src/scheduler.c` (new)

```c
// scheduler.h
void scheduler_init(void);
void schedule(void);           // Pick next process and switch
void sched_yield(void);        // Voluntarily give up CPU
void sched_tick(void);         // Called from timer interrupt
struct process *sched_current(void);
```

```c
// scheduler.c
static struct process *current = NULL;
static int time_slice = 0;
#define QUANTUM 10  // 10 ticks = 100ms

void schedule(void) {
    struct process *prev = current;
    struct process *next = pick_next_ready();

    if (next == NULL) {
        // No ready process - idle or return to shell
        return;
    }

    if (next == prev) {
        // Same process, just reset quantum
        time_slice = QUANTUM;
        return;
    }

    // Mark states
    if (prev && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
    }
    next->state = PROC_RUNNING;
    current = next;

    // Update TSS for new process's kernel stack
    tss_set_kernel_stack(next->kernel_stack + PAGE_SIZE);

    // Switch address space
    vmm_switch_address_space(next->pml4);

    // Switch kernel stacks (and thus execution context)
    if (prev) {
        switch_context(&prev->saved_rsp, next->saved_rsp);
    } else {
        // First process - just load its stack
        asm volatile("movq %0, %%rsp" : : "r"(next->saved_rsp));
        // Continue to return through ISR path
    }

    time_slice = QUANTUM;
}

void sched_tick(void) {
    if (current == NULL) return;

    if (--time_slice <= 0) {
        schedule();
    }
}

static struct process *pick_next_ready(void) {
    // Round-robin: start after current, wrap around
    int start = current ? current->pid : 0;

    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int idx = (start + i) % MAX_PROCESSES;
        if (processes[idx].state == PROC_READY) {
            return &processes[idx];
        }
    }
    return NULL;
}
```

### Phase 6: Timer Integration

**Files**: `src/pit.c`, `src/idt.c`

In `pit_handler()`:
```c
void pit_handler(void) {
    ticks++;
    sched_tick();  // May trigger context switch
}
```

**Important**: The context switch happens *inside* the interrupt handler. When `schedule()` calls `switch_context()`, we switch to a different kernel stack. When that stack's `switch_context()` returns, it returns into *that* process's interrupt handler call chain, which then does iretq to return to *that* process's userspace.

### Phase 7: Process Lifecycle Updates

**Files**: `src/process.c`, `src/syscall.c`, `src/programs.c`

1. **process_run() changes**:
```c
int process_run(struct process *p) {
    // Set up initial context
    setup_initial_context(p);

    // Mark ready
    p->state = PROC_READY;

    // Enter scheduler loop until this process exits
    while (p->state != PROC_ZOMBIE) {
        schedule();
        // After schedule returns, either:
        // - This process ran and yielded/got preempted
        // - Another process ran
        // Keep looping until our process is done
    }

    int code = p->exit_code;
    process_destroy(p);
    return code;
}
```

2. **sys_exit() changes**:
```c
void sys_exit(int code) {
    struct process *p = process_current();
    p->exit_code = code;
    p->state = PROC_ZOMBIE;
    schedule();  // Switch away, never returns
}
```

### Phase 8: ISR Modification for Scheduler

**Files**: `src/isr.S`

The ISR needs to be aware of scheduling. After `interrupt_handler` returns, we might be on a different stack (different process). The iretq will return to that process's saved state.

No changes needed to isr.S - the magic happens because:
1. `interrupt_handler()` calls `pit_handler()` which calls `sched_tick()` which calls `schedule()`
2. `schedule()` may call `switch_context()` which changes RSP
3. When we return from all those calls, we're on a different stack
4. The pops and iretq use that stack's saved values

---

## Files Summary

| File | Action | Changes |
|------|--------|---------|
| `src/process.h` | MODIFY | Add state enum, extend struct, new functions |
| `src/process.c` | MODIFY | Process table, kernel stacks, initial context |
| `src/scheduler.h` | CREATE | Scheduler API |
| `src/scheduler.c` | CREATE | Round-robin scheduler implementation |
| `src/context_switch.S` | MODIFY | Add `switch_context()` |
| `src/pit.c` | MODIFY | Call `sched_tick()` |
| `src/syscall.c` | MODIFY | Update `sys_exit()` |
| `src/programs.c` | MODIFY | Update to work with new process model |
| `src/gdt.c` | MODIFY | Possibly add `tss_get_kernel_stack()` |
| `Makefile` | MODIFY | Add scheduler.c |

---

## Testing Plan

### Test 1: Basic Preemption
```
run loop
```
Should no longer hang. Process gets preempted and shell regains control (or process times out).

### Test 2: Multiple Processes
Modify shell to run multiple programs:
```
run count &
run alpha &
```
Output should interleave (0A1B2C3D...).

### Test 3: Exit Codes
```
run ctest
```
Should still return correct exit code.

### Test 4: Exception Handling
```
run crash
```
Should terminate gracefully, not affect other processes.

### Test 5: Heap Allocation
```
run heap
```
Per-process heap (sbrk) should still work.

---

## Complexity Estimate

| Phase | Lines | Difficulty |
|-------|-------|------------|
| 1. Process table | ~80 | Easy |
| 2. Kernel stacks | ~20 | Easy |
| 3. Initial context | ~50 | Medium |
| 4. switch_context | ~25 | Medium |
| 5. Scheduler | ~100 | Medium |
| 6. Timer integration | ~10 | Easy |
| 7. Lifecycle updates | ~50 | Medium |
| 8. ISR awareness | ~0 | N/A |
| **Total** | **~335** | **Medium** |

---

## Potential Issues & Mitigations

1. **Stack alignment**: x86-64 ABI requires 16-byte alignment at call sites. The fake interrupt frame must maintain this.

2. **First process startup**: The first process has no "previous" context. Need special handling in schedule().

3. **Idle state**: What if no processes are ready? Options:
   - Busy-wait in schedule()
   - Return to shell
   - Create idle process

4. **Keyboard input**: Currently goes to shell. With multiple processes, need to decide who gets input.

5. **Zombie reaping**: Who cleans up zombie processes? For now, process_run() waits and cleans up.

---

## Future Enhancements (Not In Scope)

- Priority-based scheduling
- Sleep/wake mechanism
- Blocking I/O
- Multiple terminals
- Fork/exec
- Signals (SIGKILL for Ctrl+C)
