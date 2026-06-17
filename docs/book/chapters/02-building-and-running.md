# Chapter 2 — Building & Running SeedOS

> Part I — Foundations · Status: ✅ drafted

## What this chapter covers

This chapter gets a SeedOS kernel built and booting on your machine, and
explains what the build system is actually doing while it does it. By the end
you will know the prerequisites, the handful of `make` targets you will use
daily, the pipeline that turns C and assembly into a bootable UEFI ISO, and how
to run and debug the result under QEMU.

## Source files

- `Makefile` — the entire build, from `.c`/`.S` to `build/seed.iso`
- `configure` — a dependency checker you run once
- `scripts/download-font.sh` — fetches and bakes the console font (`data/font.bin`)
- `scripts/convert-image.sh` — turns the boot logo PNG into raw pixels
- `scripts/mkinitrd.sh` — builds the ext2 RAM disk (`build/initrd.ext2`)

## 1. Prerequisites

SeedOS builds with your system's ordinary toolchain — there is no separate
cross-compiler to install. Run the bundled checker first:

```bash
./configure
```

It verifies the **required** tools — `cc` (GCC or Clang), `ld`, `make`,
`xorriso`, and `git` — and the **optional** ones — `bear` (for the
clangd compile database), `qemu-system-x86_64`, and `gdb`. It also looks for
UEFI firmware (OVMF) in the usual locations.

A few build-time dependencies are *not* yet covered by `configure` but are
needed in practice:

- **e2fsprogs** (`mke2fs`, `debugfs`) — to build the ext2 initrd.
- **ImageMagick** (`convert`, `identify`) and **bc** — the boot logo
  (`build/logo.bin`) is regenerated from `data/seedos.png` on every clean build.
- **OVMF firmware** at `/usr/share/ovmf/OVMF.fd` (override with `make OVMF=...`).

### First-time setup: generate the font

The console font binary is *generated, not checked in* — `data/font.bin` matches
the repository's `*.bin` ignore rule. The kernel's `boot.S` embeds it directly,
so on a fresh clone you must create it once:

```bash
scripts/download-font.sh      # downloads Spleen 8×16 → data/font.bin (needs network)
```

This fetches the Spleen bitmap font, strips the PSF header, and writes the raw
256-glyph × 16-byte table to `data/font.bin`. The boot logo, by contrast, is
built automatically from the tracked `data/seedos.png`, so you only run
`convert-image.sh` indirectly through `make`.

## 2. The targets you will use

```bash
make          # Build the bootable ISO (build/seed.iso) — the default target
make run      # Build, then boot it in QEMU with serial wired to your terminal
make debug    # Same as run, but pause at reset and expose a GDB server (:1234)
make clean    # Remove the build/ directory
make compdb   # Generate compile_commands.json for clangd
```

There are a few more specialized targets — `initrd` (build just the RAM disk),
`userspace` (build the user programs), the `test*` family (Section 8), and
`show-sources` (print the discovered source/object lists, handy when debugging
the Makefile itself).

## 3. What `make` actually does

The default target produces `build/seed.iso` through a short pipeline. Each
stage maps directly onto a rule in the `Makefile`.

**1. Discover sources.** The Makefile globs every `.c` and `.S` under the
source directories:

```make
C_SRCS   := $(shell find arch kernel mm fs drivers init lib demos -name '*.c' ...)
ASM_SRCS := $(shell find arch kernel -name '*.S' ...)
```

Object files are *flattened* into `build/` (so `mm/pmm.c` becomes
`build/pmm.o`), with a `VPATH` telling Make where to find each source. Adding a
new `.c` file to one of those directories is enough for it to be picked up — no
Makefile edit required.

**2. Compile, freestanding.** Every C file is compiled with the freestanding
flags introduced in Chapter 1:

```make
CFLAGS := -g -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=kernel \
          -fno-omit-frame-pointer $(INCLUDES)
```

Assembly files are assembled by the same `cc`, with the `build/` and `data/`
directories added to the include path so `boot.S` can `.incbin` its embedded
binaries (Section 4).

**3. Link with a custom script.** All objects are linked statically, with no
standard library, using the kernel linker script that places the image in the
higher half:

```make
$(KERNEL): $(OBJS) arch/x86/boot/linker.ld
	ld -nostdlib -static -T arch/x86/boot/linker.ld -o $@ $(OBJS)
```

The result is `build/kernel.elf`.

**4. Build the initrd.** `scripts/mkinitrd.sh` creates a small ext2 image
(`build/initrd.ext2`) — see Section 7.

**5. Fetch and build Limine.** On the first build, the `limine` target clones
the Limine bootloader (the `v8.x-binary` branch, shallow) and builds its host
tools. This needs network access and `git` once; afterwards the local `limine/`
directory is reused.

**6. Assemble the ISO.** The kernel, the initrd, the Limine UEFI artifacts
(`limine-uefi-cd.bin`, `BOOTX64.EFI`), the bootloader config
(`arch/x86/boot/limine.conf`), and a `startup.nsh` are copied into an
`iso_root/` staging tree, which `xorriso` turns into a hybrid UEFI-bootable ISO:

```make
xorriso -as mkisofs -R -r -J -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    $(ISO_ROOT) -o $(ISO)
```

That `build/seed.iso` is what QEMU (or real UEFI hardware) boots.

## 4. Embedded assets: font and logo

SeedOS has no filesystem at the moment it first draws to the screen, so two
binary assets are compiled *into* the kernel and referenced from `boot.S` with
`.incbin`:

```asm
.global font_data
font_data:
    .incbin "font.bin"      # Spleen 8x16 bitmap font (256 glyphs, 16 bytes each)

.global logo_data
logo_data:
    .incbin "logo.bin"      # Logo image (raw BGRA pixels)
```

- **`data/font.bin`** comes from `download-font.sh` (Section 1) and is the
  console's 8×16 glyph table.
- **`build/logo.bin`** is produced on each build by `convert-image.sh`, which
  uses ImageMagick to scale `data/seedos.png` down to roughly 150 000 pixels and
  emit raw BGRA data, and also generates `lib/logo.h` with the final
  `LOGO_WIDTH`/`LOGO_HEIGHT`.

Because `build/boot.o` depends on both files, the build knows to regenerate the
logo (and expects the font to exist) before assembling the entry stub.

## 5. Running under QEMU

```bash
make run
```

expands to:

```bash
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom build/seed.iso \
    -boot d -serial stdio
```

`-bios OVMF.fd` supplies UEFI firmware (SeedOS does not boot legacy BIOS);
`-cdrom … -boot d` boots the ISO; and `-serial stdio` wires the kernel's COM1
serial port to your terminal. Because serial receive is interrupt-driven, the
shell is fully usable over that serial line, not just the framebuffer — handy
for copy/paste and logging.

## 6. Debugging with GDB

```bash
make debug
```

runs the same QEMU command plus `-s -S`: `-s` starts a GDB server on TCP
`:1234`, and `-S` freezes the CPU at reset so nothing executes until you attach.
In another terminal:

```bash
gdb build/kernel.elf -ex "target remote :1234"
```

Because the kernel is built with `-g` and `-fno-omit-frame-pointer`, you get
symbols and clean backtraces. The repository ships a `.gdbinit`, and the
`.vscode/` directory plus the `debug-vscode` target (which logs serial to
`build/serial.log`) wire the same flow into VS Code.

## 7. The initrd (ext2 RAM disk)

`scripts/mkinitrd.sh` builds the root filesystem image the kernel mounts at
boot. It deliberately avoids needing root: it `dd`s a blank image, formats it
with `mke2fs`, and uses `debugfs -w` to write files in — by default a
`hello.txt`, a placeholder `/init`, and a `bin/` directory:

```bash
scripts/mkinitrd.sh build/initrd.ext2 2     # 2 MiB ext2 image
```

Limine loads this image as a boot module; `kmain()` finds it via
`limine_get_module(0)` and hands it to `ext2_init()`. You can watch the
round-trip in the boot log — the kernel reads `/hello.txt` back out and prints
its contents. Chapter 24 covers the ext2 reader in detail.

## 8. Userspace and the test harness

User programs live under `userspace/` and build separately:

```bash
make userspace        # builds the libc + test programs in userspace/build/
```

For running a single userspace program straight from the initrd as PID-equivalent
`/init`, there is a small harness:

```bash
make test TEST=00_exit     # build kernel with auto-init, pack 00_exit as /init, boot headless
```

This compiles the kernel with `-DCONFIG_AUTO_INIT` (which makes `kmain()` call
`start_init()` instead of launching the kernel shell), rebuilds the initrd with
the chosen test binary as `/init`, and boots QEMU headless with a 15-second
timeout so it can run unattended. `make test-exit` and `make test-write` are
shortcuts. Chapter 27 returns to these programs.

## Reference & cross-links

- **Previous:** [Chapter 1 — Introduction & Design Philosophy](01-introduction.md).
- **Next:** [Chapter 3 — A Tour of the Source Tree](03-source-tree.md) maps what
  you just compiled.
- **What happens after `_start`:** [Chapter 4 — The Boot Process](04-boot-process.md).
- **The ext2 initrd internals:** [Chapter 24 — ext2 & the RAM Disk](24-ext2.md).
- **The userspace programs you can run:**
  [Chapter 27 — Userspace Programs & Tests](27-userspace-programs.md).
