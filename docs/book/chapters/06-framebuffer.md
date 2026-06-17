# Chapter 6 — Displaying Text on the Framebuffer

> Part I — Foundation · Status: 🚧 outline

> **Source files:** `drivers/tty/console.c`, `drivers/tty/font.h`, `arch/x86/boot/boot.S` (embedded font)

## What this chapter covers

The first sign of life from a kernel is text on screen. This chapter turns the
raw **framebuffer** Limine hands us — a block of memory where each cell is a
pixel — into a character console: drawing glyphs from a bitmap font, handling
newlines and scrolling, and a blinking cursor.

## Outline

1. **What a framebuffer is** — pixels as memory; pitch, width, height, bpp.
2. **Intuition** — drawing a character means copying a small bitmap into a
   rectangle of pixels.
3. **A bitmap font** — the embedded 8×16 glyph table (Chapter 7 builds it in).
4. **In SeedOS** — `console_init()`, glyph blitting, scrollback, cursor blink
   driven by the timer (Chapter 16).

## Reference & cross-links

- **Previous:** [Chapter 5 — Minimal Boot via Limine](05-limine-boot.md).
- **Next:** [Chapter 7 — The Toolchain](07-toolchain.md).
- **The terminal layer and `kprintf` built on top:**
  [Chapter 9 — The Terminal Abstraction & `kprintf`](09-terminal-kprintf.md).
