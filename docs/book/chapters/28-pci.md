# Chapter 28 — PCI Enumeration

> Part VI — Storage & Filesystem · Status: ⬜ not started

> **Status:** planned build-path chapter — not yet implemented in SeedOS.

## What this chapter covers

To talk to a real disk we first have to *find* it on the **PCI** bus. This
forward-looking chapter will walk PCI configuration space to enumerate devices and
locate the virtio block device used in Chapter 29.

## Outline

1. **What PCI is** — the bus that connects most devices; the bus/device/function
   addressing scheme.
2. **Configuration space** — vendor/device IDs, class codes, and Base Address
   Registers (BARs).
3. **Intuition** — enumeration is "scan every slot and ask each card what it is."
4. **Planned implementation** — probing config space (port `0xCF8`/`0xCFC` or
   MMIO), and matching the virtio-blk device.

## Reference & cross-links

- **Previous:** [Chapter 27 — Signals](27-signals.md).
- **Next:** [Chapter 29 — The virtio-blk Driver](29-virtio-blk.md).
