# Chapter 4 — From UEFI to Limine: What We Delegate

> Part 0 — Understanding the Boot Environment · Status: 🚧 outline

## What this chapter covers

Having seen what UEFI provides, we decide what to keep doing ourselves and what to
delegate. This chapter motivates using the **Limine** bootloader: it absorbs the
verbose, error-prone UEFI dance and hands our kernel a clean, documented starting
state. It is the bridge from Part 0's exploration to Part I's real kernel.

## Outline

1. **What UEFI gives us** (recap) — services, memory map, framebuffer, ACPI
   pointer; and the `ExitBootServices` cliff.
2. **What's tedious to do by hand** — juggling the memory map across
   `ExitBootServices`, setting up the higher half, parsing the boot environment.
3. **What a boot protocol buys us** — a fixed contract so the kernel just
   *declares* what it needs (Chapter 5).
4. **Why Limine** — modern, UEFI-native, gives us long mode + paging + a direct
   map for free.
5. **The boundary** — exactly what Limine hands SeedOS: framebuffer, HHDM offset,
   memory map, RSDP, and modules (the initrd).

## Reference & cross-links

- **Previous:** [Chapter 3 — vmlinuz Is Just a PE Executable](03-vmlinuz-pe.md).
- **Next:** [Chapter 5 — Minimal Boot via Limine](05-limine-boot.md) begins Part I.
