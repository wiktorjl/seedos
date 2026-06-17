# Chapter 7 — Hardware Discovery: ACPI, LAPIC & I/O APIC

> Part II — Boot & Architecture · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

Chapter 6 built the machinery to *receive* interrupts; this one builds the
machinery to *discover the hardware* and *route* interrupts to the CPU. We'll see
how the kernel learns what the machine contains by reading firmware tables
(ACPI), how software talks to a hardware device at all (memory-mapped vs. port
I/O), how the modern interrupt controllers (the Local APIC and I/O APIC) deliver
IRQs, and how the periodic timer — the heartbeat behind multitasking — is set up
and calibrated. As before: concept, intuition, example, then the SeedOS code.

## Source files

- `arch/x86/kernel/acpi.c`, `acpi.h` — reading firmware tables (RSDP→XSDT→MADT)
- `arch/x86/kernel/apic.c`, `apic.h` — the Local APIC and the periodic timer
- `arch/x86/kernel/ioapic.c`, `ioapic.h` — routing device IRQs
- `arch/x86/include/asm/io.h` — `inb`/`outb` port I/O primitives

## 1. Hardware discovery with ACPI

**The concept.** The kernel cannot assume where the interrupt hardware lives or
how many CPUs exist — those vary by machine. The firmware describes the platform
in a set of **ACPI tables**: a small in-memory database, reached by following a
chain of pointers from a root structure (the **RSDP**) to a root table
(**RSDT/XSDT**) to specific tables. The one SeedOS wants is the **MADT**, which
lists the CPUs and the APICs.

> 🐍 **From Python — the intuition.** Think of ACPI as a manifest the firmware
> leaves in memory — like a `config.json` describing the machine. You don't
> hard-code the hardware layout; you *parse* it. The catch is that it's a binary
> format with checksums and a pointer chain, not text, so "parsing" means walking
> structs and validating bytes.

**For example,** to find the interrupt controllers you start at the RSDP, follow
it to the root table, scan that table's entries for the one tagged `"APIC"` (the
MADT), and read the Local APIC and I/O APIC addresses out of it.

**In SeedOS.** `acpi_init()` does exactly that, checksumming at each hop. There's
one wrinkle from Chapter 4: under Limine revision 3, ACPI memory is *not* in the
direct map, so the parser must map each table into the kernel's address space
before reading it — which is why `acpi_init()` runs *after* `vmm_init()`:

```c
madt_t *madt = (madt_t *)find_table(root_sdt, "APIC", use_xsdt);
if (madt == NULL) { log_error("ACPI: MADT not found"); return -1; }
parse_madt(madt);
```

`parse_madt()` records what the interrupt subsystem needs into one `acpi_info_t`:
each CPU's APIC ID, the Local APIC address, the I/O APIC address, and any
**interrupt source overrides** — firmware-specified remaps of legacy IRQ numbers,
needed to route IRQs correctly on real machines.

## 2. Talking to a device: MMIO and port I/O

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
APIC's "EOI" register; reprogramming the legacy timer is a few `outb`s to its
port.

**In SeedOS.** The Local APIC is accessed through a mapped MMIO window, with a
tiny read/write helper; the legacy PIT and PIC use port I/O from
`asm/io.h`:

```c
static inline void lapic_write(uint32_t reg, uint32_t value) {
    lapic_base[reg / 4] = value;             /* MMIO: writing the chip   */
    (void)lapic_base[LAPIC_ID / 4];          /* read back as a barrier   */
}
```

The mapping is created with a "no-cache" flag, exactly for the reason in the
intuition box:

```c
vmm_map_page(pml4, lapic_virt, lapic_phys, PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);
```

## 3. The interrupt controllers: LAPIC and I/O APIC

**The concept.** Modern x86 splits interrupt handling across two chips:

- The **Local APIC (LAPIC)** is *per-CPU*. It is the CPU's interrupt inbox: it
  delivers local sources (like the timer), and it has the "EOI" register that
  acknowledges each interrupt.
- The **I/O APIC** is a *shared* switchboard. It takes external device interrupt
  lines and routes each one to a chosen CPU as a chosen vector number.

> 🐍 **From Python — the intuition.** The I/O APIC is a programmable router: a
> table mapping "device line *N*" → "(deliver vector *V* to CPU *C*)." The LAPIC
> is each CPU's local mailbox plus the *ack* button you press when you've handled
> a message. SeedOS deliberately disables the ancient 8259 PIC so these two are
> the only game in town.

**For example,** to make the keyboard work, you tell the I/O APIC "route device
line 1 to vector 33 on this CPU," then unmask that line. From then on, a keypress
arrives as interrupt vector 33 — which the IDT (Chapter 6) sends to the keyboard
handler.

**In SeedOS.** `apic_init()` maps the LAPIC, disables the legacy PIC, and enables
the LAPIC by writing its spurious-vector register; `ioapic_init()` maps the I/O
APIC and masks every line until a driver opts in. Routing applies any ACPI
override, then writes a 64-bit redirection entry:

```c
uint32_t gsi = irq_to_gsi(irq, &polarity, &trigger);   /* honor ACPI overrides */
uint64_t entry = vector
               | IOAPIC_DELIVERY_FIXED
               | IOAPIC_DESTMODE_PHYSICAL
               | polarity | trigger
               | IOAPIC_DEST(apic_id);                 /* destination CPU      */
ioapic_write_redir(gsi, entry);
```

`apic_eoi()` — the "ack button" — is a single MMIO write to the LAPIC's EOI
register.

## 4. The timer: a heartbeat, and calibrating it

**The concept.** A **periodic timer interrupt** is what lets an OS take control
back from a running program. Without it, a program stuck in a loop would own the
CPU forever. With a timer firing, say, every 10 ms, the kernel regains control 100
times a second and can switch to another task — this is the foundation of
**preemptive multitasking**. One snag: the LAPIC timer counts at a frequency the
kernel doesn't know in advance, so it must **calibrate** — measure the unknown
clock against a known one.

> 🐍 **From Python — the intuition.** The timer tick is the kernel's
> `setInterval`: a callback that fires on a fixed schedule no matter what else is
> running. It's *the* reason a single CPU can appear to run many programs at once
> (Chapter 13). Calibration is just "I have a stopwatch of unknown speed; let me
> time it against a clock I trust, then do the arithmetic."

**For example,** SeedOS runs the LAPIC timer from its maximum count, waits a known
10 ms using the legacy PIT as the trusted clock, reads how far the LAPIC counted
down, and scales that to the target rate:

```c
lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);    /* start the unknown clock at max */
pit_sleep_ms(10);                             /* wait a trusted 10 ms           */
uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);
uint32_t ticks_per_period = (elapsed * 100) / TIMER_FREQUENCY_HZ;   /* → 100 Hz */
```

**In SeedOS.** It then programs the timer in *periodic* mode on vector 32 at 100
Hz. The handler is the system's heartbeat — and notice it acknowledges the
interrupt *before* it may switch away and never return here:

```c
void apic_timer_handler(void)
{
    tick_count++;
    console_update_cursor(tick_count);   /* blink the cursor    */
    apic_eoi();                          /* ack BEFORE switching */
    if (kthread_current() != NULL) {
        kthread_wake_sleepers();
#if CONFIG_KTHREAD_PREEMPTIVE
        kthread_schedule();              /* preempt (Chapter 13) */
#endif
    }
}
```

## 5. The kernel's MMIO map

Between this chapter and Chapter 4, the kernel has claimed several fixed virtual
windows above its image for device and table mappings:

| Virtual base | Maps |
|--------------|------|
| `0xFFFF800000000000` | the direct map — all of physical RAM (Chapter 9) |
| `0xFFFFFFFF80000000` | the kernel image (Chapter 4) |
| `0xFFFFFFFD00000000` | Local APIC registers (uncached) |
| `0xFFFFFFFD00001000` | I/O APIC registers (uncached) |
| `0xFFFFFFFE00000000` | ACPI tables (mapped on demand) |

Appendix A collects the full memory map. With ACPI parsed and both APICs
configured, `kmain()` can finally run `cpu_enable_interrupts()` — every vector now
has somewhere to go, and the timer's heartbeat begins.

## Reference & cross-links

- **Previous:** [Chapter 6 — Interrupts & Exceptions](06-interrupts.md) (the
  vectors this chapter's hardware delivers).
- **Next:** [Chapter 8 — Physical Memory: The PMM](08-physical-memory.md) begins
  Part III, the memory subsystem.
- **The `vmm_map_page` / uncached mappings used here:**
  [Chapter 9 — Virtual Memory & 4-Level Paging](09-virtual-memory.md).
- **The timer tick driving preemption and sleeping threads:**
  [Chapter 13 — Kernel Threads & the Scheduler](13-threads-scheduler.md).
- **The full address-space layout:**
  [Appendix A — Memory Map Reference](../appendices/a-memory-map.md).
