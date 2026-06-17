# Chapter 14 — The Kernel Heap: `kmalloc`/`kfree`

> Part II — Memory Management · Status: 🚧 outline

> **Source files:** `mm/heap.c`, `mm/heap.h`

## What this chapter covers

Page allocation is too coarse for everyday kernel data structures, so we build the
kernel's own `malloc`: `kmalloc`/`kfree`/`krealloc`, backed by pages from the PMM
and mapped by the VMM.

## Outline

1. **Why a heap** — allocating arbitrary-sized objects, not just 4 KB pages.
2. **Intuition** — exactly Python's invisible allocator, except we write it (and
   there's no garbage collector — every `kmalloc` needs a `kfree`).
3. **A free-list allocator** — blocks, splitting, coalescing.
4. **In SeedOS** — `kheap_init()`, the allocator, and the used/free accounting.

## Reference & cross-links

- **Previous:** [Chapter 13 — Virtual Memory & Higher-Half Ownership](13-vmm.md).
- **Next:** [Chapter 15 — ACPI Table Parsing](15-acpi.md) begins Part III.
