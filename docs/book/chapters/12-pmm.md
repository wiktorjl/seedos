# Chapter 12 — The Physical Memory Manager

> Part II — Memory Management · Status: 🚧 outline

> **Source files:** `mm/pmm.c`, `mm/pmm.h`, `mm/memory.h`

## What this chapter covers

Memory management begins with knowing which physical RAM exists and which 4 KB
**pages** of it are free. This chapter takes Limine's memory map and builds a
bitmap allocator that hands out and reclaims physical pages — the foundation every
later memory layer stands on.

## Outline

1. **What "physical memory" means** — real addresses vs. the virtual ones
   programs see (preview of Chapter 13).
2. **Intuition** — a giant array of pages and a bitmap of "free/used" bits.
3. **Example** — `pmm_alloc()` returns a free page; `pmm_free()` returns it.
4. **In SeedOS** — consuming the Limine memory map, the bitmap, and the
   free/usable page accounting printed at boot.

## Reference & cross-links

- **Previous:** [Chapter 11 — A Panic Handler with Backtrace](11-panic-backtrace.md).
- **Next:** [Chapter 13 — Virtual Memory & Higher-Half Ownership](13-vmm.md).
- **Where the memory map comes from:** [Chapter 5 — Minimal Boot via Limine](05-limine-boot.md).
