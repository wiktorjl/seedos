# Chapter 29 — The virtio-blk Driver

> Part VI — Storage & Filesystem · Status: ⬜ not started

> **Status:** planned build-path chapter — not yet implemented in SeedOS.

## What this chapter covers

With the device located (Chapter 28), this forward-looking chapter will write a
driver for **virtio-blk**, the paravirtualized block device QEMU exposes —
SeedOS's first real disk, replacing the in-memory RAM disk.

## Outline

1. **What virtio is** — a standard, efficient interface for virtual devices
   (avoiding the cost of emulating real hardware).
2. **Virtqueues** — the shared-memory ring buffers the driver and device use to
   exchange requests.
3. **Intuition** — a producer/consumer queue between guest and host; you post
   "read block N into this buffer" and get a completion.
4. **Planned implementation** — device init/negotiation, the request queue, and
   read/write block operations.

## Reference & cross-links

- **Previous:** [Chapter 28 — PCI Enumeration](28-pci.md).
- **Next:** [Chapter 30 — The ext2 Filesystem](30-ext2.md).
