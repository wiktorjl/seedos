# Chapter 15 — ACPI Table Parsing (RSDP, MADT)

> Part III — Hardware Discovery · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

Before the kernel can program the interrupt controllers (Chapters 16–17), it has
to *find* them — and learn how many CPUs exist, and how the firmware has wired up
the legacy IRQs. None of that is fixed across machines; the firmware describes it
in **ACPI** tables. This chapter walks the ACPI pointer chain from the root
(**RSDP**) down to the table we care about (the **MADT**), validating a checksum
at each hop.

## Source files

- `arch/x86/kernel/acpi.c`, `acpi.h` — RSDP/RSDT/XSDT/MADT parsing

## 1. Hardware discovery with ACPI

**The concept.** The kernel cannot assume where the interrupt hardware lives or
how many CPUs the machine has. The firmware describes the platform in a set of
**ACPI tables**: a small in-memory database, reached by following a chain of
pointers from a root structure (the **RSDP**, Root System Description Pointer) to
a root table (**RSDT** or **XSDT**) to individual tables. The one SeedOS wants is
the **MADT** (Multiple APIC Description Table), which lists the CPUs and the
APICs.

> 🐍 **From Python — the intuition.** Think of ACPI as a manifest the firmware
> leaves in memory — like a `config.json` describing the machine. You don't
> hard-code the hardware layout; you *parse* it. The catch is that it's a binary
> format with checksums and a pointer chain, not text, so "parsing" means walking
> structs and validating bytes.

**For example,** to find the interrupt controllers you start at the RSDP, follow
it to the root table, scan that table's entries for the one tagged `"APIC"` (the
MADT), and read the Local APIC and I/O APIC addresses out of it.

**In SeedOS.** `acpi_init()` does exactly that. There's one wrinkle from Part 0:
under Limine revision 3, ACPI memory is *not* in the direct map (Chapter 5), so
the parser must map each table into the kernel's address space before reading it —
which is why `acpi_init()` runs *after* `vmm_init()` (Chapter 13):

```c
madt_t *madt = (madt_t *)find_table(root_sdt, "APIC", use_xsdt);
if (madt == NULL) { log_error("ACPI: MADT not found"); return -1; }
parse_madt(madt);
```

## 2. The pointer chain, hop by hop

**The concept.** Each step validates the structure it lands on with an 8-bit
**checksum** (all bytes must sum to zero) before trusting it. The walk is:

1. **RSDP** — verify the `"RSD PTR "` signature and the ACPI-1.0 checksum.
2. **Root table** — if the RSDP is revision ≥ 2, follow the 64-bit **XSDT**;
   otherwise the 32-bit **RSDT**. Map it, checksum it.
3. **MADT** — scan the root table's array of pointers for the table whose
   signature is `"APIC"`, and checksum it too.

> 🐍 **From Python — the intuition.** It's like dereferencing a chain of
> pointers in a binary file format: read a header to learn a length, map that
> many bytes, validate, follow a pointer to the next structure. The checksums are
> the format's built-in "did I land on a real table?" assertion.

**For example,** the same `find_table()` routine handles both root types — they
differ only in pointer width (`uint32_t` for RSDT, `uint64_t` for XSDT).

**In SeedOS.** Once the MADT is found, `parse_madt()` walks its variable-length
entries and records what the interrupt subsystem needs into a single
`acpi_info_t`:

| MADT entry type | What SeedOS extracts |
|-----------------|----------------------|
| 0 — Local APIC | each enabled CPU's APIC ID (`cpu_apic_ids[]`, `cpu_count`) |
| 1 — I/O APIC | its physical address, ID, and GSI base |
| 2 — Interrupt Source Override | legacy IRQ → GSI remaps + polarity/trigger |
| 5 — Local APIC Address Override | a 64-bit override of the LAPIC address |

Those **interrupt source overrides** are the crucial payoff: they tell the I/O
APIC (Chapter 17) the *true* wiring of legacy IRQs, which differs from the naive
"IRQ *n* = GSI *n*" on real machines. The result is fetched elsewhere via
`acpi_get_info()`.

## Reference & cross-links

- **Previous:** [Chapter 14 — The Kernel Heap](14-heap.md).
- **Next:** [Chapter 16 — The Local APIC Timer](16-lapic-timer.md) uses the LAPIC
  address found here.
- **Then:** [Chapter 17 — The I/O APIC & PS/2 Keyboard](17-ioapic-keyboard.md)
  uses the I/O APIC address and the IRQ overrides.
- **Why the tables must be mapped by hand:** [Chapter 13 — Virtual Memory](13-vmm.md).
