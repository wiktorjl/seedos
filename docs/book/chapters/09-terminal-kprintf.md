# Chapter 9 — The Terminal Abstraction & `kprintf`

> Part I — Foundation · Status: 🚧 outline

> **Source files:** `drivers/tty/terminal.c`, `drivers/tty/tty_dev.c`, `kernel/kprintf.c`, `include/seedos/log.h`

## What this chapter covers

With a framebuffer console (Chapter 6) and a serial port (Chapter 8), we unify
them behind one **terminal** abstraction and build the kernel's own
`printf` — `kprintf` — plus the leveled `log_*` macros used throughout the book's
code excerpts.

## Outline

1. **Why an abstraction** — write once, appear on both screen and serial.
2. **A terminal** — cursor, escape handling, line discipline (VT100-ish).
3. **`kprintf` from scratch** — format parsing with no standard library.
4. **Logging** — `log_info`/`log_debug`/… and the compile-time log level
   (`config.h`).

## Reference & cross-links

- **Previous:** [Chapter 8 — Serial Output for Debugging](08-serial.md).
- **Next:** [Chapter 10 — The IDT & Exception Handlers](10-idt-exceptions.md).
