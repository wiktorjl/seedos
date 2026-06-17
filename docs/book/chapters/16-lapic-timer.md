# Chapter 16 — The Local APIC Timer

> Part III — Hardware Discovery · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

With the hardware located via ACPI (Chapter 15), we bring up the **Local APIC** —
each CPU's interrupt inbox — and program its **timer** to fire periodically. That
periodic tick is the single most important interrupt in the system: it's the
heartbeat that makes preemptive multitasking possible (Part IV). Along the way we
meet **memory-mapped I/O**, the way software talks to most modern devices.

## Source files

- `arch/x86/kernel/apic.c`, `apic.h` — the Local APIC and the periodic timer
- `arch/x86/include/asm/io.h` — `inb`/`outb` port I/O (used to calibrate the timer)

## 1. Talking to a device: MMIO and port I/O

**The concept.** How does software "talk to" a chip? Two mechanisms:

- **Memory-mapped I/O (MMIO):** the device's control registers appear at certain
  physical addresses. Reading or writing those addresses *is* talking to the
  device — the access has hardware side effects.
- **Port I/O:** a separate address space reached only by the special `in`/`out`
  instructions, a legacy x86 mechanism still used by a few old devices.

> 🐍 **From Python — the intuition.** MMIO is like a memory address that's secretly
> a device: `reg[EOI] = 0` doesn't just store a zero, it *commands the chip*.
> Assignment has side effects. (And these addresses must be mapped *uncached* —
> the CPU must not cache a device register, or it would "read" a stale value
> instead of asking the hardware.)

**For example,** acknowledging an interrupt is a single MMIO write to the Local
APIC's "EOI" register; reprogramming the legacy timer used for calibration is a
few `outb`s to its port.

**In SeedOS.** The Local APIC is reached through a mapped MMIO window, with a tiny
read/write helper. The mapping uses a "no-cache" flag for the reason above:

```c
vmm_map_page(pml4, lapic_virt, lapic_phys, PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);

static inline void lapic_write(uint32_t reg, uint32_t value) {
    lapic_base[reg / 4] = value;             /* MMIO: writing the chip */
    (void)lapic_base[LAPIC_ID / 4];          /* read back as a barrier */
}
```

## 2. Enabling the Local APIC

**The concept.** The **Local APIC (LAPIC)** is a per-CPU chip: the CPU's interrupt
inbox. It delivers local sources (like its built-in timer) and owns the **EOI**
register that acknowledges each interrupt. SeedOS also disables the ancient 8259
PIC so the APIC is the only interrupt controller in play.

**In SeedOS.** `apic_init()` maps the LAPIC, masks out the legacy PIC, and turns
the LAPIC on by writing its spurious-vector register with the enable bit and the
spurious vector (255):

```c
lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | IRQ_SPURIOUS);
```

`apic_eoi()` — the "ack button" the dispatcher in Chapter 10 relies on — is just
a single MMIO write to the EOI register.

## 3. The timer, and calibrating it

**The concept.** A **periodic timer interrupt** is what lets an OS take control
back from a running program. Without it, a program stuck in a loop would own the
CPU forever; with a timer firing every 10 ms, the kernel regains control 100 times
a second. One snag: the LAPIC timer counts at a frequency the kernel doesn't know
in advance, so it must **calibrate** — measure the unknown clock against a known
one (the legacy PIT).

> 🐍 **From Python — the intuition.** The timer tick is the kernel's
> `setInterval`: a callback that fires on a fixed schedule no matter what else is
> running. Calibration is "I have a stopwatch of unknown speed; let me time it
> against a clock I trust, then do the arithmetic."

**For example,** run the LAPIC timer from its maximum count, wait a known 10 ms on
the PIT, and see how far it counted down:

```c
lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);    /* start the unknown clock at max */
pit_sleep_ms(10);                             /* wait a trusted 10 ms           */
uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);
uint32_t ticks_per_period = (elapsed * 100) / TIMER_FREQUENCY_HZ;   /* → 100 Hz */
```

> 🔄 **Transition: Polling → Interrupts.** You *could* track time by spinning in a
> loop reading the timer's count — but that burns the entire CPU doing nothing.
> The whole point of the timer is to let the CPU do other work and be *interrupted*
> when the period elapses. So SeedOS calibrates with a short polling loop (above),
> then switches the timer to **periodic interrupt** mode on vector 32 — and never
> polls it again.

**In SeedOS.** The handler is the system's heartbeat. Note it sends the EOI
*before* it may switch contexts and never return here:

```c
void apic_timer_handler(void)
{
    tick_count++;
    console_update_cursor(tick_count);   /* blink the cursor    */
    apic_eoi();                          /* ack BEFORE switching */
    if (kthread_current() != NULL) {
        kthread_wake_sleepers();
#if CONFIG_KTHREAD_PREEMPTIVE
        kthread_schedule();              /* preempt (Chapter 19) */
#endif
    }
}
```

## Reference & cross-links

- **Previous:** [Chapter 15 — ACPI Table Parsing](15-acpi.md).
- **Next:** [Chapter 17 — The I/O APIC & PS/2 Keyboard](17-ioapic-keyboard.md).
- **The dispatcher that calls `apic_eoi`:** [Chapter 10 — The IDT & Exception Handlers](10-idt-exceptions.md).
- **What the timer tick drives:** [Chapter 19 — The Scheduler & Preemption](19-scheduler-preemption.md).
