# Chapter 6 — Interrupts & Exceptions: The IDT and ISRs

> Part II — Boot & Architecture · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

This chapter explains how SeedOS catches everything the CPU can throw at it —
faults like page faults and general-protection faults, plus hardware IRQs from
the timer and keyboard. You will see how the Interrupt Descriptor Table is built,
what the per-vector assembly stubs do to normalize wildly different entry
conditions into one uniform stack frame, and how the single C dispatcher routes
each vector to a panic, a page-fault handler, or a registered IRQ handler.

## Source files

- `arch/x86/kernel/idt.c`, `idt.h` — the IDT, the dispatcher, and IRQ registration
- `arch/x86/kernel/isr.S` — the per-vector assembly stubs and common entry path

## 1. The vector map

x86 has 256 interrupt vectors. SeedOS uses them in three bands:

| Vectors | Purpose |
|---------|---------|
| 0–31 | CPU **exceptions** (divide error, page fault, #GP, …) |
| 32–47 | Hardware **IRQs**, delivered through the I/O APIC (Chapter 7) |
| 255 | The **spurious** interrupt vector |

Vector 32 is the Local APIC timer; vector 33 is the keyboard. The mapping of an
ISA IRQ number to one of these vectors is the I/O APIC's job, covered in the next
chapter — this chapter is about what happens once a vector fires.

## 2. The IDT and its gates

An IDT entry (`idt_entry_t`) is a 16-byte descriptor holding the address of a
handler stub, a code selector, an IST index, and a type/attribute byte. SeedOS
defines three gate types:

```c
#define IDT_GATE_INTERRUPT 0x8E   /* present, DPL 0, interrupt gate (clears IF) */
#define IDT_GATE_TRAP      0x8F   /* present, DPL 0, trap gate (leaves IF)      */
#define IDT_GATE_USER      0xEE   /* present, DPL 3 — callable from ring 3      */
```

`idt_install()` fills the table from an array of 49 assembly stubs — `isr_0`
through `isr_47`, plus `isr_255` — pointing each gate at its stub through the
kernel code selector from Chapter 5:

```c
for (int i = 0; i < IDT_SIZE; i++)
    if (isr_stubs[i] != 0)
        idt_set_gate(i, (uint64_t)isr_stubs[i], GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 0);
```

It then overrides three gates to use the IST stacks built in Chapter 5, so these
critical exceptions always run on a known-good stack:

```c
idt_set_gate(2,  (uint64_t)isr_2,  GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 1); /* NMI  → IST1 */
idt_set_gate(8,  (uint64_t)isr_8,  GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 2); /* #DF  → IST2 */
idt_set_gate(18, (uint64_t)isr_18, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 3); /* #MCE → IST3 */
```

Finally it loads the table with `lidt`. Note the IST indices (1, 2, 3) match
exactly the `ist1`/`ist2`/`ist3` slots populated in `gdt.c` — the two files have
to agree.

## 3. The ISR stubs: normalizing entry

Different vectors arrive on the stack differently: some exceptions push a
hardware error code, most do not, and none push their own vector number. The
stubs in `isr.S` paper over this with two macros so that by the time we reach
common code, every entry looks identical:

```asm
.macro ISR_NOERR num       /* vectors with no CPU error code */
isr_\num:
    pushq $0               /* push a dummy error code        */
    pushq $\num            /* push the vector number          */
    jmp isr_common
.endm

.macro ISR_ERR num         /* vectors where the CPU pushed an error code */
isr_\num:
    pushq $\num            /* just push the vector number     */
    jmp isr_common
.endm
```

Vectors 8, 10, 11, 12, 13, 14, 17, 21, and 30 use `ISR_ERR`; everything else
uses `ISR_NOERR`. `isr_common` then saves all 15 general-purpose registers and
does three subtle but essential things before calling C:

```asm
isr_common:
    pushq %rax            /* … all 15 GPRs, in interrupt_frame_t order … */
    pushq %r15

    testb $3, 144(%rsp)   /* was CS's low 2 bits = 3? (trapped from ring 3) */
    jz 1f
    swapgs                /*   yes → swap to the kernel GS base             */
1:  cld                   /* SysV ABI requires DF=0 on entry to C           */
    movq %rsp, %rdi       /* arg 1 = pointer to the saved frame             */
    call interrupt_handler
```

The conditional `swapgs` is the careful part: `GS` is only swapped when the trap
came from user mode (`CS & 3 == 3`), because doing it on a kernel-mode entry
would swap the *already-correct* kernel `GS` to garbage. On the way out the stub
mirrors the same `swapgs`, restores the registers, drops the error code and
vector with `addq $16, %rsp`, and issues `iretq`.

## 4. The interrupt frame

Because `isr_common` pushes registers in a fixed order on top of what the CPU
pushed, the kernel stack at the moment `interrupt_handler` runs is exactly an
`interrupt_frame_t`:

```c
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;   /* pushed by isr_common */
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;       /* pushed by isr_common */
    uint64_t int_no, error_code;                      /* pushed by the stub   */
    uint64_t rip, cs, rflags, rsp, ss;                /* pushed by the CPU    */
} __attribute__((packed)) interrupt_frame_t;
```

The handler receives a pointer to this and can read or modify any saved register
— which is how the scheduler (Chapter 13) preempts a thread, and how the page
fault handler inspects the faulting context.

## 5. One dispatcher to route them all

Every vector lands in `interrupt_handler()`:

```c
void interrupt_handler(interrupt_frame_t *frame)
{
    uint64_t int_no = frame->int_no;

    if (int_no < 32) {
        if (int_no == 14) { handle_page_fault(frame); return; }   /* page fault */
        /* any other exception → fatal */
        log_panic("EXCEPTION: %s (int %d, error=0x%x)",
                  exception_names[int_no], int_no, frame->error_code);
        /* … dump registers, backtrace, halt … */
    }

    if (int_no == 255) return;                  /* spurious: ignore */

    if (irq_handlers[int_no] != 0)
        irq_handlers[int_no](frame);            /* registered IRQ handler */
    else
        apic_eoi();                             /* unclaimed IRQ: just ack it */
}
```

So the three bands from Section 1 each get distinct treatment: an **exception**
(0–31) is fatal and panics with a named message and a backtrace — except the
**page fault** (vector 14), which is given to `handle_page_fault()` because
SeedOS uses faults to implement copy-on-write `fork` (the mechanics are
Chapter 20's subject). A **spurious** interrupt is silently ignored. A
**hardware IRQ** is dispatched to whatever handler registered for it.

## 6. Registering an IRQ handler

Drivers claim a vector with `idt_register_irq()`:

```c
void idt_register_irq(int irq, irq_handler_t handler);   /* irq_handlers[irq] = handler */
```

For example, the APIC timer registers vector 32 (Chapter 7) and the keyboard
registers vector 33 (Chapter 15). **A registered handler owns the End-Of-Interrupt
acknowledgement** — it must call `apic_eoi()` itself (the timer handler does so
explicitly). Only *unclaimed* vectors get an automatic EOI from the dispatcher.
This is the contract referenced back in the project's "adding an IRQ handler"
guidance.

## 7. Backtraces

When an exception is fatal, the panic path walks the stack via frame pointers:

```c
void backtrace(uint64_t rip, uint64_t rbp)
{
    for (int i = 1; i < 10 && rbp != 0; i++) {
        if (!is_valid_kernel_addr(rbp)) break;   /* must be a higher-half, aligned ptr */
        uint64_t *frame = (uint64_t *)rbp;
        log_debug("  [%d] 0x%016llx", i, frame[1] - 1);   /* return address */
        rbp = frame[0];                                    /* previous frame */
    }
}
```

This is why two earlier decisions mattered: the kernel is compiled
`-fno-omit-frame-pointer` (Chapter 2) so `RBP` always chains frames, and
`_start` zeroed `RBP` (Chapter 4) so the walk has a clean terminator at the
bottom of the call stack.

## 8. A note on NMI nesting

Vector 2 (NMI) has a dedicated stub rather than going through `isr_common`,
because an NMI can fire at literally any instruction — including the tiny
`swapgs` windows in the syscall path. The stub maintains two diagnostic counters,
`nmi_depth` and `nmi_lost_count`, to detect re-entry (a second NMI arriving on
the same IST1 stack before the first returns). The current handler is panic-only
and never re-enters, so this should be unreachable; full Linux-style nested-NMI
safety is deliberately deferred until SeedOS has a real NMI workload. It is a
good example of the book's honesty principle: the edge is documented in the code,
not hidden.

## Reference & cross-links

- **Previous:** [Chapter 5 — x86-64 CPU Setup](05-cpu-setup.md) (the GDT selector
  and IST stacks this chapter references).
- **Next:** [Chapter 7 — Hardware Discovery: ACPI, LAPIC & I/O APIC](07-acpi-apic.md)
  is where IRQ vectors 32–47 actually come from.
- **Page faults as a feature (copy-on-write):**
  [Chapter 20 — `fork()` and Copy-on-Write](20-fork-cow.md).
- **The timer IRQ that drives preemption:**
  [Chapter 13 — Kernel Threads & the Scheduler](13-threads-scheduler.md).
