# Chapter 7 — The Toolchain: Cross-Compiler, Build, GDB & QEMU

> Part I — Foundation · Status: ✅ drafted

## What this chapter covers

This chapter sets up the workshop: the tools and build that turn SeedOS source
into something that boots, and the emulator and debugger you'll live in. If your
mental model of "running code" is `python app.py`, building an OS will feel alien:
there is no interpreter, no "just run the file." Instead we compile source into
machine code, link it into one special binary, pack that onto a bootable disk
image, and boot a whole (virtual) computer from it.

> 🐍 **From Python.** Running `app.py` is one step because the interpreter and OS
> are already running. Here, *nothing* is already running — so "build and run"
> expands into: **compile → link → make a bootable image → boot a machine**. This
> chapter is that pipeline.

## Source files

- `Makefile` — the entire build, from `.c`/`.S` to `build/seed.iso`
- `configure` — a dependency checker you run once
- `scripts/download-font.sh` — fetches and bakes the console font (`data/font.bin`)
- `scripts/convert-image.sh` — turns the boot logo PNG into raw pixels
- `scripts/mkinitrd.sh` — builds the ext2 RAM disk (`build/initrd.ext2`)

## 1. The toolchain, in plain terms

A few words you'll see constantly. None have a Python equivalent because Python
hides them all inside the interpreter.

> 🧠 **First principles: compiling vs. interpreting.** Python *interprets* your
> source at run time. C is *compiled*: a separate program translates the whole
> source into the CPU's machine code *ahead of time*, producing a binary the CPU
> runs directly. No interpreter sits between your code and the CPU — exactly what
> a kernel needs.

> 🧠 **First principles: compiler, assembler, linker.** Building C uses a small
> assembly line of tools, collectively the *toolchain*:
> - the **compiler** (`cc`) turns each C file into an *object file* — machine
>   code with unresolved references ("call `kmain`, wherever it ends up");
> - the **assembler** does the same for hand-written assembly (`.S`) files;
> - the **linker** (`ld`) stitches all objects into one binary, resolving those
>   references and placing everything at concrete addresses.

> 🐍 **From Python.** An object file is a bit like a compiled `.pyc`, and linking
> is loosely like the import system wiring modules together — except it happens
> *before* the program runs, and the output is raw machine code with no
> interpreter to host it.

SeedOS builds with your system's *ordinary* `cc` and `ld` — there is no separate
cross-compiler to install. What makes it a kernel build is a set of unusual
compiler flags (Section 3) that say "assume no operating system or library is
present."

> 🗺️ **The source layout, at a glance.** The tree follows Linux conventions, so
> you always know where a feature lives: `arch/x86/` (CPU-specific: boot, GDT,
> IDT, APIC), `kernel/` (core: threads, scheduler, processes, `kprintf`), `mm/`
> (memory: PMM, VMM, heap), `drivers/` (devices: console, serial, keyboard),
> `fs/` (VFS + ext2), `init/main.c` (the `kmain` bring-up), `include/seedos/`
> (global headers), and `userspace/` (ring-3 programs). Headers live next to
> their source.

## 2. Prerequisites

Run the bundled checker first:

```bash
./configure
```

It verifies the **required** tools — `cc` (GCC or Clang), `ld`, `make`,
`xorriso`, and `git` — and the **optional** ones — `bear` (used by `make
compdb`), `qemu-system-x86_64`, and `gdb`. It also looks for UEFI firmware
(OVMF).

A few build-time dependencies aren't covered by `configure` but are needed in
practice:

- **e2fsprogs** (`mke2fs`, `debugfs`) — to build the ext2 RAM disk.
- **ImageMagick** (`convert`, `identify`) and **bc** — the boot logo is
  regenerated from a PNG on every clean build.
- **OVMF firmware** at `/usr/share/ovmf/OVMF.fd` (override with `make OVMF=...`).

### First-time setup: generate the font

The console font binary is *generated, not checked in* — `data/font.bin` matches
the repository's `*.bin` ignore rule. The kernel's `boot.S` bakes it straight into
the binary, so on a fresh clone you must create it once:

```bash
scripts/download-font.sh      # downloads Spleen 8×16 → data/font.bin (needs network)
```

This fetches the Spleen bitmap font, strips its small header, and writes the raw
256-glyph × 16-byte table to `data/font.bin`. (Why bake a font into the kernel?
Because when the kernel first wants to draw text, there is no filesystem to load a
font *from* — Chapter 6.)

## 3. The targets you'll use

```bash
make          # Build the bootable ISO (build/seed.iso) — the default target
make run      # Build, then boot it in QEMU with serial wired to your terminal
make debug    # Same as run, but pause at reset and expose a GDB server (:1234)
make clean    # Remove the build/ directory
make compdb   # Generate compile_commands.json for editor/clangd support
```

There are a few more specialized targets — `initrd`, `userspace`, the `test*`
family (§8), and `show-sources` (print the discovered source/object lists).

## 4. What `make` actually does

The default target produces `build/seed.iso` through a short pipeline. Each stage
maps onto a rule in the `Makefile`.

**1. Discover sources.** The Makefile globs every `.c` and `.S` under the source
directories:

```make
C_SRCS   := $(shell find arch kernel mm fs drivers init lib demos -name '*.c' ...)
ASM_SRCS := $(shell find arch kernel -name '*.S' ...)
```

Objects are *flattened* into `build/` (so `mm/pmm.c` → `build/pmm.o`), with a
`VPATH` telling Make where to find each source. Dropping a new `.c` into one of
those directories is enough for it to be picked up.

**2. Compile, freestanding.** Every C file is compiled with flags that turn off
the assumptions a normal program makes:

```make
CFLAGS := -g -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=kernel \
          -fno-omit-frame-pointer $(INCLUDES)
```

> 🧠 **First principles: why the strange flags?** Each removes something the
> compiler would normally assume an OS or CPU feature provides:
> - `-ffreestanding` — "there is no standard library" (the freestanding idea from
>   the Introduction, as a compiler switch).
> - `-mno-sse -mno-mmx -mno-80387` — "don't use the floating-point/vector
>   registers," because the kernel hasn't turned that hardware on yet (Chapter 21).
> - `-mcmodel=kernel` — "this code runs in the top of the address space."
> - `-fno-omit-frame-pointer` — keep the bookkeeping that lets us print a stack
>   trace when something crashes (Chapter 11).

**3. Link with a custom script.** All objects are linked statically, no standard
library, using a *linker script* that places the image in the higher half:

```make
$(KERNEL): $(OBJS) arch/x86/boot/linker.ld
	ld -nostdlib -static -T arch/x86/boot/linker.ld -o $@ $(OBJS)
```

> 🧠 **First principles: a linker script.** Normally the linker chooses where code
> lands. A kernel can't — the bootloader loads it at an exact address. `linker.ld`
> (dissected in Chapter 5) tells the linker "put the kernel at
> `0xFFFFFFFF80000000`, in this section order." The result is `build/kernel.elf`.

**4. Build the RAM disk.** `scripts/mkinitrd.sh` creates a small ext2 image
(`build/initrd.ext2`) — see §7.

**5. Fetch and build Limine.** On the first build, the `limine` target clones the
Limine bootloader (the `v8.x-binary` branch, shallow) and builds its tools — needs
network and `git` once; afterwards the local `limine/` is reused.

**6. Assemble the bootable image.** The kernel, RAM disk, and Limine's UEFI boot
files are staged and turned into a bootable CD image by `xorriso`.

> 🧠 **First principles: a bootable image.** An *ISO* is a complete CD/DVD
> filesystem in one file that firmware can boot directly. `build/seed.iso`
> contains the bootloader, kernel, and RAM disk arranged so UEFI knows what to
> launch — the artifact you'd burn to a USB stick, or hand to an emulator (§5).

## 5. Running under QEMU

```bash
make run        # → qemu-system-x86_64 -bios OVMF.fd -cdrom build/seed.iso -boot d -serial stdio
```

> 🧠 **First principles: QEMU and OVMF.** **QEMU** is a software computer — a
> program pretending to be a full x86-64 PC. Booting your OS inside it means you
> don't risk (or reboot) your real machine. **OVMF** is open-source UEFI *firmware*
> for that virtual PC — the same role the firmware chip plays on a real
> motherboard (Part 0). `-bios OVMF.fd` supplies it; `-cdrom … -boot d` boots our
> ISO.

> 🧠 **First principles: the serial port.** `-serial stdio` wires the virtual
> machine's *serial port* — an old, dead-simple text channel (Chapter 8) — to your
> terminal, so the kernel's log and shell appear in your terminal window.

## 6. Debugging with GDB

```bash
make debug      # adds -s -S: GDB server on :1234, CPU frozen at reset
gdb build/kernel.elf -ex "target remote :1234"
```

> 🧠 **First principles: a source-level debugger, on a kernel.** **GDB** pauses
> execution, steps line by line, and inspects variables and registers. Here GDB
> runs on *your* machine while the kernel runs *inside QEMU*; they talk over
> `:1234`. Because the kernel is built with `-g` and `-fno-omit-frame-pointer`,
> GDB maps machine addresses back to your C source and shows clean backtraces.

The repository ships a `.gdbinit`, and the `.vscode/` directory plus the
`debug-vscode` target (serial to `build/serial.log`) wire the same flow into VS
Code.

## 7. The RAM disk (initrd)

`scripts/mkinitrd.sh` builds the small filesystem the kernel mounts at boot.

> 🧠 **First principles: an initrd / RAM disk.** SeedOS has no disk driver yet, so
> its "filesystem" is an image loaded entirely into memory at boot — an *initrd*
> (initial RAM disk). The bootloader loads it alongside the kernel; the kernel
> reads files out of it. It's formatted as **ext2** (Chapter 30).

```bash
scripts/mkinitrd.sh build/initrd.ext2 2     # 2 MiB ext2 image
```

The script avoids needing root: it `dd`s a blank image, formats it with `mke2fs`,
and writes files in with `debugfs` — by default a `hello.txt`, a placeholder
`/init`, and a `bin/` directory. Limine loads it as a *boot module*; `kmain()`
hands it to `ext2_init()`, and you can watch the round-trip in the boot log.

## 8. Userspace and the test harness

User programs live under `userspace/` and build separately:

```bash
make userspace             # builds the libc + test programs in userspace/build/
make test TEST=00_exit     # build with auto-init, pack 00_exit as /init, boot headless
```

`make test` compiles the kernel with `-DCONFIG_AUTO_INIT` (so `kmain()` launches
`/init` directly instead of the kernel shell), rebuilds the RAM disk with the
chosen binary as `/init`, and boots QEMU headless with a 15-second timeout.
`make test-exit` and `make test-write` are shortcuts. Chapter 24 returns to these.

## Reference & cross-links

- **Previous:** [Chapter 6 — Displaying Text on the Framebuffer](06-framebuffer.md).
- **Next:** [Chapter 8 — Serial Output for Debugging](08-serial.md).
- **The linker script and `_start` in detail:**
  [Chapter 5 — Minimal Boot via Limine](05-limine-boot.md).
- **The ext2 RAM disk internals:** [Chapter 30 — The ext2 Filesystem](30-ext2.md).
- **The userspace programs you can run:** [Chapter 24 — Early Userspace](24-early-userspace.md).
