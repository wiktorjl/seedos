# Chapter 2 — Building & Running SeedOS

> Part I — Foundations · Status: ✅ drafted

## What this chapter covers

This chapter gets a SeedOS kernel built and booting, and — just as important for
a newcomer — explains what each step *is*. If your mental model of "running code"
is `python app.py`, building an OS will feel alien at first: there is no
interpreter, no "just run the file." Instead we compile source into machine code,
link it into a single special binary, pack that binary onto a bootable disk
image, and boot a whole (virtual) computer from it. We'll take those one at a
time.

> 🐍 **From Python.** Running `app.py` is one step because the interpreter is
> already installed and the OS is already running. Here, *nothing* is already
> running — so "build and run" expands into: **compile → link → make a bootable
> image → boot a machine**. This chapter is that pipeline.

## Source files

- `Makefile` — the entire build, from `.c`/`.S` to `build/seed.iso`
- `configure` — a dependency checker you run once
- `scripts/download-font.sh` — fetches and bakes the console font (`data/font.bin`)
- `scripts/convert-image.sh` — turns the boot logo PNG into raw pixels
- `scripts/mkinitrd.sh` — builds the ext2 RAM disk (`build/initrd.ext2`)

## 1. The toolchain, in plain terms

A few words you'll see constantly. None of them have a Python equivalent because
Python hides them all inside the interpreter.

> 🧠 **First principles: compiling vs. interpreting.** Python *interprets* your
> source: the interpreter reads it and acts on it at run time. C is *compiled*:
> a separate program translates the whole source into the CPU's machine code
> *ahead of time*, producing a binary the CPU runs directly. No interpreter sits
> between your code and the CPU — which is exactly what a kernel needs.

> 🧠 **First principles: compiler, assembler, linker.** Building C involves a
> small assembly line of tools, collectively the *toolchain*:
> - the **compiler** (`cc`) turns each C source file into an *object file* — a
>   chunk of machine code with unresolved references ("call the function
>   `kmain`, wherever it ends up");
> - the **assembler** does the same for hand-written assembly (`.S`) files;
> - the **linker** (`ld`) stitches all the object files into one final binary,
>   resolving those references and placing everything at concrete addresses.

> 🐍 **From Python.** An object file is a bit like a compiled `.pyc`, and linking
> is loosely like the import system wiring modules together — except it all
> happens *before* the program runs, and the output is raw machine code with no
> interpreter to host it.

SeedOS builds with your system's *ordinary* `cc` and `ld` — there is no separate
cross-compiler to install. What makes it a kernel build is a set of unusual
compiler flags (Section 3) that say "assume no operating system or library is
present."

## 2. Prerequisites

Run the bundled checker first:

```bash
./configure
```

It verifies the **required** tools — `cc` (GCC or Clang), `ld`, `make`,
`xorriso`, and `git` — and the **optional** ones — `bear` (used by `make
compdb`), `qemu-system-x86_64`, and `gdb`. It also looks for UEFI firmware
(OVMF) in the usual locations.

A few build-time dependencies are not yet covered by `configure` but are needed
in practice:

- **e2fsprogs** (`mke2fs`, `debugfs`) — to build the ext2 RAM disk.
- **ImageMagick** (`convert`, `identify`) and **bc** — the boot logo is
  regenerated from a PNG on every clean build.
- **OVMF firmware** at `/usr/share/ovmf/OVMF.fd` (override with `make OVMF=...`).

### First-time setup: generate the font

The console font binary is *generated, not checked in* — `data/font.bin` matches
the repository's `*.bin` ignore rule. The kernel's `boot.S` bakes it straight
into the binary, so on a fresh clone you must create it once:

```bash
scripts/download-font.sh      # downloads Spleen 8×16 → data/font.bin (needs network)
```

This fetches the Spleen bitmap font, strips its small header, and writes the raw
256-glyph × 16-byte table to `data/font.bin`. (Why bake a font into the kernel?
Because at the moment the kernel first wants to draw text, there is no
filesystem to load a font *from* — see §4.)

## 3. The targets you'll use

```bash
make          # Build the bootable ISO (build/seed.iso) — the default target
make run      # Build, then boot it in QEMU with serial wired to your terminal
make debug    # Same as run, but pause at reset and expose a GDB server (:1234)
make clean    # Remove the build/ directory
make compdb   # Generate compile_commands.json for editor/clangd support
```

There are a few more specialized targets — `initrd` (build just the RAM disk),
`userspace` (build the user programs), the `test*` family (§8), and
`show-sources` (print the discovered source/object lists, handy when debugging
the Makefile itself).

## 4. What `make` actually does

The default target produces `build/seed.iso` through a short pipeline. Each stage
maps onto a rule in the `Makefile`.

**1. Discover sources.** The Makefile globs every `.c` and `.S` under the source
directories:

```make
C_SRCS   := $(shell find arch kernel mm fs drivers init lib demos -name '*.c' ...)
ASM_SRCS := $(shell find arch kernel -name '*.S' ...)
```

Object files are *flattened* into `build/` (so `mm/pmm.c` becomes `build/pmm.o`),
with a `VPATH` telling Make where to find each source. Dropping a new `.c` file
into one of those directories is enough for it to be picked up — no Makefile edit
needed.

**2. Compile, freestanding.** Every C file is compiled with flags that turn off
all the assumptions a normal program makes:

```make
CFLAGS := -g -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=kernel \
          -fno-omit-frame-pointer $(INCLUDES)
```

> 🧠 **First principles: why the strange flags?** Each one removes something the
> compiler would normally assume an OS or CPU feature provides:
> - `-ffreestanding` — "there is no standard library." (This is §2 of Chapter 1
>   as a compiler switch.)
> - `-mno-sse -mno-mmx -mno-80387` — "don't use the floating-point/vector
>   registers," because the kernel hasn't turned that hardware on yet (Chapter 5).
> - `-mcmodel=kernel` — "this code runs in the top of the address space" (the
>   higher half).
> - `-fno-omit-frame-pointer` — keep the bookkeeping that lets us print a stack
>   trace when something crashes (Chapter 6).
>
> You will never pass these for an ordinary program; they exist *because* there
> is no OS underneath.

**3. Link with a custom script.** All objects are linked statically, with no
standard library, using a *linker script* that places the image in the higher
half:

```make
$(KERNEL): $(OBJS) arch/x86/boot/linker.ld
	ld -nostdlib -static -T arch/x86/boot/linker.ld -o $@ $(OBJS)
```

> 🧠 **First principles: a linker script.** Normally the linker decides where
> code and data land in memory. A kernel can't leave that to chance — the
> bootloader needs to load it at an exact address. `linker.ld` (dissected in
> Chapter 4) is a small file telling the linker "put the kernel at address
> `0xFFFFFFFF80000000`, in this section order." The result is
> `build/kernel.elf`.

**4. Build the RAM disk.** `scripts/mkinitrd.sh` creates a small ext2 image
(`build/initrd.ext2`) — see §7.

**5. Fetch and build Limine.** On the first build, the `limine` target clones the
Limine bootloader (the `v8.x-binary` branch, shallow) and builds its tools. This
needs network access and `git` once; afterwards the local `limine/` directory is
reused.

**6. Assemble the bootable image.** The kernel, the RAM disk, and Limine's UEFI
boot files are copied into a staging tree, which `xorriso` turns into a bootable
CD image (`.iso`).

> 🧠 **First principles: a bootable image.** An *ISO* is a complete CD/DVD
> filesystem in a single file. Firmware can boot from it directly. Here it is the
> delivery package: it contains the bootloader, the kernel, and the RAM disk,
> arranged so UEFI firmware knows what to launch. `build/seed.iso` is the one
> artifact you'd burn to a USB stick to run SeedOS on real hardware — or, far
> more conveniently, hand to an emulator (§5).

## 5. Running under QEMU

```bash
make run
```

expands to:

```bash
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom build/seed.iso \
    -boot d -serial stdio
```

> 🧠 **First principles: QEMU and OVMF.** **QEMU** is a software computer — a
> program that pretends to be a full x86-64 PC (CPU, memory, screen, keyboard,
> serial port). Booting your OS inside it means you don't risk (or reboot) your
> real machine, and you can restart in a second. **OVMF** is open-source UEFI
> *firmware* for that virtual PC — the same role the firmware chip plays on a
> real motherboard: the very first code that runs, which then loads the
> bootloader. `-cdrom … -boot d` boots our ISO; `-bios OVMF.fd` supplies the
> firmware.

> 🧠 **First principles: the serial port.** `-serial stdio` wires the virtual
> machine's *serial port* — an old, dead-simple text channel — to your terminal.
> SeedOS prints its boot log there and even runs its shell over it, so you
> interact with the OS through your terminal window. (Real hardware has these
> ports too; they're a kernel developer's favorite because they work before
> graphics do.)

## 6. Debugging with GDB

```bash
make debug
```

runs the same QEMU command plus `-s -S`: `-s` starts a *GDB server* on TCP port
`:1234`, and `-S` freezes the virtual CPU at power-on so nothing runs until you
attach. In another terminal:

```bash
gdb build/kernel.elf -ex "target remote :1234"
```

> 🧠 **First principles: a source-level debugger, on a kernel.** **GDB** lets you
> pause execution, step line by line, inspect variables and registers, and set
> breakpoints. The trick here is that GDB runs on *your* machine while the kernel
> runs *inside QEMU*; the two talk over that `:1234` connection. Because the
> kernel was compiled with `-g` (debug info) and `-fno-omit-frame-pointer`, GDB
> can map machine addresses back to your C source and show clean backtraces.

The repository ships a `.gdbinit`, and the `.vscode/` directory plus the
`debug-vscode` target (which logs serial to `build/serial.log`) wire the same
flow into VS Code.

## 7. The RAM disk (initrd)

`scripts/mkinitrd.sh` builds the small filesystem the kernel mounts at boot.

> 🧠 **First principles: an initrd / RAM disk.** SeedOS has no disk driver yet,
> so its "filesystem" is an image loaded entirely into memory at boot, called an
> *initrd* (initial RAM disk). The bootloader loads it alongside the kernel; the
> kernel reads files out of it. It's formatted as **ext2**, a simple, classic
> Linux filesystem (Chapter 24).

The script avoids needing root: it `dd`s a blank image, formats it with
`mke2fs`, and writes files in with `debugfs` — by default a `hello.txt`, a
placeholder `/init`, and a `bin/` directory:

```bash
scripts/mkinitrd.sh build/initrd.ext2 2     # 2 MiB ext2 image
```

Limine loads this image as a *boot module*; `kmain()` finds it and hands it to
`ext2_init()`. You can watch the round-trip in the boot log — the kernel reads
`/hello.txt` back out and prints its contents.

## 8. Userspace and the test harness

User programs live under `userspace/` and build separately:

```bash
make userspace        # builds the libc + test programs in userspace/build/
```

To run a single userspace program straight from the RAM disk as the first
process, there is a small harness:

```bash
make test TEST=00_exit     # build kernel with auto-init, pack 00_exit as /init, boot headless
```

This compiles the kernel with `-DCONFIG_AUTO_INIT` (which makes `kmain()` launch
`/init` directly instead of the kernel shell), rebuilds the RAM disk with the
chosen test binary as `/init`, and boots QEMU headless with a 15-second timeout
so it can run unattended. `make test-exit` and `make test-write` are shortcuts.
Chapter 27 returns to these programs.

## Reference & cross-links

- **Previous:** [Chapter 1 — Introduction & Design Philosophy](01-introduction.md).
- **Next:** [Chapter 3 — A Tour of the Source Tree](03-source-tree.md) maps what
  you just compiled.
- **What happens after the firmware hands off — the linker script and `_start`
  in detail:** [Chapter 4 — The Boot Process](04-boot-process.md).
- **The ext2 RAM disk internals:** [Chapter 24 — ext2 & the RAM Disk](24-ext2.md).
- **The userspace programs you can run:**
  [Chapter 27 — Userspace Programs & Tests](27-userspace-programs.md).
