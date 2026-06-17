# Chapter 7 — Hardware Discovery: ACPI, LAPIC & I/O APIC

> Part II — Boot & Architecture · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

The previous chapter built the machinery to *receive* interrupts; this one builds
the machinery to *route* them. SeedOS uses the modern interrupt hardware — the
Local APIC and the I/O APIC — and it discovers where that hardware lives by
parsing ACPI tables. This chapter walks the ACPI parse from the RSDP down to the
MADT, brings up the Local APIC and calibrates its timer against the legacy PIT,
and configures the I/O APIC to deliver device IRQs as the vectors from Chapter 6.

## Source files

- `arch/x86/kernel/acpi.c`, `acpi.h` — RSDP/RSDT/XSDT/MADT parsing
- `arch/x86/kernel/apic.c`, `apic.h` — Local APIC and the periodic timer
- `arch/x86/kernel/ioapic.c`, `ioapic.h` — I/O APIC IRQ routing
- `arch/x86/include/asm/io.h` — `inb`/`outb` port I/O primitives

## 1. The discovery problem

The Local APIC and I/O APIC are memory-mapped devices, but their physical
addresses — and the number of CPUs, and any IRQ remappings the firmware applies —
are not fixed. The kernel learns them from **ACPI**, the firmware-provided tables
that describe the platform. There is one wrinkle from Chapter 4: under Limine
base revision 3, ACPI memory is **not** in the HHDM, so the parser cannot simply
dereference the physical RSDP pointer — it must map each table into kernel space
first. `acpi_map_region()` does that, handing out pages from a fixed virtual
window at `0xFFFFFFFE00000000` and preserving the original page offset:

```c
#define ACPI_VIRT_BASE  0xFFFFFFFE00000000ULL
/* map [phys, phys+size) and return the virtual address with offset preserved */
```

This is also why `acpi_init()` runs *after* `vmm_init()` in `kmain()`: it needs
working page tables to map anything.

## 2. Parsing ACPI: RSDP → RSDT/XSDT → MADT

`acpi_init()` follows the standard chain of pointers, validating a checksum at
every hop:

1. **RSDP.** Take the physical RSDP from Limine, map it, verify the `"RSD PTR "`
   signature and the 20-byte ACPI-1.0 checksum.
2. **Root table.** If the RSDP revision is ≥ 2 (ACPI 2.0+), follow the 64-bit
   **XSDT**; otherwise the 32-bit **RSDT**. Map the header to learn the table's
   length, map the whole thing, and checksum it.
3. **MADT.** Scan the root table's entries for the one with signature `"APIC"` —
   the Multiple APIC Description Table — and checksum it too.

```c
madt_t *madt = (madt_t *)find_table(root_sdt, "APIC", use_xsdt);
if (madt == NULL) { log_error("ACPI: MADT not found"); return -1; }
```

`find_table()` is the same routine for both root types, differing only in pointer
width (`uint32_t` vs `uint64_t`). Every failure along the way logs and returns
`-1`, so a malformed table degrades gracefully rather than faulting.

## 3. What the MADT yields

The MADT is a header followed by variable-length entries. `parse_madt()` walks
them and records what the interrupt subsystem needs into a single `acpi_info_t`:

| MADT entry type | What SeedOS extracts |
|-----------------|----------------------|
| 0 — Local APIC | each enabled CPU's APIC ID (into `cpu_apic_ids[]`, `cpu_count`) |
| 1 — I/O APIC | its physical address, ID, and GSI base |
| 2 — Interrupt Source Override | ISA-IRQ → GSI remaps + polarity/trigger (into `overrides[]`) |
| 4 — Local APIC NMI | logged |
| 5 — Local APIC Address Override | a 64-bit override of the LAPIC address |

```c
acpi_info.local_apic_address = madt->local_apic_address;
acpi_info.has_pic = madt->flags & 1;   /* bit 0: dual 8259 PICs present */
```

The result is fetched elsewhere via `acpi_get_info()`. Crucially, the **interrupt
source overrides** captured here are what let the I/O APIC route IRQs correctly on
machines where, for instance, the timer's ISA IRQ 0 is wired to a different GSI.

## 4. The Local APIC and its timer

`apic_init()` (run after `acpi_init()`) brings up the per-CPU Local APIC. First it
maps the LAPIC's MMIO registers **uncached** — caching device registers would be
a correctness bug:

```c
uint64_t lapic_virt = 0xFFFFFFFD00000000ULL;
vmm_map_page(pml4, lapic_virt, lapic_phys, PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);
```

Then it disables the legacy 8259 PIC (remapping it out of the way to vectors
32–47 and masking every line, so a stray PIC interrupt can't masquerade as an
exception), and enables the LAPIC by writing its Spurious Vector Register with
the enable bit and the spurious vector 255:

```c
lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | IRQ_SPURIOUS);
```

**Calibrating the timer.** The LAPIC timer counts at an unknown frequency, so
SeedOS measures it against a known clock — the legacy PIT. It runs the LAPIC timer
from its maximum count, busy-waits 10 ms on the PIT, and sees how far the LAPIC
counted:

```c
lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);   /* start from max          */
pit_sleep_ms(10);                            /* wait a known 10 ms      */
uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);
uint32_t ticks_per_period = (elapsed * 100) / TIMER_FREQUENCY_HZ;   /* 100 Hz */
```

It then registers a handler on vector 32 and programs the timer in **periodic**
mode to fire at `TIMER_FREQUENCY_HZ` (100 Hz → one tick every 10 ms). That tick
is the heartbeat of the whole system:

```c
void apic_timer_handler(void)
{
    tick_count++;
    console_update_cursor(tick_count);   /* blink the cursor   */
    apic_eoi();                          /* ack BEFORE switching */
    if (kthread_current() != NULL) {
        kthread_wake_sleepers();
#if CONFIG_KTHREAD_PREEMPTIVE
        kthread_schedule();              /* preempt (Chapter 13) */
#endif
    }
}
```

Note the ordering: the handler signals End-Of-Interrupt *before* it may switch
contexts, because a context switch might not return here for a long time.
`apic_eoi()` itself is a single write to the LAPIC `EOI` register.

## 5. The I/O APIC: routing device IRQs

Where the Local APIC is per-CPU, the **I/O APIC** is the chip that takes external
device interrupt lines and delivers them to a CPU as a chosen vector.
`ioapic_init()` maps its registers (again uncached, at `0xFFFFFFFD00001000`),
reads how many redirection entries it has, and **masks them all** so nothing fires
until a driver opts in.

Routing happens through `ioapic_route_irq()`, which first translates the ISA IRQ
to a Global System Interrupt — applying any ACPI override from Section 3 — then
writes a 64-bit redirection entry:

```c
uint32_t gsi = irq_to_gsi(irq, &polarity, &trigger);   /* honors ACPI overrides */
uint64_t entry = vector
               | IOAPIC_DELIVERY_FIXED
               | IOAPIC_DESTMODE_PHYSICAL
               | polarity | trigger
               | IOAPIC_DEST(apic_id);                 /* destination APIC ID  */
ioapic_write_redir(gsi, entry);
```

`ioapic_mask_irq()` / `ioapic_unmask_irq()` toggle a single line's mask bit. The
I/O APIC's registers are themselves reached indirectly: you write a register index
to `IOAPIC_REGSEL` and read/write the value through `IOAPIC_WIN`, and each 64-bit
redirection entry spans two of those windows. Devices like the keyboard
(Chapter 15) call `ioapic_route_irq()` and `ioapic_unmask_irq()` to bring their
line online — the timer does *not*, because it is delivered by the Local APIC
directly, not through the I/O APIC.

## 6. Port I/O

A few legacy devices here — the PIT used for calibration and the 8259 PIC being
disabled — are programmed with old-style port I/O rather than MMIO. Those `in`/
`out` instructions are wrapped in `arch/x86/include/asm/io.h` as `inb()`/`outb()`
and used by `apic.c` (and later the keyboard and serial drivers).

## 7. The kernel's MMIO map

Between this chapter and Chapter 4, the kernel has staked out several fixed
virtual windows above the kernel image for device and table mappings. Collecting
them in one place:

| Virtual base | Maps |
|--------------|------|
| `0xFFFF800000000000` | HHDM — all of physical RAM (Chapter 9) |
| `0xFFFFFFFF80000000` | the kernel image (Chapter 4) |
| `0xFFFFFFFD00000000` | Local APIC registers (uncached) |
| `0xFFFFFFFD00001000` | I/O APIC registers (uncached) |
| `0xFFFFFFFE00000000` | ACPI tables (mapped on demand) |

Appendix A collects the full memory map. With ACPI parsed and both APICs
configured, `kmain()` can finally call `cpu_enable_interrupts()` — every vector
now has somewhere to go.

## Reference & cross-links

- **Previous:** [Chapter 6 — Interrupts & Exceptions](06-interrupts.md) (the
  vectors this chapter's hardware delivers).
- **Next:** [Chapter 8 — Physical Memory: The PMM](08-physical-memory.md) begins
  Part III.
- **The `vmm_map_page` / `PTE_NOCACHE` mappings used here:**
  [Chapter 9 — Virtual Memory & 4-Level Paging](09-virtual-memory.md).
- **The timer tick driving preemption and sleeping threads:**
  [Chapter 13 — Kernel Threads & the Scheduler](13-threads-scheduler.md).
- **The full address-space layout:**
  [Appendix A — Memory Map Reference](../appendices/a-memory-map.md).
