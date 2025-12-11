# System Calls: The Bridge Between Userspace and the Kernel

## Table of Contents

1. [What Are System Calls?](#what-are-system-calls)
2. [Syscalls vs Interrupts: Similarities and Differences](#syscalls-vs-interrupts-similarities-and-differences)
3. [How Major Operating Systems Implement Syscalls](#how-major-operating-systems-implement-syscalls)
4. [Our Design Decisions](#our-design-decisions)
5. [Implementation Walkthrough](#implementation-walkthrough)
6. [Adding a New Syscall](#adding-a-new-syscall)

---

## What Are System Calls?

System calls (syscalls) are the fundamental interface between user programs and the operating system kernel. They provide a controlled gateway for unprivileged code (ring 3) to request privileged operations from the kernel (ring 0).

### Why Do We Need Them?

Modern operating systems enforce a strict separation between userspace and kernel space:

```
┌─────────────────────────────────────────────────────────────┐
│                    USERSPACE (Ring 3)                       │
│                                                             │
│   Your program cannot:                                      │
│   - Access hardware directly (disk, network, display)       │
│   - Read/write other process's memory                       │
│   - Execute privileged CPU instructions                     │
│   - Access kernel memory                                    │
│                                                             │
│   Your program CAN:                                         │
│   - Execute normal CPU instructions                         │
│   - Access its own allocated memory                         │
│   - Make system calls to request kernel services            │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                     KERNEL (Ring 0)                         │
│                                                             │
│   The kernel can:                                           │
│   - Access all hardware                                     │
│   - Read/write any memory                                   │
│   - Execute all CPU instructions                            │
│   - Manage processes, memory, devices, filesystems          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

Without syscalls, user programs would have no way to:
- Print text to the screen
- Read files from disk
- Send data over the network
- Allocate memory
- Create new processes

### Common System Calls

Every OS provides syscalls for essential operations:

| Category | Examples |
|----------|----------|
| Process Control | `exit`, `fork`, `exec`, `wait` |
| File I/O | `open`, `read`, `write`, `close`, `lseek` |
| Memory Management | `mmap`, `munmap`, `brk` |
| Inter-Process Communication | `pipe`, `shmget`, `socket` |
| Device I/O | `ioctl` |

---

## Syscalls vs Interrupts: Similarities and Differences

System calls and hardware interrupts share the same CPU mechanism but serve different purposes.

### Similarities

Both syscalls and interrupts:

1. **Use the same CPU mechanism**: The CPU saves state, switches to ring 0, and jumps to a handler address looked up in the Interrupt Descriptor Table (IDT).

2. **Cause a privilege level transition**: Both can elevate from ring 3 to ring 0.

3. **Save similar CPU state**: SS, RSP, RFLAGS, CS, RIP are pushed to the kernel stack.

4. **Return via IRETQ**: Both use the same instruction to restore state and return.

### Differences

| Aspect | Hardware Interrupts | System Calls |
|--------|---------------------|--------------|
| **Trigger** | External hardware (keyboard, timer, disk) | User program executes special instruction |
| **Timing** | Asynchronous - can happen anytime | Synchronous - happens exactly when called |
| **Predictability** | Kernel must handle at any point | Kernel knows user is waiting |
| **Purpose** | Notify kernel of hardware events | Request kernel services |
| **IDT Entry DPL** | DPL=0 (only kernel can trigger) | DPL=3 (user can trigger) |

### The Key Insight

Syscalls are essentially **software-triggered interrupts** with special privileges. The x86 `INT` instruction causes a software interrupt, and by setting DPL=3 on the IDT entry, we allow userspace to trigger it.

```
Hardware Interrupt Flow:
  [Keyboard] → IRQ1 → PIC → CPU → IDT[33] → keyboard_handler()

System Call Flow:
  [User Program] → INT 0x80 → CPU → IDT[128] → syscall_handler()
```

---

## How Major Operating Systems Implement Syscalls

### Linux

**Legacy: INT 0x80 (i386)**
```asm
; Linux syscall via INT 0x80
mov eax, 4          ; sys_write
mov ebx, 1          ; fd = stdout
mov ecx, message    ; buffer
mov edx, 13         ; length
int 0x80            ; invoke kernel
```

**Modern: SYSCALL/SYSRET (x86-64)**

Modern Linux uses the `SYSCALL` instruction instead of `INT 0x80` for significant performance gains:

```asm
; Linux syscall via SYSCALL instruction
mov rax, 1          ; sys_write
mov rdi, 1          ; fd = stdout
mov rsi, message    ; buffer
mov rdx, 13         ; length
syscall             ; fast syscall entry
```

Why `SYSCALL` is faster:
- Doesn't go through the IDT
- Uses Model-Specific Registers (MSRs) for handler address
- Minimal state saving (no automatic stack switch)
- ~25-30% faster than INT 0x80

**Linux Syscall Numbering**

Linux has hundreds of syscalls (over 300 on x86-64). They're defined in:
- `arch/x86/entry/syscalls/syscall_64.tbl`

Common Linux syscall numbers:
| Number | Name | Description |
|--------|------|-------------|
| 0 | read | Read from file descriptor |
| 1 | write | Write to file descriptor |
| 2 | open | Open file |
| 3 | close | Close file descriptor |
| 60 | exit | Terminate process |

### macOS (XNU Kernel)

macOS uses `SYSCALL` instruction on x86-64 (same as Linux) but with different syscall numbers and conventions:

```asm
; macOS syscall
mov rax, 0x2000004  ; sys_write (0x2000000 + BSD syscall number)
mov rdi, 1          ; fd = stdout
mov rsi, message    ; buffer
mov rdx, 13         ; length
syscall
```

The `0x2000000` prefix indicates the BSD syscall class. macOS has multiple syscall classes:
- `0x0000000` - Mach traps
- `0x1000000` - Mach messages
- `0x2000000` - BSD syscalls
- `0x3000000` - Machine-dependent

### Windows

Windows uses a completely different approach:

1. **User programs don't call syscalls directly** - They call Win32 API functions in DLLs (kernel32.dll, ntdll.dll)

2. **ntdll.dll is the syscall gateway** - It contains stubs that perform the actual syscall

3. **Syscall numbers change between versions** - Windows doesn't guarantee stable syscall numbers, so programs must go through the official API

```
Application
     ↓
kernel32.dll (Win32 API)
     ↓
ntdll.dll (Native API)
     ↓
SYSCALL instruction → ntoskrnl.exe (Windows Kernel)
```

Windows syscall mechanism:
```asm
; Windows syscall (inside ntdll.dll)
mov r10, rcx        ; Windows uses r10 instead of rcx
mov eax, 0x25       ; NtWriteFile syscall number (varies by version!)
syscall
```

### Comparison Summary

| OS | Mechanism | Syscall Number in | Args in |
|----|-----------|-------------------|---------|
| Linux (32-bit) | INT 0x80 | EAX | EBX, ECX, EDX, ESI, EDI, EBP |
| Linux (64-bit) | SYSCALL | RAX | RDI, RSI, RDX, R10, R8, R9 |
| macOS | SYSCALL | RAX | RDI, RSI, RDX, R10, R8, R9 |
| Windows | SYSCALL | EAX | RCX, RDX, R8, R9, stack |
| **Our OS** | INT 0x80 | RAX | RDI, RSI, RDX, RCX |

---

## Our Design Decisions

### Why INT 0x80?

We chose `INT 0x80` over `SYSCALL` for several reasons:

1. **Simplicity**: INT 0x80 reuses the existing interrupt infrastructure (IDT, iretq). SYSCALL requires setting up MSRs and handling SYSRET correctly.

2. **Educational value**: Understanding INT 0x80 teaches the fundamentals of privilege transitions that apply to all interrupt-based mechanisms.

3. **Debugging**: INT 0x80 goes through the same path as other interrupts, making debugging consistent.

4. **Sufficient for learning**: The performance difference doesn't matter for an educational OS.

### Our Calling Convention

We follow the Linux x86-64 convention (with INT 0x80):

```
┌─────────────────────────────────────────────────────────┐
│  Register  │  Purpose                                   │
├────────────┼────────────────────────────────────────────┤
│  RAX       │  Syscall number (input), return value (out)│
│  RDI       │  Argument 1                                │
│  RSI       │  Argument 2                                │
│  RDX       │  Argument 3                                │
│  RCX       │  Argument 4                                │
└─────────────────────────────────────────────────────────┘
```

### Our Syscalls

We've implemented two syscalls so far:

| Number | Name | Arguments | Description |
|--------|------|-----------|-------------|
| 0 | SYS_EXIT | rdi=exit_code | Terminate process |
| 1 | SYS_WRITE | rdi=fd, rsi=buf, rdx=count | Write to stdout |

---

## Implementation Walkthrough

Let's trace through exactly what happens when a user program calls `sys_write`.

### Step 1: User Program Prepares Registers

In userspace, the program loads syscall number and arguments into registers:

```asm
; From the hardcoded user program (disassembled user_program.c)
mov rax, 1          ; SYS_WRITE = 1
mov rdi, 1          ; fd = 1 (stdout)
lea rsi, [rip+0x1b] ; buffer = "Hello from userspace!\n"
mov rdx, 22         ; count = 22 bytes
int 0x80            ; TRAP TO KERNEL
```

The bytes in `user_program.c`:
```c
unsigned char user_bin[] = {
  0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00,  // mov rax, 1
  0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00,  // mov rdi, 1
  0x48, 0x8d, 0x35, 0x1b, 0x00, 0x00, 0x00,  // lea rsi, [rip+0x1b]
  0x48, 0xc7, 0xc2, 0x16, 0x00, 0x00, 0x00,  // mov rdx, 22
  0xcd, 0x80,                                 // int 0x80
  // ... sys_exit follows ...
  0x48, 0x65, 0x6c, 0x6c, 0x6f, ...          // "Hello from userspace!\n"
};
```

### Step 2: CPU Handles INT 0x80

When the CPU executes `INT 0x80`:

1. **Privilege check**: CPU checks IDT[128].DPL >= CPL. Since DPL=3 and we're in ring 3 (CPL=3), the check passes.

2. **Stack switch**: CPU reads RSP0 from TSS and switches to kernel stack.

3. **Push interrupt frame**: CPU pushes SS, RSP, RFLAGS, CS, RIP onto kernel stack.

4. **Load new CS:RIP**: CPU loads handler address from IDT[128] and jumps there.

```
User Stack (Ring 3)          Kernel Stack (Ring 0)
┌─────────────┐              ┌─────────────┐
│    ...      │              │     SS      │ ← Old stack segment
├─────────────┤              ├─────────────┤
│    ...      │              │    RSP      │ ← Old stack pointer
└─────────────┘              ├─────────────┤
                             │   RFLAGS    │ ← Old flags
                             ├─────────────┤
                             │     CS      │ ← Old code segment
                             ├─────────────┤
                             │    RIP      │ ← Return address
                             └─────────────┘
```

### Step 3: IDT Entry Configuration

The syscall IDT entry is set up in `idt.c:185`:

```c
/* Set up syscall handler at INT 0x80 (Linux-compatible) */
/* DPL=3 so userspace can trigger it with INT 0x80 instruction */
idt_set_entry(SYSCALL_VECTOR, isr_128, IDT_GATE_USER);
```

Where `IDT_GATE_USER` is defined in `idt.h:97`:

```c
#define IDT_GATE_USER  0xEE  /* P=1, DPL=3, Type=0xE (user-callable interrupt) */
```

The type_attr byte breakdown:
```
0xEE = 1110 1110 binary
       │││└─┴┴┴┴── Gate Type = 0xE (interrupt gate)
       ││└──────── Always 0 for interrupt gates
       └┴───────── DPL = 3 (ring 3 can invoke this)
       └────────── P = 1 (present)
```

### Step 4: Assembly Stub Saves Registers

The CPU jumps to `isr_128` in `isr.S:114-120`:

```asm
.macro ISR_SYSCALL num
.global isr_\num
isr_\num:
    pushq $0            /* Dummy error code */
    pushq $\num         /* Interrupt number */
    jmp isr_common_syscall
.endm

ISR_SYSCALL 128  /* Creates isr_128 */
```

Then `isr_common_syscall` (isr.S:60-88):

```asm
isr_common_syscall:
    /* Save registers we care about for syscalls */
    pushq %rax          ; Syscall number
    pushq %rbx          ; Preserved
    pushq %rcx          ; Arg 4
    pushq %rdx          ; Arg 3
    pushq %rsi          ; Arg 2
    pushq %rdi          ; Arg 1

    /* RSP now points to struct syscall_registers */
    movq %rsp, %rdi     ; First C argument = pointer to registers

    call syscall_handler

    /* Restore registers (RAX may have been modified with return value) */
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax           ; Contains return value now

    addq $16, %rsp      ; Remove error code and interrupt number
    iretq               ; Return to userspace
```

The stack after pushes matches `struct syscall_registers`:

```c
struct syscall_registers {
    uint64_t rdi;   // [RSP+0]  Arg 1
    uint64_t rsi;   // [RSP+8]  Arg 2
    uint64_t rdx;   // [RSP+16] Arg 3
    uint64_t rcx;   // [RSP+24] Arg 4
    uint64_t rbx;   // [RSP+32] Preserved
    uint64_t rax;   // [RSP+40] Syscall number / return value
};
```

### Step 5: C Dispatcher Routes the Call

`syscall_handler()` in `syscall.c:112-135` receives a pointer to the saved registers:

```c
void syscall_handler(struct syscall_registers *regs) {
    /* Debug output */
    puts("Syscall invoked: ");
    put_dec(regs->rax);
    puts("\n");

    switch (regs->rax) {
        case SYS_EXIT:   // 0
            sys_exit(regs->rdi);
            /* Never returns */
            break;

        case SYS_WRITE:  // 1
            regs->rax = sys_write(regs->rdi, regs->rsi, regs->rdx);
            break;

        default:
            puts("Unknown syscall: ");
            put_dec(regs->rax);
            puts("\n");
            regs->rax = SYSCALL_ERROR;  // -1
            break;
    }
}
```

### Step 6: sys_write Implementation

`sys_write()` in `syscall.c:78-92`:

```c
static uint64_t sys_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    const char *buf = (const char *)buffer;

    /* Only stdout supported */
    if (fd != FD_STDOUT) {
        return 0;
    }

    /* Write each character to console */
    for (uint64_t i = 0; i < count; i++) {
        putc(buf[i]);  // Outputs to serial + framebuffer
    }

    return count;  // Return bytes written
}
```

**Security Note**: This implementation trusts the user-provided `buffer` pointer. A production OS must validate that:
1. The pointer is in valid user memory
2. The entire buffer range is accessible
3. The buffer doesn't overlap kernel memory

### Step 7: Return Value and IRETQ

After `syscall_handler()` returns:

1. The assembly stub restores registers (RAX now contains return value)
2. `addq $16, %rsp` removes error code and interrupt number
3. `iretq` pops RIP, CS, RFLAGS, RSP, SS and returns to userspace

```
Before IRETQ:              After IRETQ:
┌─────────────┐
│     SS      │ ──────────→ SS register
├─────────────┤
│    RSP      │ ──────────→ RSP register (user stack)
├─────────────┤
│   RFLAGS    │ ──────────→ RFLAGS register
├─────────────┤
│     CS      │ ──────────→ CS register (ring 3)
├─────────────┤
│    RIP      │ ──────────→ Continue execution here
└─────────────┘
```

### Step 8: sys_exit and Returning to Kernel

After printing, the user program calls `sys_exit`:

```asm
mov rax, 0          ; SYS_EXIT = 0
mov rdi, 0          ; exit_code = 0
int 0x80            ; Trap to kernel
```

`sys_exit()` in `syscall.c:48-64`:

```c
static void sys_exit(uint64_t exit_code) {
    puts("\n========================================\n");
    puts("Process exited with code ");
    put_dec(exit_code);
    puts("\n========================================\n");

    /* Switch back to kernel address space */
    vmm_switch_address_space(vmm_get_kernel_pml4());

    /* Return to kernel - this never returns */
    context_return_to_kernel();
}
```

`context_return_to_kernel()` in `context_asm.S:127-135`:

```asm
context_return_to_kernel:
    movq saved_kernel_rsp(%rip), %rsp  ; Restore kernel stack
    sti                                 ; Re-enable interrupts
    jmp context_resume_from_user        ; Pop saved regs and ret
```

This effectively makes `context_switch_to_user()` return, and the kernel continues execution.

---

## Adding a New Syscall

To add a new syscall (e.g., `sys_getpid`):

### 1. Define the Syscall Number

In `syscall.h`:

```c
#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2  /* New syscall */
```

### 2. Implement the Handler

In `syscall.c`:

```c
static uint64_t sys_getpid(void) {
    /* For now, we only have one process */
    return 1;
}
```

### 3. Add Dispatch Case

In `syscall_handler()`:

```c
switch (regs->rax) {
    case SYS_EXIT:
        sys_exit(regs->rdi);
        break;
    case SYS_WRITE:
        regs->rax = sys_write(regs->rdi, regs->rsi, regs->rdx);
        break;
    case SYS_GETPID:  /* New case */
        regs->rax = sys_getpid();
        break;
    default:
        regs->rax = SYSCALL_ERROR;
        break;
}
```

### 4. Call from Userspace

In user assembly:

```asm
mov rax, 2      ; SYS_GETPID
int 0x80        ; Invoke syscall
; RAX now contains PID
```

---

## Complete Flow Diagram

```
USER PROGRAM (Ring 3)                    KERNEL (Ring 0)
─────────────────────                    ────────────────

mov rax, 1 (SYS_WRITE)
mov rdi, 1 (stdout)
mov rsi, buffer
mov rdx, 22
int 0x80 ─────────────────────────────→  [CPU: IDT lookup, stack switch]
                                                    │
                                                    ▼
                                         isr_128:
                                           push registers
                                           call syscall_handler
                                                    │
                                                    ▼
                                         syscall_handler(regs):
                                           switch(regs->rax) {
                                             case SYS_WRITE:
                                               sys_write(...)
                                           }
                                                    │
                                                    ▼
                                         sys_write():
                                           for each byte:
                                             putc(byte) → serial + FB
                                           return count
                                                    │
                                                    ▼
                                         pop registers
                                         iretq ─────────────────────────→

[RAX = bytes written]
; continue execution...
```

---

## Further Reading

- **Intel SDM Volume 3**: Chapter 6 (Interrupt and Exception Handling)
- **OSDev Wiki**: [System Calls](https://wiki.osdev.org/System_Calls)
- **Linux kernel source**: `arch/x86/entry/` directory
- **"Operating Systems: Three Easy Pieces"**: Chapter on System Calls
