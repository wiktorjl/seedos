# Chapter 24 — Early Userspace: Loading Programs from a Module

> Part V — User Space · Status: 🚧 outline

> **Source files:** `kernel/kinit.c`, `userspace/crt/crt0.S`, `userspace/include/syscall.h`

## What this chapter covers

Before there's a disk driver, how do we run a real user program? The bootloader
loads a filesystem image as a **module**, and the kernel reads test binaries
straight out of it. This chapter covers that early-userspace path and the C
runtime stub (`crt0`) that a user program starts in.

## Outline

1. **The module trick** — Limine loads an archive (a tar, or our ext2 image)
   alongside the kernel; no block driver needed yet (Part VI adds one).
2. **The C runtime** — `crt0.S`: what runs before `main()`, setting up the stack
   and calling into the program.
3. **Intuition** — the module is a zip you ship inside the boot image; `crt0` is
   the hidden `if __name__ == "__main__"` plumbing.
4. **In SeedOS** — `start_init()` launching `/init`; the userspace syscall
   wrappers.

## Reference & cross-links

- **Previous:** [Chapter 23 — The VFS & File Descriptors](23-vfs-fds.md).
- **Next:** [Chapter 25 — The ELF Loader](25-elf-loader.md).
