# Calling Userspace

This document explains how Seed OS transitions from kernel mode (ring 0) to user mode (ring 3) to run a program, and how it returns when the program exits.

## Overview

Running a user program involves three phases:

1. **Setup** - Allocate memory and prepare the execution environment
2. **Enter Userspace** - Switch to ring 3 and jump to the program
3. **Return to Kernel** - Handle sys_exit and resume kernel execution

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Kernel (Ring 0)                             │
│                                                                     │
│   programs_run("hello")                                             │
│         │                                                           │
│         ▼                                                           │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐            │
│   │   Allocate  │───▶│  Map Pages  │───▶│ Copy Code   │            │
│   │   Memory    │    │             │    │             │            │
│   └─────────────┘    └─────────────┘    └─────────────┘            │
│         │                                                           │
│         ▼                                                           │
│   context_switch_to_user(&ctx)                                      │
│         │                                                           │
│         │  ┌─────────────────────────────────┐                     │
│         │  │  Save kernel state              │                     │
│         │  │  Set TSS.RSP0                   │                     │
│         │  │  Switch address space (CR3)     │                     │
│         │  │  Build IRETQ frame              │                     │
│         │  │  Execute IRETQ ─────────────────┼──────┐              │
│         │  └─────────────────────────────────┘      │              │
│         │                                           │              │
│         │                                           ▼              │
│         │                              ┌────────────────────────┐  │
│         │                              │   User Program (Ring 3)│  │
│         │                              │                        │  │
│         │                              │   "Hello from user!"   │  │
│         │                              │   sys_write(...)       │  │
│         │                              │   sys_exit(0)          │  │
│         │                              └───────────┬────────────┘  │
│         │                                          │               │
│         │  ┌─────────────────────────────────┐     │               │
│         │  │  INT 0x80 (syscall)             │◀────┘               │
│         │  │  syscall_handler()              │                     │
│         │  │  sys_exit():                    │                     │
│         │  │    - Switch to kernel PML4      │                     │
│         │  │    - context_return_to_kernel() │                     │
│         │  └─────────────────────────────────┘                     │
│         │                                                           │
│         ▼                                                           │
│   context_switch_to_user() returns                                  │
│         │                                                           │
│         ▼                                                           │
│   Cleanup and return 0                                              │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

## Phase 1: Setup

Before entering userspace, the kernel must prepare an isolated execution environment.

### Memory Allocation

The `programs_run()` function allocates three pieces of memory:

```c
uint64_t memory = vmm_create_address_space();  // New PML4 (page tables)
uint64_t code_page_phys = pmm_alloc();         // Physical page for code
uint64_t stack_page_phys = pmm_alloc();        // Physical page for stack
```

**Why three allocations?**

| Allocation | Purpose | Size |
|------------|---------|------|
| PML4 | Root of user's page tables, provides address space isolation | 4 KB |
| Code page | Holds the user program's executable code | 4 KB |
| Stack page | User's stack for local variables, function calls | 4 KB |

### Address Space Creation

`vmm_create_address_space()` creates a new PML4 table:

1. Allocates a 4 KB page for the PML4
2. Zeros it out
3. Copies entries 256-511 from the kernel's PML4

```
New User PML4:
┌─────────────────────────────────────────┐
│  Entry 0-255:   Empty (user space)      │  ← Will be populated
│  Entry 256-511: Copied from kernel      │  ← Kernel always accessible
└─────────────────────────────────────────┘
```

This ensures the kernel is mapped in every address space, so interrupt handlers and syscalls work regardless of which process is running.

### Page Mapping

The code and stack pages are mapped into the user's address space:

```c
vmm_map_page(memory, USER_CODE_BASE,  code_page_phys,  PTE_PRESENT | PTE_USER | PTE_WRITABLE);
vmm_map_page(memory, USER_STACK_BASE, stack_page_phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
```

**Address Layout:**

```
User's Virtual Address Space:
┌─────────────────────────────────────────┐ 0xFFFFFFFFFFFFFFFF
│                                         │
│            Kernel (shared)              │
│                                         │
├─────────────────────────────────────────┤ 0xFFFF800000000000
│                                         │
│        (unmapped / non-canonical)       │
│                                         │
├─────────────────────────────────────────┤ 0x00007FFFFFFFFFFF
│                                         │
│              (unmapped)                 │
│                                         │
├─────────────────────────────────────────┤ USER_STACK_BASE + 0x1000
│         Stack Page (4 KB)               │  ← RSP starts here (top)
├─────────────────────────────────────────┤ USER_STACK_BASE (0x7FFFFF000)
│                                         │
│              (unmapped)                 │
│                                         │
├─────────────────────────────────────────┤ USER_CODE_BASE + 0x1000
│         Code Page (4 KB)                │  ← Program loaded here
├─────────────────────────────────────────┤ USER_CODE_BASE (0x400000)
│                                         │
│              (unmapped)                 │
│                                         │
└─────────────────────────────────────────┘ 0x0000000000000000
```

**Page Table Entry Flags:**

| Flag | Meaning |
|------|---------|
| `PTE_PRESENT` | Page is valid and mapped |
| `PTE_USER` | Ring 3 code can access this page |
| `PTE_WRITABLE` | Page is read-write (needed for stack) |

### Copying the Program

The program code is copied from the kernel's embedded binary to the allocated page:

```c
void *code_page_virt = phys_to_virt(code_page_phys);
memcpy(code_page_virt, prog->code, prog->code_len);
```

**Why `phys_to_virt()`?**

The kernel cannot write directly to `USER_CODE_BASE` (0x400000) because that address isn't mapped in the kernel's address space. Instead, it uses the Higher Half Direct Map (HHDM):

```
Physical Address:  0x00000000_00123000  (code_page_phys)
                          │
                          │  phys_to_virt()
                          ▼
HHDM Virtual:      0xFFFF8000_00123000  (code_page_virt)
                          │
                          │  Kernel writes program here
                          ▼
User sees at:      0x00000000_00400000  (USER_CODE_BASE)
```

Both addresses map to the same physical page, just through different page table entries.

### Context Structure

The kernel prepares a `user_context` structure:

```c
struct user_context ctx;
ctx.pml4  = memory;                           // User's page tables
ctx.entry = USER_CODE_BASE;                   // Where to start executing
ctx.stack = USER_STACK_BASE + USER_STACK_SIZE; // Top of stack (grows down)
```

## Phase 2: Enter Userspace

The `context_switch_to_user()` function performs the actual ring transition.

### Step 1: Save Kernel State

```asm
pushq %rbp
pushq %rbx
pushq %r12
pushq %r13
pushq %r14
pushq %r15
movq %rsp, saved_kernel_rsp(%rip)
```

The kernel saves all callee-saved registers and RSP. This allows `context_return_to_kernel()` to restore state and "return" from this function later.

### Step 2: Set TSS.RSP0

```asm
movq %rsp, %rdi
call tss_set_kernel_stack
```

When an interrupt occurs in ring 3, the CPU needs a kernel stack. It reads RSP0 from the Task State Segment (TSS). Without this, interrupts in userspace would crash.

```
TSS Structure:
┌─────────────────────┐
│  Reserved           │
├─────────────────────┤
│  RSP0 ◄─────────────┼── CPU loads this as RSP when
│  (kernel stack)     │   transitioning ring 3 → ring 0
├─────────────────────┤
│  RSP1, RSP2         │
│  (unused)           │
├─────────────────────┤
│  ...                │
└─────────────────────┘
```

### Step 3: Switch Address Space

```asm
movq 0(%r12), %rdi        ; ctx->pml4
call vmm_switch_address_space
```

This writes the user's PML4 physical address to CR3, activating the user's page tables. After this point:

- User addresses (0x400000, etc.) are valid
- Kernel addresses still work (because we copied kernel mappings)
- The TLB is flushed (old translations invalidated)

### Step 4: Build IRETQ Frame

```asm
pushq $0x23             ; SS  = GDT_USER_DATA | RPL3
pushq %r8               ; RSP = user stack
pushq $0x202            ; RFLAGS with IF=1 (interrupts enabled)
pushq $0x1b             ; CS  = GDT_USER_CODE | RPL3
pushq %rcx              ; RIP = entry point
```

The IRETQ instruction expects this exact stack layout:

```
Stack before IRETQ:
┌─────────────────┐  ← RSP
│  SS    (0x23)   │  User data segment, ring 3
├─────────────────┤
│  RSP   (user)   │  User's stack pointer
├─────────────────┤
│  RFLAGS (0x202) │  IF=1 (interrupts enabled)
├─────────────────┤
│  CS    (0x1B)   │  User code segment, ring 3
├─────────────────┤
│  RIP   (entry)  │  Where to start executing
└─────────────────┘

After IRETQ:
- CPU pops these values into respective registers
- CS:RIP causes jump to user code
- CPL changes from 0 to 3 (ring transition!)
```

**Segment Selector Values:**

| Selector | Value | Meaning |
|----------|-------|---------|
| `0x1B` | `0x18 \| 3` | GDT entry 3 (user code), RPL=3 |
| `0x23` | `0x20 \| 3` | GDT entry 4 (user data), RPL=3 |

### Step 5: Enter Ring 3

```asm
xorq %rax, %rax         ; Clear all general-purpose registers
xorq %rbx, %rbx         ; (don't leak kernel data to userspace)
; ... clear all registers ...
iretq                   ; Jump to user code!
```

The IRETQ instruction:
1. Pops RIP, CS, RFLAGS, RSP, SS from the stack
2. Changes CPL (Current Privilege Level) from 0 to 3
3. Begins executing at the user's entry point

**The user program is now running!**

## Phase 3: Return to Kernel

When the user program calls `sys_exit()`, execution returns to the kernel.

### System Call Entry

The user program triggers a syscall via `INT 0x80`:

```asm
; In user program:
mov $0, %rax      ; SYS_EXIT = 0
mov $0, %rdi      ; exit code = 0
int $0x80         ; trigger syscall
```

The CPU:
1. Finds IDT entry 0x80
2. Verifies DPL allows ring 3 to call it
3. Loads RSP from TSS.RSP0 (kernel stack!)
4. Pushes interrupt frame (SS, RSP, RFLAGS, CS, RIP)
5. Jumps to the syscall handler

### Syscall Handler

```c
void syscall_handler(struct syscall_registers *regs) {
    switch (regs->rax) {
        case SYS_EXIT:
            sys_exit(regs->rdi);
            break;
        // ...
    }
}
```

### sys_exit Implementation

```c
static void sys_exit(uint64_t exit_code) {
    // Switch back to kernel address space
    vmm_switch_address_space(vmm_get_kernel_pml4());

    // Return to where context_switch_to_user was called
    context_return_to_kernel();
}
```

### context_return_to_kernel

```asm
context_return_to_kernel:
    movq saved_kernel_rsp(%rip), %rsp    ; Restore kernel stack
    sti                                   ; Re-enable interrupts
    jmp context_resume_from_user          ; Continue cleanup

context_resume_from_user:
    popq %r15                             ; Restore callee-saved registers
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret                                   ; Return to programs_run()!
```

This restores the kernel stack pointer that was saved when entering userspace. The `ret` instruction returns to wherever `context_switch_to_user()` was called - back in `programs_run()`.

## The "Coroutine" Pattern

From `programs_run()`'s perspective, `context_switch_to_user()` behaves like a blocking function call:

```c
int programs_run(const char *name) {
    // ... setup ...

    context_switch_to_user(&ctx);  // "Blocks" until sys_exit

    // Execution resumes here after user program exits!
    return 0;
}
```

This is similar to a coroutine:
- The kernel "yields" to the user program
- The user program runs until it yields back (via sys_exit)
- The kernel resumes where it left off

## Memory Summary

Here's a complete picture of what gets allocated and mapped:

```
Physical Memory                    Virtual Memory
───────────────                    ──────────────

┌─────────────┐                    Kernel's View (kernel PML4):
│ PML4 page   │ ◄─────────────────┬─ Not directly mapped
│ (4 KB)      │                   │  (only used as CR3 value)
└─────────────┘                   │
                                  │
┌─────────────┐                   │  User's View (user PML4 active):
│ Code page   │ ◄─────────────────┼─ 0x400000 (USER_CODE_BASE)
│ (4 KB)      │                   │
└─────────────┘                   │  Kernel's View (kernel PML4 active):
      │                           └─ HHDM + phys_addr (for writing)
      │
      │ phys_to_virt()
      ▼
┌─────────────┐
│ HHDM maps   │ ◄──── All physical memory accessible via HHDM
│ all phys    │       at 0xFFFF800000000000 + physical_address
└─────────────┘

┌─────────────┐                    User's View:
│ Stack page  │ ◄──────────────── 0x7FFFFF000 (USER_STACK_BASE)
│ (4 KB)      │
└─────────────┘
```

## Security Considerations

The ring 0 → ring 3 transition provides hardware-enforced isolation:

| Protection | Mechanism |
|------------|-----------|
| Memory isolation | Separate page tables (PML4), PTE_USER flag |
| Privilege separation | CPL in CS, DPL in segment descriptors |
| Controlled entry | Syscalls only via INT 0x80 (IDT gate) |
| Stack isolation | TSS.RSP0 provides kernel stack for interrupts |

**Current limitations (TODO):**
- User pointers in syscalls are not validated
- No memory cleanup after program exit (pages are leaked)
- Single process only (no scheduler)

## Related Documentation

- [VMM.md](VMM.md) - Virtual memory and paging details
- [SYSCALLS.md](SYSCALLS.md) - System call interface
- [IRQ.md](IRQ.md) - Interrupt handling and the IDT
