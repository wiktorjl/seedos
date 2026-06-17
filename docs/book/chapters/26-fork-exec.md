# Chapter 26 — `fork` & `exec`

> Part V — User Space · Status: 🚧 outline

> **Source files:** `kernel/process.c`, `mm/page.c` (COW), `arch/x86/kernel/idt.c` (page-fault handler)

## What this chapter covers

The Unix way to create processes: `fork` duplicates the current process, and
`exec` replaces a process's image with a new program. This chapter covers the
process model, **copy-on-write** `fork` (sharing pages until one side writes), and
`exec` layering on the ELF loader.

## Outline

1. **The process model** — states, the process table, `fork`/`waitpid`/`spawn`.
2. **What copy-on-write is** — share pages read-only, copy lazily on the first
   write, driven by the page-fault handler (Chapter 10).
3. **Intuition** — `fork` is `os.fork()`; COW is "don't copy a gigabyte until
   someone actually changes a byte."
4. **In SeedOS** — `process_fork()`, the COW page-fault path, and `exec` reusing
   the ELF loader (Chapter 25).

## Reference & cross-links

- **Previous:** [Chapter 25 — The ELF Loader](25-elf-loader.md).
- **Next:** [Chapter 27 — Signals](27-signals.md).
- **The page-fault mechanism COW rides on:** [Chapter 10 — The IDT & Exception Handlers](10-idt-exceptions.md).
