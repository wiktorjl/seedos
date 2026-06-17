# Chapter 8 — Serial Output for Debugging

> Part I — Foundation · Status: 🚧 outline

> **Source files:** `drivers/tty/serial.c`, `arch/x86/include/asm/io.h`

## What this chapter covers

A kernel developer's most reliable friend is the **serial port**: a dead-simple
text channel that works before graphics, survives crashes, and can be captured to
a file. This chapter brings up the COM1 UART for both debug logging and
interactive input.

## Outline

1. **What a serial port (UART) is** — a byte-at-a-time text link; why it predates
   and outlives fancier I/O.
2. **Intuition** — like piping the kernel's stdout to your terminal (which is
   literally what `-serial stdio` does, Chapter 7).
3. **Port I/O** — programming the 16550 UART registers with `inb`/`outb`.
4. **In SeedOS** — transmit for logging; interrupt-driven receive so the shell
   works over serial (the IRQ is wired in Chapter 17).

## Reference & cross-links

- **Previous:** [Chapter 7 — The Toolchain](07-toolchain.md).
- **Next:** [Chapter 9 — The Terminal Abstraction & `kprintf`](09-terminal-kprintf.md).
