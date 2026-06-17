# Chapter 17 — The I/O APIC & PS/2 Keyboard

> Part III — Hardware Discovery · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

The Local APIC (Chapter 16) handles the CPU's *local* interrupts. External
devices — like the keyboard — reach the CPU through a different chip: the **I/O
APIC**, a programmable router from device interrupt lines to CPU vectors. This
chapter configures it, then uses it to bring the **PS/2 keyboard** online — first
by polling, then converted to interrupt-driven input.

## Source files

- `arch/x86/kernel/ioapic.c`, `ioapic.h` — routing device IRQs
- `drivers/input/keyboard.c`, `keyboard.h` — the PS/2 keyboard driver

## 1. The I/O APIC: a programmable interrupt router

**The concept.** Where the Local APIC is per-CPU, the **I/O APIC** is shared. It
takes external device interrupt lines and routes each one to a chosen CPU as a
chosen vector number, via a table of **redirection entries**.

> 🐍 **From Python — the intuition.** The I/O APIC is a routing table you program:
> "device line *N* → deliver vector *V* to CPU *C*." It's the switchboard between
> the dozens of physical interrupt lines and the 256 vectors the CPU understands.

**For example,** to make the keyboard work you tell the I/O APIC "route device
line 1 to vector 33 on this CPU," then unmask that line. From then on, a keypress
arrives as vector 33 — which the IDT (Chapter 10) hands to the keyboard handler.

**In SeedOS.** `ioapic_init()` maps the I/O APIC's registers (uncached) and masks
every line until a driver opts in. `ioapic_route_irq()` applies any ACPI override
from Chapter 15, then writes the 64-bit redirection entry:

```c
uint32_t gsi = irq_to_gsi(irq, &polarity, &trigger);   /* honor ACPI overrides */
uint64_t entry = vector
               | IOAPIC_DELIVERY_FIXED
               | IOAPIC_DESTMODE_PHYSICAL
               | polarity | trigger
               | IOAPIC_DEST(apic_id);                 /* destination CPU      */
ioapic_write_redir(gsi, entry);
```

## 2. The PS/2 keyboard

**The concept.** The **PS/2 keyboard** is a simple device behind a small
controller: when a key changes state it makes a **scancode** byte available at an
I/O port and (optionally) raises interrupt line 1. Software reads the byte and
translates the scancode into a character.

> 🐍 **From Python — the intuition.** Reading a key is like reading one byte from a
> socket: there's a "is data ready?" flag and a "data" register. The question is
> only *how you wait* for that byte — and that's the heart of this chapter.

> 🔄 **Transition: Polling → Interrupts.** The simplest way to read a key is to
> **poll**: loop forever reading the controller's status port until the
> "output buffer full" bit is set, then read the scancode. That works — and it's
> a great first test that the device is alive — but it pins the CPU at 100% doing
> nothing but checking. The fix is to let the keyboard **raise IRQ 1**: the CPU
> does other work, and the I/O APIC delivers an interrupt only when a key is
> actually pressed. SeedOS uses the interrupt-driven path.

**For example,** the polling version is essentially:

```c
while (!(inb(0x64) & 1)) { }    /* spin until a byte is ready (status port) */
uint8_t scancode = inb(0x60);   /* read it (data port)                       */
```

…and the interrupt version replaces the spin with "do nothing; the handler runs
when IRQ 1 fires."

**In SeedOS.** `keyboard_init()` registers a handler on vector 33 with
`idt_register_irq()`, then routes and unmasks the keyboard's line on the I/O APIC.
When a key is pressed, the handler reads the scancode, translates it, and feeds the
character to the terminal — and, like every IRQ handler, sends an `apic_eoi()`
(Chapter 10) on its way out.

## 3. The kernel's MMIO map

With ACPI and both APICs configured, here are the fixed virtual windows the kernel
has claimed for devices and tables across Part 0 and Part III:

| Virtual base | Maps |
|--------------|------|
| `0xFFFF800000000000` | the direct map — all of physical RAM (Chapter 13) |
| `0xFFFFFFFF80000000` | the kernel image (Chapter 5) |
| `0xFFFFFFFD00000000` | Local APIC registers (uncached) |
| `0xFFFFFFFD00001000` | I/O APIC registers (uncached) |
| `0xFFFFFFFE00000000` | ACPI tables (mapped on demand) |

With every vector now having somewhere to go, `kmain()` runs
`cpu_enable_interrupts()` and the machine is fully live: the timer's heartbeat
ticks and keystrokes flow in.

## Reference & cross-links

- **Previous:** [Chapter 16 — The Local APIC Timer](16-lapic-timer.md).
- **Next:** [Chapter 18 — Kernel Threads & Context Switching](18-threads-context-switch.md)
  begins Part IV.
- **The IDT that routes vector 33:** [Chapter 10 — The IDT & Exception Handlers](10-idt-exceptions.md).
- **The IRQ overrides honored when routing:** [Chapter 15 — ACPI Table Parsing](15-acpi.md).
- **The full address-space layout:** [Appendix A — Memory Map Reference](../appendices/a-memory-map.md).
