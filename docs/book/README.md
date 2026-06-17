# The SeedOS Book

> A complete guide to a modern x86-64 kernel, built from scratch.

This book explains **how SeedOS works and how it is built** — from the first
instruction the bootloader hands off, through memory management and the
scheduler, up to user-mode processes running ELF binaries off an ext2
filesystem.

It is written as a **hybrid**: each *Part* tells a story you can read straight
through, while individual *chapters* mix conceptual explanation with concrete
reference detail and links into the real source tree. You can read it
cover-to-cover to learn how an OS comes together, or jump to a chapter as a
reference for one subsystem.

## How to read it

- Start with **[the Table of Contents](SUMMARY.md)**.
- New to the codebase? Read Parts I–II in order.
- Looking for one subsystem? Jump straight to its chapter; each begins with the
  source files it covers.

## How this book relates to the rest of `docs/`

- **`docs/book/`** (here) — the narrative, all-in-one book. The canonical,
  descriptive account of how the OS works *today*.
- **`docs/reference/`** — the original terse per-subsystem reference notes.
  These are being absorbed into book chapters; each chapter links back to its
  source reference while migration is in progress.
- **`docs/plans/`** — forward-looking design and TODO documents (userspace,
  BusyBox bring-up, fork completion, known issues). These describe what *will*
  be, vs. the book's what *is*.

## Conventions

- Code excerpts are illustrative; the cited source file is always authoritative.
- File references look like `kernel/process.c:42` and are clickable in editors.
- Status markers (✅ / 🚧 / ⬜) in the TOC show how complete each chapter is.

## Contributing to the book

1. Pick a 🚧 or ⬜ chapter from [`SUMMARY.md`](SUMMARY.md).
2. Fill in the outline; pull accurate detail from the cited source files.
3. When a chapter fully covers a `docs/reference/` file, fold that file in and
   remove it, updating the link.
4. Keep `SUMMARY.md` status markers up to date.
