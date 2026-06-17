# Chapter 23 — The VFS & File Descriptors

> Part V — User Space · Status: 🚧 outline

> **Source files:** `fs/vfs.c`, `fs/vfs.h`, `kernel/process.c` (fd table), `drivers/tty/tty_dev.c`

## What this chapter covers

`open`/`read`/`write`/`close` need a uniform interface over different file sources.
This chapter builds the **Virtual Filesystem** layer and the per-process **file
descriptor** table that maps the small integers user code uses to open files.

## Outline

1. **What a VFS is** — one interface (`read`/`write`/`stat`/…) many backends plug
   into.
2. **What a file descriptor is** — a per-process index into a table of open files.
3. **Intuition** — `fd` is like the handle Python's `open()` returns, but it's
   literally an `int` indexing a kernel array.
4. **In SeedOS** — path resolution, the fd table, and wiring the TTY as fds 0/1/2.

## Reference & cross-links

- **Previous:** [Chapter 22 — System Calls](22-syscalls.md).
- **Next:** [Chapter 24 — Early Userspace](24-early-userspace.md).
- **The on-disk filesystem behind it:** [Chapter 30 — The ext2 Filesystem](30-ext2.md).
