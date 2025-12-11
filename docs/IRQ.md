# Interrupts and IRQs: A Deep Dive

This document explains interrupt handling from first principles, surveys how major operating systems approach it, and provides a detailed walkthrough of our implementation.

## Table of Contents

1. [What Are Interrupts?](#what-are-interrupts)
2. [Types of Interrupts](#types-of-interrupts)
3. [How Major Systems Handle Interrupts](#how-major-systems-handle-interrupts)
4. [Our Design Decisions](#our-design-decisions)
5. [Implementation Walkthrough](#implementation-walkthrough)

---

## What Are Interrupts?

An **interrupt** is a signal that tells the CPU to stop what it's doing and handle something more urgent. Without interrupts, the CPU would need to constantly poll every device to check if it needs attention - wasting enormous amounts of processing power.

### The Problem Interrupts Solve

Imagine you're writing a document and waiting for a key press. Without interrupts, the CPU would need to do something like:

```
while (true) {
    check_keyboard();      // Is a key pressed?
    check_timer();         // Has time elapsed?
    check_disk();          // Is disk I/O complete?
    check_network();       // Did a packet arrive?
    // ... check 100 more devices
    do_actual_work();      // Finally do something useful
}
```

This is **polling** - inefficient and wastes CPU cycles. With interrupts, the CPU can focus on useful work:

```
do_actual_work();  // CPU does this until interrupted
// When keyboard has data, it signals an interrupt
// CPU pauses, handles keyboard, returns to work
```

### The Interrupt Lifecycle

1. **Device signals** - Hardware device asserts an interrupt line
2. **CPU acknowledges** - CPU finishes current instruction, saves state
3. **Handler lookup** - CPU consults the Interrupt Descriptor Table (IDT)
4. **Handler executes** - Kernel code processes the interrupt
5. **Return** - CPU restores state and resumes previous work

---

## Types of Interrupts

### 1. Exceptions (Synchronous)

CPU-generated interrupts caused by the currently executing instruction:

| Vector | Name | Cause |
|--------|------|-------|
| 0 | Divide Error | Division by zero |
| 6 | Invalid Opcode | Unknown instruction |
| 13 | General Protection Fault | Privilege violation, segment errors |
| 14 | Page Fault | Memory access violation |

Exceptions are **synchronous** - they happen at a predictable point in code execution.

### 2. Hardware IRQs (Asynchronous)

External device signals that can occur at any time:

| IRQ | Device |
|-----|--------|
| 0 | Programmable Interval Timer (PIT) |
| 1 | PS/2 Keyboard |
| 2 | Cascade (slave PIC) |
| 8 | Real-Time Clock (RTC) |
| 12 | PS/2 Mouse |
| 14 | Primary ATA (hard disk) |

IRQs are **asynchronous** - they can interrupt the CPU at any point.

### 3. Software Interrupts

Deliberately triggered by the `INT` instruction:

- `INT 0x80` - Traditional Linux system call
- `INT 3` - Debugger breakpoint

---

## How Major Systems Handle Interrupts

### Linux

Linux has evolved a sophisticated interrupt handling architecture over decades:

**Top Half / Bottom Half Design:**
- **Top half**: Runs immediately in interrupt context, does minimal work
- **Bottom half**: Deferred work (softirqs, tasklets, workqueues)

```
Interrupt arrives
    │
    ▼
┌─────────────────┐
│    Top Half     │ ◄── Runs with interrupts disabled
│ (hardirq)       │     Quick: acknowledge device, queue work
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Bottom Half   │ ◄── Runs with interrupts enabled
│ (softirq/tasklet)│    Slower: process data, update state
└─────────────────┘
```

**Key Design Choices:**
- Uses APIC (Advanced PIC) on modern hardware for better scalability
- Per-CPU interrupt stacks to avoid stack overflow
- IRQ affinity - can bind interrupts to specific CPUs
- Threaded IRQs - many handlers run as kernel threads
- NAPI for high-throughput networking (polling + interrupts hybrid)

**Relevant source files:**
- `arch/x86/kernel/idt.c` - IDT setup
- `kernel/irq/` - Generic IRQ infrastructure
- `arch/x86/kernel/irq.c` - x86-specific handling

### macOS / Darwin (XNU Kernel)

Apple's XNU kernel uses a different model:

**I/O Kit Framework:**
- Object-oriented driver model in C++
- Interrupt handlers are "interrupt event sources"
- Work loops process events asynchronously

**Key Design Choices:**
- Uses I/O Kit's `IOInterruptEventSource` abstraction
- Interrupt handlers scheduled on per-driver work loops
- Heavy use of Mach primitives for synchronization
- APIC-based on Intel, custom on Apple Silicon

**Example flow:**
```
Hardware interrupt
    │
    ▼
IOInterruptEventSource::interruptOccurred()
    │
    ▼
Queued to driver's IOWorkLoop
    │
    ▼
Driver's Action method called
```

### Windows NT Kernel

Windows uses the **Interrupt Request Level (IRQL)** model:

**IRQL Levels:**
```
HIGH_LEVEL      ─── Machine check
POWER_LEVEL     ─── Power management
IPI_LEVEL       ─── Inter-processor interrupt
CLOCK_LEVEL     ─── System clock
PROFILE_LEVEL   ─── Profiling
DEVICE_LEVEL    ─── Device interrupts (DIRQL)
DISPATCH_LEVEL  ─── Thread scheduler
APC_LEVEL       ─── Async procedure calls
PASSIVE_LEVEL   ─── Normal thread execution
```

**Key Design Choices:**
- Strict priority hierarchy - higher IRQL masks lower
- Deferred Procedure Calls (DPCs) for bottom-half work
- Hardware Abstraction Layer (HAL) isolates interrupt hardware
- Interrupt Service Routines (ISRs) must be minimal

**Windows flow:**
```
Interrupt arrives
    │
    ▼
┌─────────────────┐
│      ISR        │ ◄── IRQL raised to device level
│                 │     Quick: queue DPC, dismiss interrupt
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│      DPC        │ ◄── IRQL at DISPATCH_LEVEL
│                 │     Process data, complete I/O
└─────────────────┘
```

### Comparison Summary

| Aspect | Linux | macOS | Windows |
|--------|-------|-------|---------|
| Model | Top/Bottom Half | Work Loops | IRQL + DPC |
| Handler Context | Interrupt → Softirq | IOWorkLoop thread | ISR → DPC |
| PIC/APIC | Both supported | APIC | HAL abstraction |
| Driver Model | Generic IRQ API | I/O Kit C++ | WDF/KMDF |

---

## Our Design Decisions

For this educational OS, we prioritize **simplicity** and **clarity** over performance.

### Design Goals

1. **Readable code** - Every line should be understandable
2. **Minimal abstraction** - See exactly what happens during an interrupt
3. **Educational value** - Expose the hardware directly

### Key Decisions

**1. Use the 8259 PIC, not APIC**

The APIC (Advanced PIC) is what modern systems use, but the 8259 PIC is simpler:
- Only 15 IRQ lines to manage
- Simpler initialization sequence
- No need for memory-mapped I/O
- Sufficient for learning

**2. Single handler for all interrupts**

Instead of registering per-IRQ handlers, we use one C function (`interrupt_handler`) that dispatches based on interrupt number. This makes the control flow explicit.

**3. Assembly stubs normalize the stack**

x86 exceptions are inconsistent - some push an error code, some don't. Our assembly stubs push a dummy zero for those that don't, giving us a uniform stack layout.

**4. No bottom-half processing**

Real OSes defer work from interrupt context. We handle everything immediately because:
- Our drivers are simple (keyboard only)
- We're not optimizing for throughput
- It's easier to understand

**5. Simple circular buffer for keyboard input**

Instead of complex event queues, we use a basic ring buffer. The interrupt handler (producer) adds characters, the main loop (consumer) reads them.

---

## Implementation Walkthrough

Let's trace through the code, following the path of a keyboard interrupt.

### Step 1: Hardware Initialization

Before interrupts can work, we must set up the hardware and CPU structures.

#### 1.1 PIC Initialization (`pic.c:92-145`)

The 8259 PIC needs to be remapped. By default, IRQ 0-7 map to interrupt vectors 8-15, which conflicts with CPU exceptions!

```c
void pic_init(void) {
    // Save current masks - we'll restore them after init
    uint8_t saved_master_mask = inb(PIC_MASTER_DATA);
    uint8_t saved_slave_mask = inb(PIC_SLAVE_DATA);

    // ICW1: Begin initialization sequence
    outb(PIC_MASTER_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC_SLAVE_COMMAND, ICW1_INIT | ICW1_ICW4);

    // ICW2: Set interrupt vector offsets
    // This is the crucial remapping step!
    outb(PIC_MASTER_DATA, 32);  // IRQ 0-7  → INT 32-39
    outb(PIC_SLAVE_DATA, 40);   // IRQ 8-15 → INT 40-47

    // ICW3: Configure master/slave cascade
    outb(PIC_MASTER_DATA, 4);   // Slave on IRQ2 (bit 2)
    outb(PIC_SLAVE_DATA, 2);    // Slave ID is 2

    // ICW4: Set 8086 mode
    outb(PIC_MASTER_DATA, ICW4_8086);
    outb(PIC_SLAVE_DATA, ICW4_8086);

    // Restore saved masks
    outb(PIC_MASTER_DATA, saved_master_mask);
    outb(PIC_SLAVE_DATA, saved_slave_mask);
}
```

After this, the interrupt vector mapping is:

```
CPU Exceptions:     0 - 31
Master PIC IRQs:   32 - 39  (IRQ 0-7)
Slave PIC IRQs:    40 - 47  (IRQ 8-15)
```

#### 1.2 IDT Setup (`idt.c:177-193`)

The Interrupt Descriptor Table tells the CPU where each interrupt handler lives:

```c
void idt_init(void) {
    // Set up handlers for exceptions (0-31) and IRQs (32-47)
    for (int i = 0; i < NUM_ISR_STUBS; i++) {
        idt_set_entry(i, isr_stubs[i], IDT_GATE_INTERRUPT);
    }

    // Syscall handler at INT 0x80 - callable from userspace (DPL=3)
    idt_set_entry(SYSCALL_VECTOR, isr_128, IDT_GATE_USER);

    // Load IDT into CPU
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;
    asm volatile ("lidt %0" : : "m"(idtr));
}
```

Each IDT entry is a 16-byte structure (`idt.h:39-47`):

```c
struct idt_entry {
    uint16_t offset_low;   // Handler address bits 0-15
    uint16_t selector;     // Code segment (kernel)
    uint8_t  ist;          // Interrupt Stack Table index
    uint8_t  type_attr;    // Present, DPL, gate type
    uint16_t offset_mid;   // Handler address bits 16-31
    uint32_t offset_high;  // Handler address bits 32-63
    uint32_t reserved;
};
```

The handler address is split across three fields for historical compatibility with 32-bit mode.

#### 1.3 Keyboard Initialization (`keyboard.c:153-155`)

Simply unmask IRQ 1 so keyboard interrupts reach the CPU:

```c
void keyboard_init(void) {
    pic_unmask(KEYBOARD_IRQ);  // IRQ 1
}
```

#### 1.4 Enable Interrupts (`kernel.c:436`)

Finally, set the Interrupt Flag in RFLAGS:

```c
asm volatile ("sti");  // Set Interrupt Flag
```

### Step 2: Interrupt Occurs

User presses a key. The PS/2 keyboard controller:

1. Stores the scancode in its buffer
2. Asserts the IRQ 1 line to the PIC

The PIC:

1. Receives the signal on IRQ 1
2. Raises the INTR pin to the CPU
3. When CPU acknowledges, sends vector number 33 (32 + IRQ 1)

### Step 3: CPU Response

The CPU, between instructions:

1. Sees INTR is asserted
2. Finishes current instruction
3. Saves state to stack:
   - SS (stack segment)
   - RSP (stack pointer)
   - RFLAGS
   - CS (code segment)
   - RIP (instruction pointer)
4. Looks up IDT[33]
5. Jumps to the handler address

If transitioning from ring 3 to ring 0, the CPU also:
- Loads the kernel stack pointer from TSS.RSP0
- Switches to kernel code/data segments

### Step 4: Assembly Stub (`isr.S`)

The CPU jumped to `isr_33`. Let's trace the assembly:

```asm
// Generated by macro ISR_NOERR 33
isr_33:
    pushq $0        // Dummy error code (IRQs don't have one)
    pushq $33       // Interrupt number
    jmp isr_common
```

Now the common handler:

```asm
isr_common:
    // Save all general-purpose registers
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    // Pass stack pointer as argument (points to interrupt_frame)
    movq %rsp, %rdi

    // Call C handler
    call interrupt_handler

    // Restore all registers
    popq %r15
    ... (reverse order)
    popq %rax

    // Remove error code and interrupt number
    addq $16, %rsp

    // Return from interrupt
    iretq
```

The stack now looks like this (matching `struct interrupt_frame`):

```
High addresses
┌─────────────────┐
│       SS        │ ◄── Pushed by CPU (if privilege change)
├─────────────────┤
│       RSP       │
├─────────────────┤
│     RFLAGS      │
├─────────────────┤
│       CS        │
├─────────────────┤
│       RIP       │
├─────────────────┤
│   Error Code    │ ◄── Pushed by our stub (dummy 0)
├─────────────────┤
│   Int Number    │ ◄── Pushed by our stub (33)
├─────────────────┤
│       RAX       │ ◄── Pushed by isr_common
├─────────────────┤
│       RBX       │
├─────────────────┤
│       ...       │
├─────────────────┤
│       R15       │ ◄── RSP points here (top of stack)
└─────────────────┘
Low addresses
```

### Step 5: C Interrupt Handler (`idt.c:213-316`)

```c
void interrupt_handler(struct interrupt_frame *frame) {
    // Breakpoint - just continue (for debugging)
    if (frame->int_no == EXCEPTION_BREAKPOINT) {
        return;
    }

    // CPU Exception (vectors 0-31) - panic
    if (frame->int_no < 32) {
        // Print diagnostic info and halt
        puts("KERNEL PANIC: ...");
        while (1) { asm volatile ("cli; hlt"); }
    }

    // Hardware IRQ (vectors 32-47)
    if (frame->int_no >= IRQ_BASE && frame->int_no < IRQ_BASE + 16) {
        uint8_t irq_number = frame->int_no - IRQ_BASE;

        // Dispatch to specific handler
        if (irq_number == 1) {
            keyboard_handler();  // IRQ 1 = keyboard
        }

        // CRITICAL: Acknowledge the interrupt
        pic_send_eoi(irq_number);
        return;
    }
}
```

For IRQ 1, we call `keyboard_handler()`, then send EOI to the PIC.

### Step 6: Keyboard Handler (`keyboard.c:164-214`)

```c
void keyboard_handler(void) {
    // Read scancode - MUST be done to clear keyboard buffer
    uint8_t scancode = inb(PS2_DATA_PORT);  // Port 0x60

    // Handle shift key state
    if (scancode == SCANCODE_LEFT_SHIFT ||
        scancode == SCANCODE_RIGHT_SHIFT) {
        shift_held = 1;
        return;
    }
    // ... handle shift release ...

    // Ignore key releases (high bit set)
    if (scancode & 0x80) {
        return;
    }

    // Translate scancode to ASCII
    char ascii = 0;
    if (scancode < SCANCODE_TABLE_SIZE) {
        if (shift_held) {
            ascii = scancode_to_ascii_shifted[scancode];
        } else {
            ascii = scancode_to_ascii_normal[scancode];
        }
    }

    // Add to circular buffer
    if (ascii != 0) {
        int next_write_pos = (buffer_write_pos + 1) % INPUT_BUFFER_SIZE;
        if (next_write_pos != buffer_read_pos) {  // Not full
            input_buffer[buffer_write_pos] = ascii;
            buffer_write_pos = next_write_pos;
        }
    }
}
```

The character is now in the input buffer, ready for the shell to consume.

### Step 7: End of Interrupt (`pic.c:155-162`)

```c
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        // Slave PIC interrupt - acknowledge slave first
        outb(PIC_SLAVE_COMMAND, 0x20);  // EOI command
    }
    // Always acknowledge master
    outb(PIC_MASTER_COMMAND, 0x20);
}
```

**Why is EOI critical?** The PIC tracks which interrupt is being serviced. Until we send EOI, it won't deliver any more interrupts of equal or lower priority. Forget EOI, and your keyboard stops working!

### Step 8: Return to Interrupted Code

The assembly stub (`isr.S`) resumes:

```asm
    // Restore all registers
    popq %r15
    popq %r14
    // ... etc
    popq %rax

    // Remove error code and interrupt number from stack
    addq $16, %rsp

    // Return from interrupt
    iretq
```

`IRETQ` pops RIP, CS, RFLAGS, RSP, and SS from the stack, restoring the CPU to exactly where it was before the interrupt.

### Step 9: Main Loop Consumes Input (`kernel.c:464-472`)

```c
while (1) {
    if (keyboard_has_char()) {
        char c = keyboard_get_char();
        shell_input(c);  // Process the character
    }

    // Halt until next interrupt - saves power
    asm volatile ("hlt");
}
```

The shell receives the character and updates the display.

---

## Complete Interrupt Flow Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                         KEY PRESSED                               │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  PS/2 Keyboard Controller                                        │
│  • Stores scancode in buffer                                     │
│  • Asserts IRQ 1 line                                            │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  8259 PIC (Master)                                               │
│  • Receives IRQ 1                                                │
│  • Raises INTR to CPU                                            │
│  • When acknowledged, sends vector 33                            │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  CPU                                                             │
│  • Finishes current instruction                                  │
│  • Pushes SS, RSP, RFLAGS, CS, RIP to stack                     │
│  • Looks up IDT[33]                                              │
│  • Jumps to isr_33                                               │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  isr_33 (isr.S)                                                  │
│  • Push dummy error code (0)                                     │
│  • Push interrupt number (33)                                    │
│  • Jump to isr_common                                            │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  isr_common (isr.S)                                              │
│  • Push all general-purpose registers                            │
│  • Set RDI = RSP (pointer to interrupt_frame)                    │
│  • Call interrupt_handler                                        │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  interrupt_handler (idt.c)                                       │
│  • Check: int_no >= 32 && < 48? Yes, it's an IRQ                │
│  • irq_number = 33 - 32 = 1                                      │
│  • Call keyboard_handler()                                       │
│  • Call pic_send_eoi(1)                                          │
└──────────────────────────────────────────────────────────────────┘
                               │
            ┌──────────────────┴──────────────────┐
            │                                     │
            ▼                                     ▼
┌─────────────────────────┐       ┌─────────────────────────┐
│  keyboard_handler       │       │  pic_send_eoi           │
│  (keyboard.c)           │       │  (pic.c)                │
│                         │       │                         │
│  • inb(0x60) → scancode │       │  • outb(0x20, 0x20)     │
│  • Translate to ASCII   │       │    (EOI to master)      │
│  • Add to input buffer  │       │                         │
└─────────────────────────┘       └─────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  isr_common continues (isr.S)                                    │
│  • Pop all general-purpose registers                             │
│  • Add 16 to RSP (remove error code and int number)             │
│  • IRETQ (restore RIP, CS, RFLAGS, RSP, SS)                     │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  Execution resumes where it was interrupted                      │
│  (probably in the HLT instruction of the main loop)             │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  Main loop (kernel.c)                                            │
│  • keyboard_has_char() → true                                    │
│  • c = keyboard_get_char()                                       │
│  • shell_input(c) → character appears on screen                  │
└──────────────────────────────────────────────────────────────────┘
```

---

## Key Files Summary

| File | Purpose |
|------|---------|
| `idt.h` | IDT structures, gate type constants, exception numbers |
| `idt.c` | IDT initialization, main `interrupt_handler()` |
| `isr.S` | Assembly stubs that save/restore CPU state |
| `pic.h` | PIC documentation and API declarations |
| `pic.c` | PIC initialization, EOI, mask/unmask |
| `io.h` | `inb()`/`outb()` for port I/O |
| `keyboard.c` | PS/2 keyboard driver using IRQ 1 |

---

## Further Reading

- **Intel Software Developer's Manual, Volume 3** - Chapter 6 (Interrupt and Exception Handling)
- **OSDev Wiki** - https://wiki.osdev.org/Interrupts
- **Linux kernel source** - `arch/x86/kernel/idt.c`, `kernel/irq/`
- **"Understanding the Linux Kernel"** by Bovet & Cesati - Chapter 4

---

## Exercises

1. **Add a timer interrupt handler** - The PIT fires on IRQ 0. Implement a handler that increments a tick counter.

2. **Implement `sleep()`** - Using the tick counter, implement a function that blocks for N milliseconds.

3. **Add more syscalls** - Implement `sys_read()` to read from the keyboard buffer via a system call.

4. **Migrate to APIC** - Replace the 8259 PIC with the Local APIC for better multiprocessor support.
