# Dash Shell Implementation Plan for SeedOS

A comprehensive plan for implementing a minimal POSIX-like shell (inspired by Dash) for SeedOS.

## Executive Summary

Implementing a real shell requires fundamental changes to SeedOS's process model. The current kernel runs processes synchronously from kernel space. A true shell needs:
- **fork()** - Create child processes from userspace
- **exec()** - Replace process image with new program
- **wait()** - Parent waits for child termination
- **pipe()** - Inter-process communication
- **dup2()** - File descriptor manipulation for redirections
- **Signals** - Process notification (at minimum SIGCHLD)

This plan is organized into phases, from kernel prerequisites through full shell implementation.

## Current State Analysis

### What SeedOS Has
```
Syscalls:     exit, write, read, getpid, uptime, sbrk, open, close, lseek
Process:      Single-tasking, kernel creates/runs processes synchronously
Filesystem:   TAR-based initrd, basic VFS
Memory:       PMM, VMM with per-process address spaces
```

### What a Shell Needs
```
Syscalls:     fork, exec, wait/waitpid, pipe, dup/dup2, getcwd, chdir, stat
Process:      Multi-process, userspace process creation, parent-child relationships
Filesystem:   Directory traversal (for PATH, globbing)
Signals:      At least SIGCHLD for child termination notification
```

### Gap Analysis

| Requirement | Current State | Gap |
|-------------|---------------|-----|
| Process creation from userspace | Kernel-only | **fork() syscall** |
| Program execution | Kernel loads ELF | **exec() syscall** |
| Parent-child wait | N/A | **wait()/waitpid() syscalls** |
| Pipelines | N/A | **pipe() syscall** |
| Redirections | N/A | **dup/dup2() syscalls** |
| Working directory | N/A | **getcwd/chdir syscalls** |
| Process table | Single process | **Multi-process support** |
| Signals | None | **Signal infrastructure** |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         DASH SHELL                              │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │  Lexer   │→│  Parser  │→│ Executor │→│ Built-in Commands│   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────────┘   │
│                                │                                │
│                     ┌──────────┴──────────┐                    │
│                     ▼                      ▼                    │
│              External Commands       Pipelines/Redirects        │
│              (fork + exec)           (pipe + dup2 + fork)       │
└─────────────────────────────────────────────────────────────────┘
                              │
                    System Calls (int 0x80)
                              │
┌─────────────────────────────▼───────────────────────────────────┐
│                         KERNEL                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Process Manager                       │   │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐       │   │
│  │  │  fork   │ │  exec   │ │  wait   │ │ signals │       │   │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘       │   │
│  │                                                          │   │
│  │  ┌───────────────────────────────────────────────────┐  │   │
│  │  │              Process Table                         │  │   │
│  │  │  PID | PPID | State | Exit Code | Address Space   │  │   │
│  │  └───────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                         VFS                              │   │
│  │  pipe() creates pipe vnodes with circular buffers        │   │
│  │  dup2() manipulates file descriptor table                │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Implementation Phases

---

## Phase 0: Scheduler Foundation

Before multi-process support, we need a basic scheduler.

### 0.1 Process Table

**Goal:** Track multiple processes in the kernel.

```c
// process.h additions

#define MAX_PROCESSES 64

enum process_state {
    PROC_UNUSED = 0,    // Slot available
    PROC_EMBRYO,        // Being created
    PROC_READY,         // Ready to run
    PROC_RUNNING,       // Currently executing
    PROC_SLEEPING,      // Waiting for event
    PROC_ZOMBIE,        // Exited, waiting for parent to reap
};

struct process {
    // Existing fields...
    pid_t pid;
    pid_t ppid;                  // Parent PID
    enum process_state state;
    int exit_code;

    // Scheduling
    struct process *parent;      // Parent process pointer
    uint64_t kernel_stack;       // Kernel stack for this process
    uint64_t saved_rsp;          // Saved stack pointer for context switch

    // Wait queue
    struct process *wait_queue;  // Processes waiting for this one
};

// Global process table
extern struct process proc_table[MAX_PROCESSES];
extern struct process *current_proc;
```

**Tasks:**
- [ ] Define process states enum
- [ ] Add parent/child tracking to process struct
- [ ] Create global process table array
- [ ] Implement `proc_alloc()` - allocate process slot
- [ ] Implement `proc_free()` - release process slot
- [ ] Track `current_proc` globally

### 0.2 Kernel Stacks

**Goal:** Each process needs its own kernel stack for syscalls/interrupts.

```c
// Each process gets a kernel stack page
#define KERNEL_STACK_SIZE 4096

struct process {
    // ...
    uint64_t kernel_stack_phys;  // Physical address of kernel stack
    uint64_t kernel_stack_top;   // Top of kernel stack (for TSS.RSP0)
};
```

**Tasks:**
- [ ] Allocate kernel stack page per process
- [ ] Update TSS.RSP0 on process switch
- [ ] Save/restore kernel stack pointer in context switch

### 0.3 Basic Round-Robin Scheduler

**Goal:** Switch between ready processes.

```c
// scheduler.h

void scheduler_init(void);
void scheduler_add(struct process *p);
void scheduler_remove(struct process *p);
void schedule(void);           // Pick next process and switch
void yield(void);              // Voluntarily give up CPU
void sleep(void *channel);     // Sleep until wakeup(channel)
void wakeup(void *channel);    // Wake all processes sleeping on channel
```

**Tasks:**
- [ ] Implement simple ready queue (linked list)
- [ ] Implement `schedule()` - context switch to next ready process
- [ ] Implement `yield()` syscall
- [ ] Hook timer interrupt to call `schedule()` for preemption
- [ ] Implement sleep/wakeup mechanism

### 0.4 Context Switch Enhancement

**Goal:** Switch between user processes (not just kernel ↔ user).

Current context_switch.S handles kernel→user and user→kernel.
Need: user→kernel→different_user (full context switch).

```asm
; context_switch.S additions

; Save current process context, restore next process context
; Called from schedule() when switching processes
switch_context:
    ; Save callee-saved registers to current process's kernel stack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save current stack pointer to current_proc->saved_rsp
    mov rax, [current_proc]
    mov [rax + PROC_SAVED_RSP], rsp

    ; Load next process's stack pointer
    mov rax, [next_proc]
    mov rsp, [rax + PROC_SAVED_RSP]

    ; Update current_proc
    mov [current_proc], rax

    ; Update TSS.RSP0 for new process
    ; ...

    ; Restore callee-saved registers from new process's kernel stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
```

**Tasks:**
- [ ] Implement process-to-process context switch
- [ ] Save/restore full register state
- [ ] Update CR3 (address space) on switch
- [ ] Update TSS.RSP0 on switch
- [ ] Handle first-time process startup (fake return frame)

---

## Phase 1: Fork System Call

### 1.1 Understanding Fork

```
fork() creates an exact copy of the calling process:
- New process (child) gets new PID
- Child is copy of parent (same code, data, stack)
- Both return from fork(): parent gets child PID, child gets 0

Parent Process          fork()           Child Process
┌────────────────┐                      ┌────────────────┐
│ pid = 1        │                      │ pid = 2        │
│ code           │  ──────────────────▶ │ code (copy)    │
│ data           │                      │ data (copy)    │
│ stack          │                      │ stack (copy)   │
│ returns 2      │                      │ returns 0      │
└────────────────┘                      └────────────────┘
```

### 1.2 Fork Implementation Strategy

**Copy-on-Write (Advanced):** Share pages until written, then copy.
**Simple Copy (Initial):** Copy all pages immediately.

We'll start with simple copy for clarity.

```c
// syscall: fork()
// Returns: child PID to parent, 0 to child, -1 on error

pid_t sys_fork(void) {
    struct process *parent = current_proc;
    struct process *child = proc_alloc();
    if (!child) return -1;

    // Copy address space
    child->pml4 = vmm_copy_address_space(parent->pml4);
    if (!child->pml4) {
        proc_free(child);
        return -1;
    }

    // Copy file descriptors
    fd_table_copy(&child->fds, &parent->fds);

    // Set up parent-child relationship
    child->ppid = parent->pid;
    child->parent = parent;

    // Copy kernel stack and set child's return value to 0
    // ... (tricky part: set up child to return 0 from fork)

    // Child is ready to run
    child->state = PROC_READY;
    scheduler_add(child);

    // Parent returns child's PID
    return child->pid;
}
```

### 1.3 Address Space Copying

```c
// vmm.c additions

// Copy entire address space (user portion only)
uint64_t vmm_copy_address_space(uint64_t src_pml4_phys) {
    uint64_t dst_pml4_phys = vmm_create_address_space();
    if (!dst_pml4_phys) return 0;

    // Walk source page tables, copy all user pages
    uint64_t *src_pml4 = phys_to_virt(src_pml4_phys);
    uint64_t *dst_pml4 = phys_to_virt(dst_pml4_phys);

    // Copy entries 0-255 (user space), entries 256-511 are kernel (shared)
    for (int i = 0; i < 256; i++) {
        if (src_pml4[i] & PTE_PRESENT) {
            // Recursively copy PDPT, PD, PT, and actual pages
            dst_pml4[i] = copy_page_table_entry(src_pml4[i], 3);
        }
    }

    return dst_pml4_phys;
}

// Recursively copy page table hierarchy
static uint64_t copy_page_table_entry(uint64_t entry, int level) {
    if (!(entry & PTE_PRESENT)) return 0;

    uint64_t src_phys = entry & PTE_ADDR_MASK;
    uint64_t flags = entry & ~PTE_ADDR_MASK;

    if (level == 0) {
        // Leaf level: copy actual page data
        uint64_t dst_phys = pmm_alloc();
        memcpy(phys_to_virt(dst_phys), phys_to_virt(src_phys), PAGE_SIZE);
        return dst_phys | flags;
    } else {
        // Non-leaf: allocate new table and copy entries
        uint64_t dst_phys = pmm_alloc();
        uint64_t *src_table = phys_to_virt(src_phys);
        uint64_t *dst_table = phys_to_virt(dst_phys);

        memset(dst_table, 0, PAGE_SIZE);
        for (int i = 0; i < 512; i++) {
            if (src_table[i] & PTE_PRESENT) {
                dst_table[i] = copy_page_table_entry(src_table[i], level - 1);
            }
        }
        return dst_phys | flags;
    }
}
```

### 1.4 Fork Return Value Magic

The tricky part: child must return 0 from fork(), parent returns child PID.

```c
// When setting up child's kernel stack, we manipulate the saved
// registers so that when the child is scheduled and returns from
// the "syscall", it sees 0 in RAX.

void setup_child_return(struct process *child, struct process *parent) {
    // Copy parent's kernel stack to child
    memcpy(phys_to_virt(child->kernel_stack_phys),
           phys_to_virt(parent->kernel_stack_phys),
           KERNEL_STACK_SIZE);

    // Find the saved syscall_registers on child's kernel stack
    struct syscall_registers *child_regs = /* calculate offset */;

    // Set RAX to 0 so child returns 0 from fork()
    child_regs->rax = 0;

    // Set up saved_rsp so scheduler resumes at right point
    child->saved_rsp = /* point to switch_context return frame */;
}
```

**Tasks:**
- [ ] Implement `vmm_copy_address_space()`
- [ ] Implement `fd_table_copy()` for file descriptors
- [ ] Implement child kernel stack setup
- [ ] Implement `sys_fork()` syscall
- [ ] Add fork to syscall dispatch table
- [ ] Add userspace wrapper `pid_t fork(void)`
- [ ] Test with simple fork program

---

## Phase 2: Exec System Call

### 2.1 Understanding Exec

```
exec() replaces current process image with new program:
- Same PID, same file descriptors (mostly)
- New code, data, stack, heap
- Does NOT return on success (new program starts at its entry point)

Before exec("ls")        After exec("ls")
┌────────────────┐      ┌────────────────┐
│ pid = 2        │      │ pid = 2        │  (same)
│ shell code     │      │ ls code        │  (replaced)
│ shell data     │  ──▶ │ ls data        │  (replaced)
│ shell stack    │      │ ls stack       │  (replaced)
└────────────────┘      └────────────────┘
```

### 2.2 Exec Family

```c
// execve is the base syscall, others are library wrappers
int execve(const char *pathname, char *const argv[], char *const envp[]);

// Library functions built on execve:
int execl(const char *path, const char *arg, ... /*, NULL */);
int execv(const char *path, char *const argv[]);
int execle(const char *path, const char *arg, .../*, NULL, char *const envp[]*/);
int execlp(const char *file, const char *arg, ... /*, NULL */);  // PATH search
int execvp(const char *file, char *const argv[]);                 // PATH search
```

### 2.3 Exec Implementation

```c
// syscall: execve(path, argv, envp)
// Returns: -1 on error, does not return on success

int sys_execve(const char *path, char *const argv[], char *const envp[]) {
    struct process *p = current_proc;

    // Find executable in filesystem
    struct tar_file *exe = tar_find(path);
    if (!exe) return -ENOENT;

    // Validate ELF
    if (elf_validate(exe->data, exe->size) < 0) return -ENOEXEC;

    // Save argv/envp (they're in old address space, about to be destroyed)
    char **saved_argv = copy_strings_to_kernel(argv);
    char **saved_envp = copy_strings_to_kernel(envp);

    // Destroy old address space (user portion)
    vmm_free_user_pages(p->pml4);

    // Load new executable
    uint64_t entry;
    if (elf_load(exe->data, exe->size, p->pml4, &entry) < 0) {
        // Fatal: can't recover, process must die
        sys_exit(-1);
    }

    // Set up new stack with argv, envp
    uint64_t new_stack = setup_user_stack(p->pml4, saved_argv, saved_envp);

    // Free kernel copies
    free_kernel_strings(saved_argv);
    free_kernel_strings(saved_envp);

    // Reset heap
    p->brk = USER_HEAP_BASE;

    // Close close-on-exec file descriptors
    fd_table_close_cloexec(&p->fds);

    // Jump to new program (does not return)
    enter_userspace(entry, new_stack);

    // Never reached
    return 0;
}
```

### 2.4 Stack Setup for New Program

```
Stack layout for execve:
                        High addresses
┌─────────────────────────────────────┐
│  Environment strings                │  "PATH=/bin"
│  "HOME=/root"                       │
├─────────────────────────────────────┤
│  Argument strings                   │  "ls"
│  "-la"                              │  "/home"
├─────────────────────────────────────┤
│  NULL (end of envp)                 │
├─────────────────────────────────────┤
│  envp[1] pointer                    │
│  envp[0] pointer                    │
├─────────────────────────────────────┤
│  NULL (end of argv)                 │
├─────────────────────────────────────┤
│  argv[2] pointer                    │
│  argv[1] pointer                    │
│  argv[0] pointer                    │
├─────────────────────────────────────┤
│  argc                               │  ← RSP points here
└─────────────────────────────────────┘
                        Low addresses
```

**Tasks:**
- [ ] Implement `copy_strings_to_kernel()`
- [ ] Implement `vmm_free_user_pages()`
- [ ] Implement `setup_user_stack()` with argv/envp
- [ ] Implement `sys_execve()` syscall
- [ ] Add execve to syscall dispatch table
- [ ] Add userspace wrappers (execve, execv, execl, etc.)
- [ ] Test with fork + exec

---

## Phase 3: Wait System Call

### 3.1 Understanding Wait

```
wait() blocks parent until child exits:
- Parent calls wait(&status)
- If child already exited (zombie), returns immediately
- If child still running, parent sleeps until child exits
- Returns child's PID and exit status

Parent                              Child
┌──────────────────┐               ┌──────────────────┐
│ fork()           │──────────────▶│ pid = 2          │
│ pid = 1          │               │                  │
│                  │               │ do_work()        │
│ wait(&status) ───┼─ SLEEP ─────▶ │                  │
│                  │               │ exit(42)         │
│                  │◀── WAKEUP ────│ ZOMBIE           │
│ status = 42      │               │ REAPED           │
└──────────────────┘               └──────────────────┘
```

### 3.2 Wait Implementation

```c
// syscall: wait(int *status)
// Returns: PID of terminated child, or -1 on error

pid_t sys_wait(int *status) {
    return sys_waitpid(-1, status, 0);
}

// syscall: waitpid(pid, status, options)
// pid = -1: wait for any child
// pid > 0:  wait for specific child
// options: WNOHANG = don't block

pid_t sys_waitpid(pid_t pid, int *status, int options) {
    struct process *p = current_proc;

    while (1) {
        // Look for zombie children
        struct process *child = find_zombie_child(p, pid);

        if (child) {
            // Found a zombie, reap it
            pid_t child_pid = child->pid;
            if (status) {
                // Validate user pointer
                if (!vmm_validate_user_range(status, sizeof(int))) {
                    return -EFAULT;
                }
                *status = child->exit_code;
            }

            // Free child resources
            proc_free(child);

            return child_pid;
        }

        // No zombie children
        if (!has_children(p, pid)) {
            return -ECHILD;  // No children to wait for
        }

        if (options & WNOHANG) {
            return 0;  // Non-blocking, no child ready
        }

        // Sleep until a child exits
        sleep(&p->wait_channel);
    }
}

// Called when a process exits
void process_exit(int code) {
    struct process *p = current_proc;

    // Close all file descriptors
    fd_table_close_all(&p->fds);

    // Free address space (except kernel mappings)
    vmm_free_user_pages(p->pml4);

    // Reparent children to init (or just orphan them for now)
    reparent_children(p);

    // Become zombie
    p->exit_code = code;
    p->state = PROC_ZOMBIE;

    // Wake parent if waiting
    if (p->parent) {
        wakeup(&p->parent->wait_channel);
    }

    // Never run again
    schedule();

    // Never reached
    panic("zombie scheduled");
}
```

**Tasks:**
- [ ] Add wait_channel to process struct
- [ ] Implement `find_zombie_child()`
- [ ] Implement `has_children()`
- [ ] Implement `reparent_children()`
- [ ] Modify `sys_exit()` to become zombie instead of destroying
- [ ] Implement `sys_wait()` and `sys_waitpid()`
- [ ] Add to syscall dispatch table
- [ ] Add userspace wrappers
- [ ] Test with fork + wait

---

## Phase 4: Pipe System Call

### 4.1 Understanding Pipes

```
pipe() creates unidirectional communication channel:
- Returns two file descriptors: read end and write end
- Data written to write end can be read from read end
- Used to connect stdout of one process to stdin of another

Process A                    Pipe                    Process B
┌──────────────┐      ┌──────────────────┐      ┌──────────────┐
│              │      │   ┌──────────┐   │      │              │
│ write(fd[1]) │─────▶│   │  Buffer  │   │─────▶│ read(fd[0])  │
│              │      │   └──────────┘   │      │              │
└──────────────┘      └──────────────────┘      └──────────────┘

Shell: ls | grep foo
┌────────────┐         ┌────────────┐
│     ls     │  pipe   │    grep    │
│  stdout=1 ─┼────────▶├─ stdin=0   │
└────────────┘         └────────────┘
```

### 4.2 Pipe Data Structures

```c
// pipe.h

#define PIPE_BUFFER_SIZE 4096

struct pipe {
    char buffer[PIPE_BUFFER_SIZE];
    size_t read_pos;           // Next position to read from
    size_t write_pos;          // Next position to write to
    size_t count;              // Bytes currently in buffer

    int read_open;             // Read end open?
    int write_open;            // Write end open?

    struct process *readers;   // Processes blocked on read
    struct process *writers;   // Processes blocked on write
};

// Pipe vnode operations
struct vnode_ops pipe_read_ops = {
    .read = pipe_read,
    .write = NULL,             // Can't write to read end
    .close = pipe_close_read,
};

struct vnode_ops pipe_write_ops = {
    .read = NULL,              // Can't read from write end
    .write = pipe_write,
    .close = pipe_close_write,
};
```

### 4.3 Pipe Implementation

```c
// syscall: pipe(int pipefd[2])
// Returns: 0 on success, -1 on error
// pipefd[0] = read end, pipefd[1] = write end

int sys_pipe(int pipefd[2]) {
    struct process *p = current_proc;

    // Validate user pointer
    if (!vmm_validate_user_range(pipefd, 2 * sizeof(int))) {
        return -EFAULT;
    }

    // Allocate pipe structure
    struct pipe *pipe = pipe_alloc();
    if (!pipe) return -ENOMEM;

    // Create vnodes for read and write ends
    struct vnode *read_vn = vnode_alloc();
    struct vnode *write_vn = vnode_alloc();
    if (!read_vn || !write_vn) {
        pipe_free(pipe);
        return -ENOMEM;
    }

    read_vn->ops = &pipe_read_ops;
    read_vn->fs_data = pipe;
    write_vn->ops = &pipe_write_ops;
    write_vn->fs_data = pipe;

    // Allocate file descriptors
    int fd_read = vfs_alloc_fd(&p->fds);
    int fd_write = vfs_alloc_fd(&p->fds);
    if (fd_read < 0 || fd_write < 0) {
        // Cleanup...
        return -EMFILE;
    }

    // Set up file descriptors
    p->fds.fds[fd_read].vn = read_vn;
    p->fds.fds[fd_read].flags = O_RDONLY;
    p->fds.fds[fd_write].vn = write_vn;
    p->fds.fds[fd_write].flags = O_WRONLY;

    pipefd[0] = fd_read;
    pipefd[1] = fd_write;

    return 0;
}

// Read from pipe
ssize_t pipe_read(struct vnode *vn, void *buf, size_t count, size_t offset) {
    (void)offset;  // Pipes don't support seeking
    struct pipe *pipe = vn->fs_data;

    while (pipe->count == 0) {
        if (!pipe->write_open) {
            return 0;  // EOF: write end closed, no data
        }
        // Block until data available or write end closed
        sleep(&pipe->readers);
    }

    // Copy data from pipe buffer to user buffer
    size_t to_read = (count < pipe->count) ? count : pipe->count;
    // Handle circular buffer wraparound...

    pipe->count -= to_read;

    // Wake writers if they were blocked
    wakeup(&pipe->writers);

    return to_read;
}

// Write to pipe
ssize_t pipe_write(struct vnode *vn, const void *buf, size_t count, size_t offset) {
    (void)offset;
    struct pipe *pipe = vn->fs_data;

    if (!pipe->read_open) {
        // SIGPIPE would be sent here
        return -EPIPE;
    }

    size_t written = 0;
    while (written < count) {
        while (pipe->count == PIPE_BUFFER_SIZE) {
            if (!pipe->read_open) {
                return -EPIPE;
            }
            // Block until space available
            sleep(&pipe->writers);
        }

        // Copy data to pipe buffer
        size_t space = PIPE_BUFFER_SIZE - pipe->count;
        size_t to_write = (count - written < space) ? count - written : space;
        // Handle circular buffer...

        pipe->count += to_write;
        written += to_write;

        // Wake readers
        wakeup(&pipe->readers);
    }

    return written;
}
```

**Tasks:**
- [ ] Create `pipe.h` with data structures
- [ ] Implement circular buffer operations
- [ ] Implement `pipe_alloc()` / `pipe_free()`
- [ ] Implement `pipe_read()` vnode operation
- [ ] Implement `pipe_write()` vnode operation
- [ ] Implement `pipe_close_read()` / `pipe_close_write()`
- [ ] Implement `sys_pipe()` syscall
- [ ] Add to syscall dispatch table
- [ ] Add userspace wrapper
- [ ] Test with simple pipe program

---

## Phase 5: Dup/Dup2 System Calls

### 5.1 Understanding Dup2

```
dup2(oldfd, newfd) makes newfd refer to same file as oldfd:
- Used for I/O redirection
- If newfd is open, it's closed first

Example: redirect stdout to file
    int fd = open("output.txt", O_WRONLY | O_CREAT);
    dup2(fd, STDOUT_FILENO);  // stdout now goes to file
    close(fd);
    printf("Hello");          // Written to output.txt

Example: set up pipeline (ls | grep)
    int pipefd[2];
    pipe(pipefd);

    if (fork() == 0) {
        // Child: ls
        close(pipefd[0]);           // Close read end
        dup2(pipefd[1], STDOUT);    // stdout -> pipe write
        close(pipefd[1]);
        execve("/bin/ls", ...);
    }

    // Parent: grep
    close(pipefd[1]);              // Close write end
    dup2(pipefd[0], STDIN);        // stdin <- pipe read
    close(pipefd[0]);
    execve("/bin/grep", ...);
```

### 5.2 Dup2 Implementation

```c
// syscall: dup(oldfd)
// Returns: new fd that refers to same file, or -1

int sys_dup(int oldfd) {
    struct fd_table *fdt = process_get_fd_table();

    if (oldfd < 0 || oldfd >= MAX_FDS) return -EBADF;
    if (!fdt->fds[oldfd].vn) return -EBADF;

    // Find lowest available fd
    int newfd = vfs_alloc_fd(fdt);
    if (newfd < 0) return -EMFILE;

    // Copy file descriptor entry
    fdt->fds[newfd] = fdt->fds[oldfd];
    fdt->fds[newfd].vn->refcount++;

    return newfd;
}

// syscall: dup2(oldfd, newfd)
// Returns: newfd on success, -1 on error

int sys_dup2(int oldfd, int newfd) {
    struct fd_table *fdt = process_get_fd_table();

    // Validate oldfd
    if (oldfd < 0 || oldfd >= MAX_FDS) return -EBADF;
    if (!fdt->fds[oldfd].vn) return -EBADF;

    // Validate newfd
    if (newfd < 0 || newfd >= MAX_FDS) return -EBADF;

    // If same, just return
    if (oldfd == newfd) return newfd;

    // Close newfd if open
    if (fdt->fds[newfd].vn) {
        sys_close(newfd);
    }

    // Copy file descriptor entry
    fdt->fds[newfd] = fdt->fds[oldfd];
    fdt->fds[newfd].vn->refcount++;

    return newfd;
}
```

**Tasks:**
- [ ] Implement `sys_dup()` syscall
- [ ] Implement `sys_dup2()` syscall
- [ ] Add to syscall dispatch table
- [ ] Add userspace wrappers
- [ ] Test with I/O redirection

---

## Phase 6: Directory Operations

### 6.1 Required for PATH and Globbing

```c
// For shell to search PATH directories
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

// For globbing (ls *.c)
DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

// For file type checking
int stat(const char *path, struct stat *statbuf);
```

### 6.2 Implementation

```c
// process.h additions
struct process {
    // ...
    char cwd[256];  // Current working directory
};

// syscall: chdir(path)
int sys_chdir(const char *path) {
    // Validate path exists and is directory
    struct tar_file *f = tar_find(path);
    if (!f || !f->is_dir) return -ENOENT;

    strncpy(current_proc->cwd, path, 255);
    return 0;
}

// syscall: getcwd(buf, size)
char *sys_getcwd(char *buf, size_t size) {
    if (!vmm_validate_user_range(buf, size)) return NULL;
    strncpy(buf, current_proc->cwd, size);
    return buf;
}

// syscall: getdents(fd, dirp, count)
// Read directory entries (used by readdir)
int sys_getdents(int fd, struct dirent *dirp, size_t count);
```

**Tasks:**
- [ ] Add `cwd` to process struct
- [ ] Initialize cwd to "/" for new processes
- [ ] Implement `sys_chdir()`
- [ ] Implement `sys_getcwd()`
- [ ] Implement `sys_getdents()` (already planned in VFS_PLAN)
- [ ] Add userspace wrappers
- [ ] Implement opendir/readdir/closedir in libc

---

## Phase 7: Signal Infrastructure (Minimal)

### 7.1 Minimal Signal Support

For a basic shell, we need at minimum:
- SIGCHLD: Sent to parent when child exits (for `wait()`)
- SIGPIPE: Sent when writing to closed pipe

Full signal support is complex. Start minimal.

```c
// signal.h

#define SIGCHLD  17
#define SIGPIPE  13

// Minimal: just track pending signals, no custom handlers yet
struct process {
    // ...
    uint64_t pending_signals;  // Bitmask of pending signals
};

// Check for pending signals on return to userspace
void check_signals(void);
```

**Tasks:**
- [ ] Define signal numbers
- [ ] Add pending_signals to process struct
- [ ] Implement signal delivery on return to userspace
- [ ] Send SIGCHLD when child exits
- [ ] Send SIGPIPE on write to closed pipe
- [ ] Default handlers: ignore SIGCHLD, terminate on SIGPIPE

---

## Phase 8: Shell Implementation

With all kernel infrastructure in place, we can implement the shell.

### 8.1 Shell Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                          DASH SHELL                             │
│                                                                 │
│  Input: "ls -la | grep foo > output.txt"                       │
│                                                                 │
│  ┌─────────────┐      ┌─────────────┐      ┌─────────────┐    │
│  │   LEXER     │ ───▶ │   PARSER    │ ───▶ │  EXECUTOR   │    │
│  └─────────────┘      └─────────────┘      └─────────────┘    │
│        │                    │                     │            │
│        ▼                    ▼                     ▼            │
│  Tokens:               AST:                  Actions:          │
│  [ls][-la][|]          Pipeline {            fork/exec         │
│  [grep][foo]             Cmd1: ls -la        pipe              │
│  [>][output.txt]         Cmd2: grep foo      dup2              │
│                          Redir: > output     wait              │
│                        }                                       │
└─────────────────────────────────────────────────────────────────┘
```

### 8.2 Lexer (Tokenizer)

```c
// lexer.h

enum token_type {
    TOK_WORD,       // command or argument
    TOK_PIPE,       // |
    TOK_REDIR_IN,   // <
    TOK_REDIR_OUT,  // >
    TOK_REDIR_APPEND, // >>
    TOK_BACKGROUND, // &
    TOK_SEMICOLON,  // ;
    TOK_NEWLINE,    // \n
    TOK_EOF,        // end of input
};

struct token {
    enum token_type type;
    char *value;            // For TOK_WORD
};

struct lexer {
    const char *input;
    size_t pos;
    struct token current;
};

void lexer_init(struct lexer *l, const char *input);
struct token lexer_next(struct lexer *l);
struct token lexer_peek(struct lexer *l);
```

```c
// lexer.c

struct token lexer_next(struct lexer *l) {
    // Skip whitespace
    while (l->input[l->pos] == ' ' || l->input[l->pos] == '\t') {
        l->pos++;
    }

    char c = l->input[l->pos];

    if (c == '\0') return (struct token){TOK_EOF, NULL};
    if (c == '\n') { l->pos++; return (struct token){TOK_NEWLINE, NULL}; }
    if (c == '|')  { l->pos++; return (struct token){TOK_PIPE, NULL}; }
    if (c == ';')  { l->pos++; return (struct token){TOK_SEMICOLON, NULL}; }
    if (c == '&')  { l->pos++; return (struct token){TOK_BACKGROUND, NULL}; }
    if (c == '<')  { l->pos++; return (struct token){TOK_REDIR_IN, NULL}; }
    if (c == '>') {
        l->pos++;
        if (l->input[l->pos] == '>') {
            l->pos++;
            return (struct token){TOK_REDIR_APPEND, NULL};
        }
        return (struct token){TOK_REDIR_OUT, NULL};
    }

    // Word (command/argument)
    // Handle quoting: 'single' "double" and \escapes
    return read_word(l);
}

static struct token read_word(struct lexer *l) {
    char buf[256];
    size_t len = 0;

    while (1) {
        char c = l->input[l->pos];

        // End of word
        if (c == '\0' || c == ' ' || c == '\t' || c == '\n' ||
            c == '|' || c == '&' || c == ';' || c == '<' || c == '>') {
            break;
        }

        // Escape next character
        if (c == '\\' && l->input[l->pos + 1]) {
            l->pos++;
            buf[len++] = l->input[l->pos++];
            continue;
        }

        // Single quotes: literal until closing quote
        if (c == '\'') {
            l->pos++;
            while (l->input[l->pos] && l->input[l->pos] != '\'') {
                buf[len++] = l->input[l->pos++];
            }
            if (l->input[l->pos] == '\'') l->pos++;
            continue;
        }

        // Double quotes: allow escapes and variable expansion
        if (c == '"') {
            l->pos++;
            while (l->input[l->pos] && l->input[l->pos] != '"') {
                if (l->input[l->pos] == '\\' && l->input[l->pos + 1]) {
                    l->pos++;
                }
                buf[len++] = l->input[l->pos++];
            }
            if (l->input[l->pos] == '"') l->pos++;
            continue;
        }

        buf[len++] = l->input[l->pos++];
    }

    buf[len] = '\0';
    return (struct token){TOK_WORD, strdup(buf)};
}
```

### 8.3 Parser

```c
// parser.h

struct command {
    char **argv;            // NULL-terminated argument vector
    int argc;
    char *input_file;       // < redirect
    char *output_file;      // > or >> redirect
    int append;             // 1 if >>, 0 if >
    int background;         // 1 if &
};

struct pipeline {
    struct command *commands;
    int num_commands;
};

struct parser {
    struct lexer lexer;
};

void parser_init(struct parser *p, const char *input);
struct pipeline *parse_line(struct parser *p);
void free_pipeline(struct pipeline *pl);
```

```c
// parser.c

// Grammar:
// line     = pipeline (';' pipeline)* ['\n']
// pipeline = command ('|' command)*
// command  = word+ [redirections]

struct pipeline *parse_line(struct parser *p) {
    struct pipeline *pl = malloc(sizeof(struct pipeline));
    pl->commands = NULL;
    pl->num_commands = 0;

    while (1) {
        struct command *cmd = parse_command(p);
        if (!cmd) break;

        // Add to pipeline
        pl->commands = realloc(pl->commands,
                               (pl->num_commands + 1) * sizeof(struct command));
        pl->commands[pl->num_commands++] = *cmd;

        struct token tok = lexer_peek(&p->lexer);
        if (tok.type == TOK_PIPE) {
            lexer_next(&p->lexer);  // consume '|'
            continue;
        }
        break;
    }

    return pl;
}

struct command *parse_command(struct parser *p) {
    struct command *cmd = malloc(sizeof(struct command));
    memset(cmd, 0, sizeof(*cmd));
    cmd->argv = malloc(16 * sizeof(char *));

    while (1) {
        struct token tok = lexer_peek(&p->lexer);

        if (tok.type == TOK_WORD) {
            lexer_next(&p->lexer);
            cmd->argv[cmd->argc++] = tok.value;
        }
        else if (tok.type == TOK_REDIR_IN) {
            lexer_next(&p->lexer);
            tok = lexer_next(&p->lexer);
            cmd->input_file = tok.value;
        }
        else if (tok.type == TOK_REDIR_OUT || tok.type == TOK_REDIR_APPEND) {
            cmd->append = (tok.type == TOK_REDIR_APPEND);
            lexer_next(&p->lexer);
            tok = lexer_next(&p->lexer);
            cmd->output_file = tok.value;
        }
        else if (tok.type == TOK_BACKGROUND) {
            lexer_next(&p->lexer);
            cmd->background = 1;
        }
        else {
            break;
        }
    }

    cmd->argv[cmd->argc] = NULL;
    return (cmd->argc > 0) ? cmd : NULL;
}
```

### 8.4 Executor

```c
// executor.h

int execute_pipeline(struct pipeline *pl);
int execute_command(struct command *cmd);
int execute_builtin(struct command *cmd);

// Built-in commands
int builtin_cd(int argc, char **argv);
int builtin_exit(int argc, char **argv);
int builtin_export(int argc, char **argv);
int builtin_echo(int argc, char **argv);
int builtin_pwd(int argc, char **argv);
```

```c
// executor.c

int execute_pipeline(struct pipeline *pl) {
    if (pl->num_commands == 0) return 0;

    // Single command (no pipes)
    if (pl->num_commands == 1) {
        return execute_command(&pl->commands[0]);
    }

    // Multiple commands: set up pipes
    int num_pipes = pl->num_commands - 1;
    int pipefds[num_pipes][2];

    // Create all pipes
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipefds[i]) < 0) {
            perror("pipe");
            return -1;
        }
    }

    // Fork each command
    pid_t pids[pl->num_commands];
    for (int i = 0; i < pl->num_commands; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            return -1;
        }

        if (pids[i] == 0) {
            // Child process

            // Set up input from previous pipe (except first command)
            if (i > 0) {
                dup2(pipefds[i-1][0], STDIN_FILENO);
            }

            // Set up output to next pipe (except last command)
            if (i < num_pipes) {
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            // Close all pipe fds
            for (int j = 0; j < num_pipes; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }

            // Handle redirections
            setup_redirections(&pl->commands[i]);

            // Execute
            exec_or_builtin(&pl->commands[i]);
            exit(127);  // exec failed
        }
    }

    // Parent: close all pipes
    for (int i = 0; i < num_pipes; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    // Wait for all children
    int status;
    for (int i = 0; i < pl->num_commands; i++) {
        waitpid(pids[i], &status, 0);
    }

    return WEXITSTATUS(status);
}

void setup_redirections(struct command *cmd) {
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            perror(cmd->input_file);
            exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->output_file) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->append ? O_APPEND : O_TRUNC;
        int fd = open(cmd->output_file, flags, 0644);
        if (fd < 0) {
            perror(cmd->output_file);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

void exec_or_builtin(struct command *cmd) {
    // Check for builtins first
    if (execute_builtin(cmd) >= 0) {
        exit(0);  // Builtin executed successfully
    }

    // External command: search PATH
    char *path = find_in_path(cmd->argv[0]);
    if (!path) {
        fprintf(stderr, "%s: command not found\n", cmd->argv[0]);
        exit(127);
    }

    execve(path, cmd->argv, environ);
    perror(cmd->argv[0]);
    exit(126);
}
```

### 8.5 Built-in Commands

```c
// builtins.c

struct builtin {
    const char *name;
    int (*func)(int argc, char **argv);
};

struct builtin builtins[] = {
    {"cd",     builtin_cd},
    {"exit",   builtin_exit},
    {"export", builtin_export},
    {"pwd",    builtin_pwd},
    {"echo",   builtin_echo},
    {NULL, NULL}
};

int execute_builtin(struct command *cmd) {
    for (struct builtin *b = builtins; b->name; b++) {
        if (strcmp(cmd->argv[0], b->name) == 0) {
            return b->func(cmd->argc, cmd->argv);
        }
    }
    return -1;  // Not a builtin
}

int builtin_cd(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : getenv("HOME");
    if (chdir(path) < 0) {
        perror("cd");
        return 1;
    }
    return 0;
}

int builtin_exit(int argc, char **argv) {
    int code = (argc > 1) ? atoi(argv[1]) : 0;
    exit(code);
    return 0;  // Never reached
}

int builtin_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    char buf[256];
    if (getcwd(buf, sizeof(buf))) {
        printf("%s\n", buf);
        return 0;
    }
    perror("pwd");
    return 1;
}

int builtin_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

int builtin_export(int argc, char **argv) {
    if (argc < 2) {
        // Print all environment variables
        extern char **environ;
        for (char **env = environ; *env; env++) {
            printf("%s\n", *env);
        }
        return 0;
    }

    // Set variable: export VAR=value
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
        }
    }
    return 0;
}
```

### 8.6 Main Shell Loop

```c
// dash.c

char *environ_init[] = {
    "PATH=/bin",
    "HOME=/",
    "SHELL=/bin/dash",
    NULL
};

char **environ = environ_init;

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    char line[1024];
    struct parser parser;

    while (1) {
        // Print prompt
        printf("$ ");
        fflush(stdout);

        // Read line
        if (!fgets(line, sizeof(line), stdin)) {
            break;  // EOF
        }

        // Parse and execute
        parser_init(&parser, line);
        struct pipeline *pl = parse_line(&parser);

        if (pl && pl->num_commands > 0) {
            execute_pipeline(pl);
        }

        free_pipeline(pl);
    }

    return 0;
}
```

---

## Phase 9: Advanced Features (Future)

### 9.1 Control Structures

```sh
# if/then/else
if [ -f file ]; then
    echo "exists"
else
    echo "not found"
fi

# while
while read line; do
    echo "$line"
done < file

# for
for f in *.c; do
    echo "$f"
done

# case
case "$1" in
    start) do_start;;
    stop)  do_stop;;
    *)     echo "usage";;
esac
```

### 9.2 Variable Expansion

```sh
$VAR          # Simple expansion
${VAR}        # Braces (for clarity)
${VAR:-def}   # Default if unset
${VAR:=def}   # Assign default if unset
${#VAR}       # String length
${VAR%pat}    # Remove shortest suffix
${VAR%%pat}   # Remove longest suffix
${VAR#pat}    # Remove shortest prefix
${VAR##pat}   # Remove longest prefix
```

### 9.3 Globbing

```sh
*             # Match any characters
?             # Match single character
[abc]         # Match any of a, b, c
[a-z]         # Match range
[!abc]        # Match anything except a, b, c
```

### 9.4 Job Control

```sh
command &     # Run in background
jobs          # List background jobs
fg %1         # Bring job 1 to foreground
bg %1         # Continue job 1 in background
Ctrl+Z        # Suspend foreground job
```

---

## Implementation Order Summary

```
Phase 0: Scheduler Foundation          [~2-3 weeks]
  - Process table
  - Kernel stacks per process
  - Round-robin scheduler
  - Context switching

Phase 1: fork()                        [~2-3 weeks]
  - Address space copying
  - File descriptor copying
  - Child return value setup

Phase 2: exec()                        [~2 weeks]
  - Argument/environment passing
  - Address space replacement
  - Stack setup

Phase 3: wait()/waitpid()             [~1-2 weeks]
  - Zombie processes
  - Parent notification
  - Exit code collection

Phase 4: pipe()                        [~1-2 weeks]
  - Pipe buffer implementation
  - Blocking read/write
  - Close handling

Phase 5: dup/dup2()                   [~1 week]
  - File descriptor duplication
  - I/O redirection support

Phase 6: Directory operations         [~1-2 weeks]
  - chdir/getcwd
  - getdents
  - readdir wrapper

Phase 7: Minimal signals              [~1 week]
  - SIGCHLD
  - SIGPIPE

Phase 8: Shell implementation         [~3-4 weeks]
  - Lexer
  - Parser
  - Pipeline executor
  - Built-in commands

                              Total: ~15-20 weeks
```

## Complexity Estimates

| Component | Lines of Code | Complexity |
|-----------|---------------|------------|
| Process table + scheduler | 400-600 | High |
| fork() syscall | 300-500 | Very High |
| exec() syscall | 300-400 | High |
| wait() syscall | 150-250 | Medium |
| pipe() syscall | 200-300 | Medium |
| dup/dup2() | 50-100 | Low |
| Directory ops | 150-200 | Medium |
| Signal basics | 100-200 | Medium |
| Shell lexer | 200-300 | Medium |
| Shell parser | 300-400 | Medium |
| Shell executor | 300-400 | High |
| Shell builtins | 200-300 | Low |
| **Total** | **~2500-4000** | |

## Testing Strategy

### Unit Tests

```c
// test_fork.c
void test_fork_returns_different_values() {
    pid_t pid = fork();
    if (pid == 0) {
        // Child
        assert(getpid() != getppid());
        exit(42);
    } else {
        // Parent
        assert(pid > 0);
        int status;
        wait(&status);
        assert(WEXITSTATUS(status) == 42);
    }
}
```

### Integration Tests

```sh
# test_pipe.sh
echo "hello" | cat
# Expected: hello

# test_redirect.sh
echo "test" > /tmp/out
cat < /tmp/out
# Expected: test

# test_pipeline.sh
echo -e "c\na\nb" | sort | head -1
# Expected: a
```

## References

- [xv6 Operating System](https://github.com/mit-pdos/xv6-public) - Excellent reference implementation
- [Dash Source Code](https://git.kernel.org/pub/scm/utils/dash/dash.git)
- [POSIX Shell Specification](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html)
- [The Linux Programming Interface](https://man7.org/tlpi/) - Comprehensive Unix API reference
- [OSDev Wiki - Process](https://wiki.osdev.org/Processes_and_Threads)
- [Writing a Unix Shell](https://brennan.io/2015/01/16/write-a-shell-in-c/)
