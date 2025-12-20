# Process Management and Multitasking in SeedOS

## Chapter 1: Definitions and Concepts

### 1.1 Process

A **process** is an instance of a running program. It has its own:
- Virtual address space (memory)
- CPU register state (RIP, RSP, general-purpose registers)
- Execution state (running, ready, blocked, etc.)

**Example:** When you type `ls` in the shell, a new process is created to run the `ls` program. It has its own memory for code and stack, separate from the shell.

### 1.2 Context

A **context** is the complete CPU state needed to resume a process:
- Instruction pointer (RIP) - where to continue executing
- Stack pointer (RSP) - where the stack is
- General-purpose registers (RAX, RBX, etc.)
- Segment selectors (CS, SS) - privilege level
- Flags register (RFLAGS) - condition flags, interrupt enable

**Example:** When process A is interrupted and we switch to process B, we save A's context (all registers) and load B's context. When we switch back to A, we restore A's saved context.

### 1.3 Context Switch

A **context switch** is the act of saving one process's context and loading another's. This is how the CPU switches between processes.

**Example:**
```
Process A running (RIP=0x401000, RSP=0x7fff000)
    ↓ timer interrupt
Save A's context to memory
Load B's context from memory
    ↓
Process B running (RIP=0x402000, RSP=0x7ffe000)
```

### 1.4 Preemption

**Preemption** is forcibly taking the CPU away from a running process (without its cooperation). This is done via hardware interrupts (typically a timer).

**Example:** Process A is in an infinite loop. Every 10ms, the timer fires an interrupt. The kernel can then switch to process B, even though A never voluntarily yielded.

**Non-preemptive (cooperative):** Processes must voluntarily yield. A buggy infinite loop would freeze the system.

**Preemptive:** The timer forces switches. Infinite loops don't freeze the system.

### 1.5 Scheduler

The **scheduler** decides which process runs next. Common algorithms:
- **Round-robin:** Each process gets a time slice, then the next process runs
- **Priority-based:** Higher priority processes run first

**Example:** With round-robin and 3 processes (A, B, C), the execution order is: A→B→C→A→B→C→...

### 1.6 Kernel Mode vs User Mode

- **User mode (Ring 3):** Restricted privileges, can't access hardware directly
- **Kernel mode (Ring 0):** Full privileges, can access everything

**Example:** When a process calls `write()`, it triggers a syscall which transitions to kernel mode. The kernel performs the I/O, then returns to user mode.

### 1.7 Kernel Stack

Each transition to kernel mode needs a stack for the kernel code to use. There are two models:

**Single kernel stack:** All processes share one kernel stack. Simple but limiting.

**Per-process kernel stack:** Each process has its own kernel stack. More complex but enables true concurrency.

**Example (single stack problem):**
```
Process A syscall → uses kernel stack
    A blocks waiting for I/O
    Want to run Process B
    But B's syscall would overwrite A's kernel stack!
```

### 1.8 Blocking vs Non-blocking

**Blocking:** The process waits (sleeps) until an operation completes.
**Non-blocking:** The operation returns immediately, possibly incomplete.

**Example:**
- Blocking read: `read()` waits until data is available
- Non-blocking read: `read()` returns immediately with "no data yet" error

### 1.9 Synchronous vs Asynchronous Execution

**Synchronous:** Caller waits for the operation to complete before continuing.
**Asynchronous:** Caller continues immediately; operation runs in parallel.

**Example:**
- `spawn()` (synchronous): Shell waits until child process exits
- `spawn_async()` (asynchronous): Shell continues immediately; child runs in background

---

## Chapter 2: Current State in SeedOS

### 2.1 What Works

#### Process Creation and Execution
- Processes can be created with their own address space
- ELF executables are loaded and run
- Processes can pass arguments (argc/argv)
- Processes can exit with an exit code

#### Synchronous Process Execution (spawn)
```
Shell → spawn(ls) → ls runs → ls exits → Shell continues
```
This works correctly. The shell blocks until the child exits.

#### Timer-Based Preemption (Userspace Only)
- PIT fires at 100 Hz
- Timer interrupt triggers scheduler
- Scheduler can switch between processes **that are in userspace**

#### Round-Robin Scheduler
- Processes in the ready queue get fair time slices
- Context is saved/restored correctly for userspace processes

### 2.2 What Partially Works

#### Asynchronous Process Execution (spawn_async)
```
Shell → spawn_async(bgcount) → Shell continues → bgcount runs via timer
```
- The child process IS added to the scheduler
- The child DOES run when timer preempts
- **BUT:** When the child exits, it returns to the wrong context
- **BUT:** Shell can't properly wait for the child

#### Background Execution (&)
```
shell$ bgcount &
[3] bgcount
shell$ ← prompt returns
```
- Process starts and runs
- Prompt returns immediately
- **BUT:** Child's exit corrupts the execution flow

### 2.3 What Doesn't Work

#### True Concurrent Background Processes
```
shell$ bgcount A &
shell$ bgcount B &
shell$ ← both should run concurrently while shell waits for input
```
This doesn't work reliably because:
1. Shell is in kernel mode (waiting for keyboard input)
2. Kernel preemption is not properly supported
3. When children exit, they corrupt the return path

#### Waitpid for Async Children
```c
pid = spawn_async("/bin/child", args);
// ... do other work ...
waitpid(pid, &status, 0);  // Wait for child
```
The waitpid enters a polling loop, but the child can't properly signal completion because the context switching between kernel waitpid and userspace child is broken.

---

## Chapter 3: What's Missing for Linux-like Background Processes

To achieve the Linux experience where you can run multiple background processes while the shell remains responsive, SeedOS needs the following:

### 3.1 Per-Process Kernel Stacks (CRITICAL)

**Current state:** Single shared kernel stack for all processes.

**Problem:** When process A is in a syscall (using the kernel stack) and we want to switch to process B, B's syscall would overwrite A's kernel stack. When we switch back to A, its stack is corrupted.

**Solution:** Each process needs its own kernel stack.

```c
struct process {
    // ... existing fields ...
    uint64_t kernel_stack;      // Physical address of kernel stack
    uint64_t kernel_stack_top;  // Top of kernel stack (RSP0 for TSS)
};
```

**Changes needed:**
1. Allocate a kernel stack page when creating a process
2. Update TSS.RSP0 on every context switch to point to the current process's kernel stack
3. Save/restore the kernel stack pointer in the process struct

**Complexity:** Medium. ~100-200 lines of code changes.

### 3.2 Proper Kernel Context Saving (CRITICAL)

**Current state:** Only userspace context (registers at interrupt time) is saved.

**Problem:** When a process is in a syscall (kernel mode) and gets preempted, we need to save WHERE in the kernel code it was, not just the userspace state.

**Solution:** Save the kernel execution state when switching away from a process that's in kernel mode.

**Changes needed:**
1. Detect when a process is preempted while in kernel mode
2. Save the kernel RIP/RSP (where in the syscall we were)
3. On resume, return to that kernel location to finish the syscall

**Complexity:** High. Requires careful handling of nested interrupts and kernel reentrancy.

### 3.3 Blocking Sleep Queue (IMPORTANT)

**Current state:** Waitpid uses busy-waiting (polling in a loop with HLT).

**Problem:** Polling wastes CPU cycles and doesn't integrate well with the scheduler.

**Solution:** Implement proper sleep queues.

```c
struct wait_queue {
    struct process *waiters;  // Linked list of waiting processes
};

void sleep_on(struct wait_queue *wq);  // Remove from ready queue, add to wait queue
void wake_up(struct wait_queue *wq);   // Move from wait queue to ready queue
```

**Changes needed:**
1. Add wait_queue structure
2. Modify waitpid to use sleep_on() instead of polling
3. Modify sys_exit to use wake_up() for parent
4. Integrate with scheduler

**Complexity:** Medium. ~150 lines of code.

### 3.4 Proper Process State Machine (IMPORTANT)

**Current state:** States exist but transitions are ad-hoc.

**Proper state machine:**
```
                    ┌─────────────┐
     create         │   UNUSED    │
        │           └─────────────┘
        ▼                 ▲
┌─────────────┐      destroy
│    READY    │◄─────────────────────┐
└─────────────┘                      │
        │                            │
   schedule                     wake │
        ▼                            │
┌─────────────┐    block     ┌───────┴─────┐
│   RUNNING   │─────────────►│   BLOCKED   │
└─────────────┘              └─────────────┘
        │
      exit
        ▼
┌─────────────┐
│   ZOMBIE    │ (waiting to be reaped by parent)
└─────────────┘
        │
      wait
        ▼
┌─────────────┐
│   UNUSED    │
└─────────────┘
```

**Changes needed:**
1. Enforce state transitions in all scheduler/process functions
2. Add assertions to catch invalid transitions
3. Clean up the exit path

**Complexity:** Low-Medium. Mostly refactoring.

### 3.5 SIGCHLD or Exit Notification (NICE TO HAVE)

**Current state:** Parent must actively poll or block-wait for children.

**Problem:** Shell can't know when a background process exits unless it checks.

**Solution:** Signal mechanism or simpler notification.

```c
// Option 1: Full signals (complex)
signal(SIGCHLD, handler);

// Option 2: Simple notification flag
struct process {
    int child_exited;  // Set when any child exits
};
```

**Complexity:** Signals are high complexity. Simple flag is low.

### 3.6 Job Control (NICE TO HAVE)

**Current state:** No concept of foreground/background jobs.

**Solution:** Track process groups and job states.

```c
struct job {
    int job_id;
    struct process *processes;  // Processes in this job
    int foreground;             // Is this the foreground job?
};
```

**Commands:** `jobs`, `fg`, `bg`, Ctrl+Z

**Complexity:** Medium-High.

---

## Chapter 4: Implementation Priority

### Phase 1: Core Infrastructure (Required for Background Processes)

1. **Per-process kernel stacks** - Without this, nothing else works
2. **Kernel context saving** - Required for preempting syscalls
3. **Fix sys_exit for async processes** - Proper cleanup path

### Phase 2: Better Blocking (Improves Reliability)

4. **Sleep queues** - Replace polling with proper blocking
5. **Process state machine cleanup** - Reduce bugs

### Phase 3: User Experience (Polish)

6. **Exit notification** - Shell knows when background job finishes
7. **Job control** - Full fg/bg/jobs support

---

## Chapter 5: Current Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         USER SPACE                               │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐      │
│  │  Shell  │    │   ls    │    │ bgcount │    │  ctest  │      │
│  │ (sync)  │    │ (sync)  │    │ (async) │    │ (sync)  │      │
│  └────┬────┘    └────┬────┘    └────┬────┘    └────┬────┘      │
│       │              │              │              │            │
├───────┼──────────────┼──────────────┼──────────────┼────────────┤
│       │   syscall    │   syscall    │   syscall    │            │
│       ▼              ▼              ▼              ▼            │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    SYSCALL HANDLER                       │   │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐    │   │
│  │  │  spawn  │  │  exit   │  │  read   │  │ waitpid │    │   │
│  │  │ (sync)  │  │         │  │(blocks) │  │(polls)  │    │   │
│  │  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘    │   │
│  └───────┼────────────┼────────────┼────────────┼──────────┘   │
│          │            │            │            │               │
│          ▼            ▼            ▼            ▼               │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              SINGLE SHARED KERNEL STACK                   │  │  ← PROBLEM!
│  │  (All syscalls use the same stack - can't switch away)   │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
│          ┌───────────────────┼───────────────────┐             │
│          ▼                   ▼                   ▼             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐        │
│  │  SCHEDULER  │    │   PROCESS   │    │   MEMORY    │        │
│  │             │    │  MANAGEMENT │    │  MANAGEMENT │        │
│  │ ready_queue │    │   slots[]   │    │  PMM + VMM  │        │
│  └─────────────┘    └─────────────┘    └─────────────┘        │
│                                                                 │
│                         KERNEL SPACE                            │
└─────────────────────────────────────────────────────────────────┘
```

### The Core Problem Illustrated

```
Timeline of attempted concurrent execution:

Time    Shell                   BgCount A              BgCount B
────    ─────                   ─────────              ─────────
T0      spawn_async(A)
T1      spawn_async(B)
T2      read() ──► KERNEL
T3      │ waiting...            [in ready queue]       [in ready queue]
T4      │ ▼
T5      │ TIMER! ───────────────► runs (userspace)
T6      │ kernel stack          │ timer preempts
T7      │ still has             │
T8      │ shell's read()  ◄─────┤ TIMER! switch back... but to where?
T9      │ state                 │
T10     │                       │
        │                       ▼
        │               Problem: Shell is in KERNEL MODE
        │               Its read() syscall is using the kernel stack
        │               We can't resume shell - would corrupt stack
        │               We can't switch to B - would also corrupt stack
        ▼
    STUCK or CRASH
```

### What Per-Process Kernel Stacks Would Enable

```
┌─────────────────────────────────────────────────────────────────┐
│                         USER SPACE                               │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────┐         ┌─────────┐         ┌─────────┐           │
│  │  Shell  │         │ BgCount │         │ BgCount │           │
│  │         │         │    A    │         │    B    │           │
│  └────┬────┘         └────┬────┘         └────┬────┘           │
├───────┼──────────────────┼──────────────────┼───────────────────┤
│       ▼                  ▼                  ▼                   │
│  ┌─────────┐        ┌─────────┐        ┌─────────┐             │
│  │ Kernel  │        │ Kernel  │        │ Kernel  │             │
│  │ Stack   │        │ Stack   │        │ Stack   │             │
│  │ (Shell) │        │  (A)    │        │  (B)    │             │
│  │         │        │         │        │         │             │
│  │ read()  │        │ (empty) │        │ (empty) │             │
│  │ waiting │        │         │        │         │             │
│  └─────────┘        └─────────┘        └─────────┘             │
│       │                  │                  │                   │
│       └──────────────────┴──────────────────┘                   │
│                          │                                      │
│                          ▼                                      │
│                   ┌─────────────┐                               │
│                   │  SCHEDULER  │                               │
│                   │             │                               │
│                   │ Can switch  │                               │
│                   │ freely!     │                               │
│                   └─────────────┘                               │
└─────────────────────────────────────────────────────────────────┘

Now when timer fires:
- Shell's kernel state is preserved on its own stack
- Can switch to A or B freely
- When A/B exit, can switch back to Shell
- Shell's read() resumes exactly where it was
```

---

## Chapter 6: Quick Reference

### Current SeedOS Process API

| Function | Type | Works? | Notes |
|----------|------|--------|-------|
| `spawn(path, argv)` | Synchronous | ✅ Yes | Blocks until child exits |
| `spawn_async(path, argv)` | Asynchronous | ⚠️ Partial | Child runs but exit is broken |
| `waitpid(pid, status, 0)` | Blocking wait | ⚠️ Partial | Polls, doesn't work with async |
| `exit(code)` | Terminate | ✅ Yes | Works for sync spawn |
| `getpid()` | Get PID | ✅ Yes | |

### Process States

| State | Meaning | In Ready Queue? |
|-------|---------|-----------------|
| PROC_UNUSED | Slot available | No |
| PROC_READY | Waiting for CPU | Yes |
| PROC_RUNNING | Currently executing | Yes (as current) |
| PROC_BLOCKED | Waiting for event | No |
| PROC_ZOMBIE | Exited, waiting for parent | No |

### Key Files

| File | Purpose |
|------|---------|
| `src/process.c` | Process creation, destruction, memory setup |
| `src/sched.c` | Scheduler, ready queue, context switching |
| `src/syscall.c` | System call implementations |
| `src/context_switch.S` | Low-level context switch assembly |
| `src/idt.c` | Timer interrupt handler |
