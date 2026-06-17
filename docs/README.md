# SeedOS Documentation

Documentation for SeedOS is organized into three areas:

| Area | Path | Purpose |
|------|------|---------|
| 📖 **The Book** | [`book/`](book/) | Complete narrative guide to how SeedOS works and is built. Start here. |
| 📑 **Reference** | [`reference/`](reference/) | Terse per-subsystem notes. Being absorbed into the book. |
| 🧭 **Plans** | [`plans/`](plans/) | Forward-looking design & TODO docs (what *will* be). |

## Start here

- **[The SeedOS Book](book/README.md)** — read the [Table of Contents](book/SUMMARY.md).

## Reference notes

The original subsystem docs (descriptive, lookup-oriented):

- [Directory tree structure](reference/directory-tree-structure.md)
- [x86 architecture](reference/arch-x86.md)
- [Init & boot](reference/init-boot.md)
- [Kernel core](reference/kernel-core.md)
- [Memory management](reference/memory-management.md)
- [TTY drivers](reference/tty-drivers.md)
- [Input drivers](reference/input-drivers.md)
- [Library utilities](reference/lib-utilities.md)

> These predate the userspace/process/filesystem work and cover only the early
> kernel. They are being migrated into book chapters; treat the source tree as
> authoritative where they disagree.

## Plans & design docs

- [Userspace implementation plan](plans/userspace.md)
- [BusyBox bring-up plan](plans/busybox.md)
- [fork() completion plan](plans/fork-completion.md)
- [Known issues](plans/issues.md)
