# Chapter 10 — The IDT & Exception Handlers

> Part I — Foundation · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

Interrupts are how a CPU stops what it's doing to deal with something else — a
hardware device that needs attention, or an instruction that went wrong. They are
the mechanism behind multitasking, device I/O, and crash handling, so they are
worth understanding deeply. This chapter introduces the concept, gives you a
programmer's mental model for it, and then walks the SeedOS machinery: the table
that routes interrupts, the assembly that saves the CPU's state, and the single C
function that dispatches everything. (The crash path it triggers — panic and
backtrace — is Chapter 11.)

## Source files

- `arch/x86/kernel/idt.c`, `idt.h` — the interrupt table, dispatcher, and registration
- `arch/x86/kernel/isr.S` — the per-vector assembly stubs and common entry path

## 1. What an interrupt is

**The concept.** An **interrupt** diverts the CPU from its current instruction
stream to run a designated **handler**, then resumes where it left off. There are
two flavors:

- An **exception** is *synchronous* — caused by the instruction the CPU just
  executed. Dividing by zero, dereferencing an unmapped address (a page fault),
  or executing an illegal instruction all raise exceptions.
- A **hardware interrupt** (or **IRQ**, interrupt request) is *asynchronous* — it
  comes from a device at an unpredictable time. The timer firing and a key being
  pressed are IRQs.

Each is identified by a number 0–255 called a **vector**.

> 🐍 **From Python — the intuition.** You already know both flavors:
> - An exception is like a Python `ZeroDivisionError` — it happens *because of*
>   the line you just ran, synchronously. The difference is that here the *CPU
>   itself* raises and dispatches it, below any language.
> - A hardware IRQ is like a signal handler or an async event callback — code
>   that fires "out of band," interrupting whatever was running, because
>   something external happened. Except the event loop is the silicon.

**For example,** when you press a key, the keyboard asserts IRQ 1; the CPU stops
your running program mid-instruction, jumps to the keyboard handler, then returns
as if nothing happened. When a program divides by zero, the CPU raises exception
vector 0 instead.

**In SeedOS.** The 256 vectors are used in three bands:

| Vectors | Purpose |
|---------|---------|
| 0–31 | CPU **exceptions** (divide error, page fault, #GP, …) |
| 32–47 | Hardware **IRQs**, delivered via the I/O APIC (Chapter 17) |
| 255 | The **spurious** interrupt (a "never mind" from the APIC) |

Vector 32 is the timer; 33 is the keyboard.

## 2. The IDT: a dispatch table the hardware reads

**The concept.** How does the CPU know *which* handler to run for vector 14? It
looks it up in the **Interrupt Descriptor Table (IDT)** — an array of 256
entries, one per vector, each holding the address of a handler plus some
attributes. The kernel builds this table and tells the CPU where it is; from then
on, the hardware indexes it automatically.

> 🐍 **From Python — the intuition.** The IDT is a `dict` mapping vector → handler
> — except *the CPU* does the lookup, not your code. You register the callbacks
> once; thereafter the hardware calls them for you. Setting up the IDT is like
> wiring up an event dispatcher and then handing the keys to the kernel.

**For example,** entry 14 points at the page-fault handler, so any unmapped
memory access transfers control there; entry 32 points at the timer handler.

**In SeedOS.** An IDT entry (`idt_entry_t`) packs a handler address, the kernel
code selector (the GDT segment set up in Chapter 21), and a type byte.
`idt_install()` fills all 256 entries from an array of assembly stubs and loads
the table with `lidt`:

```c
for (int i = 0; i < IDT_SIZE; i++)
    if (isr_stubs[i] != 0)
        idt_set_gate(i, (uint64_t)isr_stubs[i], GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 0);
```

Three vectors are then overridden to run on dedicated **interrupt stacks** (the
IST, configured with the CPU's tables in Chapter 21) — the critical exceptions
that must survive a corrupt kernel stack:

```c
idt_set_gate(2,  (uint64_t)isr_2,  GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 1); /* NMI  → IST1 */
idt_set_gate(8,  (uint64_t)isr_8,  GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 2); /* #DF  → IST2 */
idt_set_gate(18, (uint64_t)isr_18, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 3); /* #MCE → IST3 */
```

The IST indices (1, 2, 3) match the slots filled in the GDT setup — the two must
agree.

## 3. Saving the CPU's state

**The concept.** A handler runs on the same CPU, using the same registers, as the
code it interrupted. If it clobbers a register and then resumes the interrupted
code, that code breaks in impossible-to-debug ways. So every handler must **save
all registers on entry and restore them on exit** — the interrupted program must
resume *exactly* as if the interrupt never happened. The saved snapshot is called
the interrupt's **context**. Two complications make this fiddly:

1. The CPU's automatic save is uneven: some exceptions push an **error code**,
   most don't, and none push their own vector number — so entry conditions
   differ per vector.
2. If the interrupt came from a *user* program, a per-CPU register base (`GS`)
   must be swapped to the kernel's before C code runs.

> 🐍 **From Python — the intuition.** Picture a decorator that wraps every handler
> to snapshot all state on the way in and restore it on the way out — like a
> context manager around a callback. Here you write that decorator *by hand, in
> assembly, register by register*, because there is no runtime to do it for you.

**For example,** to make every handler look identical, the stub for a vector with
no hardware error code pushes a *dummy* one, so the stack layout is uniform
regardless of which vector fired. Then a shared routine saves the 15
general-purpose registers in a fixed order.

**In SeedOS.** Two macros in `isr.S` normalize the entry; then `isr_common` saves
state, fixes up `GS`, and calls C:

```asm
.macro ISR_NOERR num        /* vectors with no CPU error code */
isr_\num:
    pushq $0                /* dummy error code → uniform layout */
    pushq $\num             /* vector number                      */
    jmp isr_common
.endm

isr_common:
    pushq %rax              /* … save all 15 GPRs … */
    pushq %r15
    testb $3, 144(%rsp)     /* did we come from ring 3? (CS low bits = 3) */
    jz 1f
    swapgs                  /*   yes → switch to the kernel's GS base      */
1:  cld                     /* C expects the direction flag clear           */
    movq %rsp, %rdi         /* arg 1 = pointer to the saved register frame  */
    call interrupt_handler
```

The conditional `swapgs` is the subtle bit: `GS` is swapped *only* when the trap
came from user mode, because doing it on a kernel-mode entry would swap the
already-correct kernel `GS` to garbage. On exit the stub mirrors the swap,
restores every register, drops the error code and vector, and issues `iretq` to
resume. Because the registers were pushed in a known order, the kernel stack at
the call site is exactly a C struct:

```c
typedef struct {
    uint64_t r15, ..., rax;          /* pushed by isr_common */
    uint64_t int_no, error_code;     /* pushed by the stub   */
    uint64_t rip, cs, rflags, rsp, ss; /* pushed by the CPU  */
} __attribute__((packed)) interrupt_frame_t;
```

A handler receives a pointer to this and can read *or modify* any saved register —
which is exactly how the scheduler preempts a thread (Chapter 19).

## 4. One dispatcher, and registering handlers

**The concept.** With state saved, a single C function decides what each vector
*means*: an exception is (usually) a fatal bug and should panic; an IRQ should be
routed to whoever asked for it. And after servicing a hardware IRQ, the handler
must **acknowledge** the interrupt controller — the "End Of Interrupt" (EOI) —
or it won't deliver the next one.

> 🐍 **From Python — the intuition.** This is a central event-loop dispatcher:
> one function, a table of registered callbacks, route each event to its
> callback. The EOI is the "ack" you send back to the broker so it knows you're
> ready for the next message.

**For example,** the timer driver calls `idt_register_irq(32, handler)` once at
boot; thereafter every timer tick flows through the dispatcher into that handler.

**In SeedOS.** `interrupt_handler()` is that function:

```c
void interrupt_handler(interrupt_frame_t *frame)
{
    uint64_t int_no = frame->int_no;

    if (int_no < 32) {
        if (int_no == 14) { handle_page_fault(frame); return; }  /* page fault */
        log_panic("EXCEPTION: %s ...", exception_names[int_no]);   /* else fatal */
        /* … dump registers, backtrace, halt — see Chapter 11 … */
    }
    if (int_no == 255) return;                  /* spurious: ignore */
    if (irq_handlers[int_no] != 0)
        irq_handlers[int_no](frame);            /* registered IRQ handler */
    else
        apic_eoi();                             /* unclaimed IRQ: just ack it */
}
```

Drivers claim a vector with `idt_register_irq(vector, handler)`. **A registered
handler owns its EOI** — it must call `apic_eoi()` itself (the timer handler does;
Chapter 16). Only *unclaimed* vectors get an automatic ack here.

One exception is special: vector 14, the **page fault**, is not always a bug —
SeedOS uses page faults to implement copy-on-write `fork`, so it gets its own
handler. That mechanism is Chapter 26's subject; here it's enough to see that a
fault can be a *feature*. The fatal path — what happens to all the *other*
exceptions — is Chapter 11.

## 5. A note on NMI nesting

Vector 2 (the *non-maskable interrupt*) has a hand-written stub instead of going
through `isr_common`, because an NMI can strike at literally any instruction —
including the brief `swapgs` windows above. The stub keeps two diagnostic
counters (`nmi_depth`, `nmi_lost_count`) to detect re-entry. The current handler
is panic-only and never re-enters, so this should be unreachable; full
Linux-style nested-NMI safety is deliberately deferred until SeedOS has a real
NMI workload. It's a good example of the book's honesty principle: the sharp edge
is documented in the code, not hidden.

## Reference & cross-links

- **Previous:** [Chapter 9 — The Terminal Abstraction & `kprintf`](09-terminal-kprintf.md).
- **Next:** [Chapter 11 — A Panic Handler with Backtrace](11-panic-backtrace.md)
  is the crash path this chapter's dispatcher triggers.
- **Where IRQ vectors 32–47 come from:**
  [Chapter 16 — The Local APIC Timer](16-lapic-timer.md) and
  [Chapter 17 — The I/O APIC & PS/2 Keyboard](17-ioapic-keyboard.md).
- **Page faults as a feature (copy-on-write):** [Chapter 26 — `fork` & `exec`](26-fork-exec.md).
- **The CPU tables (GDT/IST) these gates reference:** [Chapter 21 — User Mode](21-user-mode.md).
