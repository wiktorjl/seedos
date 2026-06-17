# Chapter 13 — Virtual Memory & Higher-Half Ownership

> Part II — Memory Management · Status: 🚧 outline

> **Source files:** `mm/vmm.c`, `mm/vmm.h`, `mm/page.c`

## What this chapter covers

The central illusion of an OS: every program sees its own private address space,
built by the CPU translating **virtual** addresses to **physical** ones through
page tables the kernel controls. This chapter builds 4-level paging, the
higher-half kernel mapping, and the direct map (HHDM) that lets the kernel reach
all of RAM.

## Outline

1. **What virtual memory is** — address translation, why it exists (isolation +
   flexibility).
2. **Intuition** — a per-process dictionary mapping virtual pages → physical
   pages, enforced by hardware.
3. **4-level paging** — PML4 → PDPT → PD → PT; the page-table walk.
4. **In SeedOS** — `vmm_map_page`, the higher half, the direct map, and the
   `phys_to_virt`/`virt_to_phys` arithmetic.

## Reference & cross-links

- **Previous:** [Chapter 12 — The Physical Memory Manager](12-pmm.md).
- **Next:** [Chapter 14 — The Kernel Heap](14-heap.md).
- **Page faults and copy-on-write build on this:**
  [Chapter 26 — `fork` & `exec`](26-fork-exec.md).
