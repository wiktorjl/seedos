# Fork Completion Plan

This document outlines the complete implementation plan for making `fork()` fully functional, including proper process scheduling so the child process can actually execute.

## Current State

**What works:**
- `sys_fork()` creates child process structure
- Address space copied with COW (Copy-on-Write)
- Page fault handler resolves COW faults
- File descriptors duplicated with refcount increment
- Child added to process list and parent's children list

**What's missing:**
- Child has no kernel thread to execute on
- Child's user registers not captured from parent
- No kernel stack frame set up for child to "return" from syscall
- No scheduler to run the child
- No way for child to return 0 to userspace

## Architecture Overview

```
Parent calls fork()
       │
       ▼
┌─────────────────────────────────────────────────────────┐
│  SYSCALL ENTRY (syscall_entry.S)                        │
│  - Save user RIP in RCX, RFLAGS in R11                  │
│  - Switch to kernel stack                               │
│  - Build syscall_frame_t                                │
└─────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────┐
│  sys_fork()                                             │
│  1. Create child process (PCB, kernel stack, COW mem)   │
│  2. Capture parent's user registers                     │
│  3. Create child kthread with fork_return entry         │
│  4. Set up child's kernel stack for syscall return      │
│  5. Add child to scheduler run queue                    │
│  6. Return child PID to parent                          │
└─────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────┐
│  SYSCALL EXIT                                           │
│  - Check scheduler for context switch                   │
│  - Parent: sysretq with RAX = child_pid                 │
└─────────────────────────────────────────────────────────┘

       ... later, scheduler runs child ...

┌─────────────────────────────────────────────────────────┐
│  Child kthread scheduled                                │
│  - kthread_switch() loads child's kernel stack          │
│  - Returns to fork_return stub                          │
└─────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────┐
│  fork_return (assembly stub)                            │
│  - Set RAX = 0 (child's return value)                   │
│  - Load user RIP, RSP, RFLAGS                           │
│  - sysretq back to userspace                            │
└─────────────────────────────────────────────────────────┘
       │
       ▼
   Child continues at instruction after fork()
   with return value 0
```

---

## Implementation Phases

### Phase 1: Scheduler Infrastructure

**Goal:** Create a basic round-robin scheduler that can switch between processes.

#### 1.1 Run Queue Data Structure

**File:** `kernel/sched.h` (new)

```c
#ifndef _SCHED_H
#define _SCHED_H

#include "process.h"

/* Initialize scheduler */
void sched_init(void);

/* Add process to run queue */
void sched_add(process_t *proc);

/* Remove process from run queue */
void sched_remove(process_t *proc);

/* Pick next process to run (returns NULL if none) */
process_t *sched_pick_next(void);

/* Main scheduling function - may context switch */
void schedule(void);

/* Yield CPU to another process */
void sched_yield(void);

#endif /* _SCHED_H */
```

**File:** `kernel/sched.c` (new)

- [ ] Define run queue (simple linked list for now)
- [ ] `sched_init()` - Initialize run queue
- [ ] `sched_add()` - Add RUNNABLE process to tail of queue
- [ ] `sched_remove()` - Remove process from queue
- [ ] `sched_pick_next()` - Remove and return head of queue
- [ ] `schedule()` - Core scheduling logic (see below)
- [ ] `sched_yield()` - Voluntary yield wrapper

#### 1.2 Schedule Function

```c
void schedule(void)
{
    process_t *current = process_current();
    process_t *next;

    /* If current is still runnable, add back to queue */
    if (current && current->state == PROC_RUNNING) {
        current->state = PROC_RUNNABLE;
        sched_add(current);
    }

    /* Pick next process */
    next = sched_pick_next();

    if (!next) {
        /* No runnable processes - idle */
        if (current && current->state == PROC_RUNNABLE) {
            /* Actually, keep running current */
            current->state = PROC_RUNNING;
            return;
        }
        /* True idle - halt until interrupt */
        while (!sched_pick_next()) {
            __asm__ volatile("sti; hlt; cli");
        }
        next = sched_pick_next();
    }

    if (next == current) {
        next->state = PROC_RUNNING;
        return;  /* No switch needed */
    }

    /* Perform context switch */
    next->state = PROC_RUNNING;
    process_switch(next);

    /* Switch kthreads */
    if (current && current->kthread && next->kthread) {
        kthread_switch(&current->kthread->rsp, next->kthread->rsp);
    }
}
```

#### 1.3 Integration Points

- [ ] Call `sched_init()` in `kmain()` after `kthread_init()`
- [ ] Modify `process_create()` to NOT auto-add to scheduler
- [ ] Modify `process_exit()` to call `schedule()` instead of halting

---

### Phase 2: Timer-Based Preemption

**Goal:** Periodically interrupt running processes to allow scheduling.

#### 2.1 Timer Setup

**File:** `arch/x86/kernel/timer.c` (new)

- [ ] Configure APIC timer for periodic interrupts (~100 Hz / 10ms)
- [ ] Register timer IRQ handler
- [ ] Timer handler calls `schedule()` if in user mode

#### 2.2 Timer IRQ Handler

```c
void timer_handler(interrupt_frame_t *frame)
{
    apic_eoi();

    /* Only preempt if we were in user mode */
    if (frame->cs & 0x3) {  /* RPL != 0 means user mode */
        /* Save user state */
        process_t *proc = process_current();
        if (proc) {
            proc->user_rip = frame->rip;
            proc->user_rsp = frame->rsp;
            proc->user_rflags = frame->rflags;
        }

        schedule();

        /* If we switched, we won't return here */
        /* If we didn't switch, just return normally */
    }
}
```

#### 2.3 Integration

- [ ] Register timer handler: `idt_register_irq(32, timer_handler)`
- [ ] Start timer in `kmain()` after scheduler init
- [ ] Ensure timer doesn't fire during critical kernel sections

---

### Phase 3: Fork Child Return Path

**Goal:** Set up child process to return from fork() with value 0.

#### 3.1 Capture Parent's User Registers

**Modify:** `arch/x86/kernel/syscall.c`

The syscall entry saves registers in `syscall_frame_t`, but we need access to user RIP (in RCX) and user RFLAGS (in R11). These are saved on the kernel stack by `syscall_entry.S`.

- [ ] Modify `syscall_entry.S` to pass frame pointer that includes RCX/R11
- [ ] Or: Save user RIP/RSP/RFLAGS in process struct at syscall entry

**Option A - Extend syscall_frame_t:**
```c
typedef struct {
    uint64_t r11;      /* User RFLAGS - ADD */
    uint64_t rcx;      /* User RIP - ADD */
    uint64_t nr;
    uint64_t arg1, arg2, arg3, arg4, arg5, arg6;
} syscall_frame_t;
```

**Option B - Save at entry:**
```c
/* At start of syscall_dispatch or in asm entry */
process_t *proc = process_current();
if (proc) {
    proc->user_rip = /* RCX from stack */;
    proc->user_rsp = /* from percpu */;
    proc->user_rflags = /* R11 from stack */;
}
```

#### 3.2 Fork Return Assembly Stub

**File:** `arch/x86/kernel/fork_return.S` (new)

```asm
.global fork_return
.type fork_return, @function

/*
 * fork_return - Entry point for forked child process
 *
 * Called when scheduler first runs a forked child.
 * Child's process struct has user_rip, user_rsp, user_rflags set.
 * We need to return to userspace with RAX=0.
 *
 * On entry:
 *   RDI = pointer to child's process_t (passed by kthread trampoline)
 */
fork_return:
    /* Get child's process struct */
    movq %rdi, %rbx                /* Save process pointer */

    /* Switch to child's address space */
    movq 72(%rbx), %rax            /* proc->pml4_phys (offset may vary) */
    movq %rax, %cr3

    /* Set up TSS.rsp0 for future syscalls */
    movq 80(%rbx), %rdi            /* proc->kernel_stack_top */
    call gdt_set_tss_rsp0

    /* Load user registers from process struct */
    movq 88(%rbx), %rcx            /* proc->user_rip -> RCX for sysretq */
    movq 96(%rbx), %rsp            /* proc->user_rsp -> will go to percpu */
    movq 104(%rbx), %r11           /* proc->user_rflags -> R11 for sysretq */

    /* Store user RSP in percpu for sysretq */
    movq %rsp, %gs:8               /* PERCPU_USER_RSP offset */

    /* Load kernel RSP (we're about to sysretq) */
    movq 80(%rbx), %rsp            /* Use top of kernel stack */

    /* Clear all other registers for security */
    xorq %rax, %rax                /* RAX = 0: child's fork return value! */
    xorq %rbx, %rbx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    /* Switch to user GS */
    swapgs

    /* Return to userspace */
    sysretq
```

#### 3.3 Create Child's Kthread

**Modify:** `arch/x86/kernel/syscall.c` - `sys_fork()`

```c
/* After copying address space... */

/* Capture parent's user state for child to return to */
child->user_rip = parent->user_rip;    /* Same instruction after fork() */
child->user_rsp = parent->user_rsp;    /* Same user stack */
child->user_rflags = parent->user_rflags;

/* Create kthread for child - will start at fork_return */
extern void fork_return(void *arg);
child->kthread = kthread_create_raw(fork_return, child);
if (!child->kthread) {
    /* Cleanup and return error */
}

/* Add child to scheduler */
child->state = PROC_RUNNABLE;
sched_add(child);
```

#### 3.4 kthread_create_raw Helper

We need a variant of `kthread_create` that doesn't wrap in trampoline:

```c
kthread_t *kthread_create_raw(void (*entry)(void *), void *arg)
{
    kthread_t *thread = kmalloc(sizeof(kthread_t));
    /* ... allocate stack ... */

    /* Set up stack so kthread_switch returns to entry(arg) */
    uint64_t *sp = (uint64_t *)(thread->stack_top);

    /* Argument in RDI (first param) */
    *(--sp) = (uint64_t)arg;        /* Will be popped into RDI */
    *(--sp) = (uint64_t)entry;      /* Return address */

    /* Callee-saved registers (zeros) */
    for (int i = 0; i < 6; i++)
        *(--sp) = 0;

    thread->rsp = (uint64_t)sp;
    return thread;
}
```

---

### Phase 4: Putting It Together

#### 4.1 Modified sys_fork()

```c
static int64_t sys_fork(uint64_t arg1, ...)
{
    process_t *parent = process_current();
    process_t *child;

    /* 1. Create child PCB and kernel stack */
    child = kmalloc(sizeof(process_t));
    memcpy(child, parent, sizeof(process_t));
    child->kernel_stack_top = (uint64_t)kmalloc(16384) + 16384;

    /* 2. Copy address space with COW */
    child->pml4_phys = vmm_copy_address_space_cow(parent->pml4_phys);

    /* 3. Set up child identity */
    child->pid = process_allocate_pid();
    child->parent = parent;
    child->children = NULL;
    child->sibling = parent->children;
    parent->children = child;

    /* 4. Copy file descriptors */
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        if (parent->fd_table[i].file)
            vfs_file_ref(parent->fd_table[i].file);
    }

    /* 5. Capture parent's user register state */
    /* These were saved at syscall entry */
    child->user_rip = parent->user_rip;
    child->user_rsp = parent->user_rsp;
    child->user_rflags = parent->user_rflags;

    /* 6. Create kthread for child */
    child->kthread = kthread_create_raw(fork_return, child);

    /* 7. Add to process list and scheduler */
    process_add(child);
    child->state = PROC_RUNNABLE;
    sched_add(child);

    log_info("FORK: Created child PID %llu", child->pid);

    /* 8. Return child PID to parent */
    return (int64_t)child->pid;
}
```

#### 4.2 Test Scenario

```c
/* Userspace test */
int main() {
    long pid = fork();

    if (pid < 0) {
        write(1, "fork failed\n", 12);
        return 1;
    }

    if (pid == 0) {
        /* Child */
        write(1, "I am child\n", 11);
        exit(42);
    } else {
        /* Parent */
        write(1, "I am parent\n", 12);
        int status;
        waitpid(pid, &status, 0);
        /* status should encode exit code 42 */
    }

    return 0;
}
```

---

## Task Checklist

### Phase 1: Scheduler Infrastructure
- [ ] Create `kernel/sched.h` with scheduler API
- [ ] Create `kernel/sched.c` with run queue implementation
- [ ] Implement `sched_init()`
- [ ] Implement `sched_add()` / `sched_remove()`
- [ ] Implement `sched_pick_next()`
- [ ] Implement `schedule()`
- [ ] Implement `sched_yield()`
- [ ] Call `sched_init()` in `kmain()`
- [ ] Modify `process_exit()` to call `schedule()`
- [ ] Test: Two kthreads yielding to each other

### Phase 2: Timer Preemption
- [ ] Create `arch/x86/kernel/timer.c`
- [ ] Configure APIC timer for 100 Hz
- [ ] Implement `timer_handler()`
- [ ] Save user state on timer interrupt
- [ ] Call `schedule()` from timer handler
- [ ] Test: Process preemption without explicit yield

### Phase 3: Fork Return Path
- [ ] Modify syscall entry to save user RIP/RSP/RFLAGS in process_t
- [ ] Create `arch/x86/kernel/fork_return.S`
- [ ] Implement `fork_return` assembly stub
- [ ] Implement `kthread_create_raw()` helper
- [ ] Modify `sys_fork()` to create child kthread
- [ ] Modify `sys_fork()` to capture parent's user registers
- [ ] Modify `sys_fork()` to add child to scheduler
- [ ] Test: fork() returns 0 to child, child_pid to parent

### Phase 4: Integration & Testing
- [ ] Test fork + exit
- [ ] Test fork + waitpid
- [ ] Test multiple forks
- [ ] Test COW with actual writes after fork
- [ ] Test fork + exec (if execve implemented)

---

## File Summary

| File | Action | Description |
|------|--------|-------------|
| `kernel/sched.h` | Create | Scheduler API |
| `kernel/sched.c` | Create | Run queue and schedule() |
| `arch/x86/kernel/timer.c` | Create | APIC timer for preemption |
| `arch/x86/kernel/fork_return.S` | Create | Child return-to-userspace stub |
| `arch/x86/kernel/syscall_entry.S` | Modify | Save user registers to process_t |
| `arch/x86/kernel/syscall.c` | Modify | Update sys_fork() |
| `kernel/kthread.c` | Modify | Add kthread_create_raw() |
| `kernel/process.c` | Modify | process_exit() calls schedule() |
| `init/main.c` | Modify | Initialize scheduler and timer |

---

## Notes

### Why not just use iretq?

The `sysretq` instruction is faster than `iretq` but has constraints:
- RCX must contain target RIP
- R11 must contain target RFLAGS
- Cannot return to non-canonical addresses

For fork, `sysretq` is appropriate because we're returning to the same userspace address the parent would return to.

### Stack Considerations

Each process needs:
1. **User stack** - In user address space (COW shared initially)
2. **Kernel stack** - For syscall/interrupt handling (16KB, separate per process)
3. **Kthread stack** - For kernel thread context switches (could share with kernel stack)

### Future Improvements

- Priority-based scheduling
- Multiple run queues (real-time, normal, idle)
- CPU affinity for SMP
- Proper signal delivery to processes
- Process groups and sessions
