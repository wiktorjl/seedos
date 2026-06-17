# Chapter 25 — The ELF Loader

> Part V — User Space · Status: 🚧 outline

> **Source files:** `kernel/elf.c`, `kernel/elf.h`

## What this chapter covers

To run a compiled program, the kernel must parse its **ELF** file, map its
segments into a fresh address space, and set up the initial stack exactly as the
program expects. This chapter builds the loader, including the Linux-compatible
stack layout (`argc`, `argv`, `envp`, `auxv`).

## Outline

1. **What ELF is** — the executable format: headers, program segments, entry
   point.
2. **Intuition** — like an installer's manifest saying "map these bytes at these
   addresses with these permissions, then jump here."
3. **The initial stack** — `argc`/`argv`/`envp`/the auxiliary vector, in the
   precise order the C runtime reads them.
4. **In SeedOS** — `elf_load()`, segment mapping via the VMM, and handing off to
   user mode.

## Reference & cross-links

- **Previous:** [Chapter 24 — Early Userspace](24-early-userspace.md).
- **Next:** [Chapter 26 — `fork` & `exec`](26-fork-exec.md).
- **The address space it populates:** [Chapter 13 — Virtual Memory](13-vmm.md).
