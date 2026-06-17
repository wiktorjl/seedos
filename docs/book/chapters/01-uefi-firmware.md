# Chapter 1 — UEFI: The Firmware Beneath Everything

> Part 0 — Understanding the Boot Environment · Status: ✅ drafted

## What this chapter covers

Before we boot *our* operating system, it pays to understand the environment our
OS is born into. When a modern PC powers on, a large, capable program is already
running before any operating system exists: the **UEFI firmware**. This chapter
explains what firmware is, what UEFI specifically provides, and how it turns a
cold machine into one ready to load an OS. Part 0 is exploratory — we won't write
SeedOS code yet; instead we get comfortable with the world that SeedOS (and its
bootloader, Limine) is handed.

> 🧭 **Note on Part 0.** These first chapters describe the *boot environment*, not
> SeedOS's own source — SeedOS delegates the messy UEFI details to Limine. We
> explore it directly so that, by Part I, you understand exactly what Limine is
> doing on your behalf.

## 1. What firmware is

**The concept.** **Firmware** is software stored in non-volatile memory on the
motherboard (historically a ROM chip) that runs the instant the CPU receives
power. Its job is to initialize the hardware — memory controller, buses, basic
devices — and then find and start the next stage of boot. **UEFI** (Unified
Extensible Firmware Interface) is the modern standard for this firmware, the
successor to the legacy "BIOS."

> 🐍 **From Python — the intuition.** Think of UEFI as a tiny operating system
> whose only purpose is to *find and launch your real operating system*. It even
> has a shell, a notion of files and drives, and the ability to run programs.
> Crucially, it exists *before* your OS, so it's the one piece of software you
> can rely on already being there.

**For example,** the boot sequence on a UEFI machine looks like:

```
power on → CPU starts in firmware → UEFI initializes hardware (POST)
        → UEFI looks for a bootloader on disk → runs it → bootloader loads the OS
```

**For us.** In this book you'll run SeedOS inside QEMU using **OVMF**, which is
exactly UEFI firmware compiled to run in the emulator (Chapter 7). Everything in
this chapter is happening, for real, every time you `make run`.

## 2. What UEFI provides

**The concept.** UEFI is not just a launcher — it's a rich runtime with a
documented programming interface. When firmware hands control to a program, it
passes a pointer to an **EFI System Table**, from which everything else is
reachable:

- **Boot Services** — callable functions available *before* the OS takes over:
  allocate memory, get the memory map, read files, locate devices and
  *protocols*. They vanish when the OS calls `ExitBootServices`.
- **Runtime Services** — a smaller set that survives into the running OS: get/set
  the time, access firmware variables, reboot the machine.
- **Protocols** — UEFI's interfaces to devices, looked up by ID. The most useful
  for us is the **Graphics Output Protocol (GOP)**, which hands you a linear
  **framebuffer** (a block of memory whose bytes are screen pixels).
- **The configuration table** — where firmware leaves pointers to standard
  structures, including the **ACPI RSDP** we'll need in Part III.

> 🐍 **From Python — the intuition.** UEFI is an **SDK for the pre-OS world**. The
> System Table is like a big object handed to your program; `BootServices` is its
> standard library (`malloc`, file I/O, device discovery), and protocols are like
> driver interfaces you request by name. The catch: it's all C function pointers
> and structs, and most of it disappears the moment your OS says "I'm taking
> over."

**For example,** a UEFI program prints a line by calling a function reached
through the System Table:

```c
SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Hello, UEFI!\r\n");
```

No standard library, no OS — just a function pointer the firmware provided.

**For us.** A bootloader's entire job is to *consume* these services — get the
memory map, set up a framebuffer, load the kernel from the EFI System Partition —
and package the results for the kernel. When we adopt Limine in Part I, this is
the work it does so SeedOS doesn't have to.

## 3. The UEFI shell

**The concept.** Many UEFI firmwares include (or can run) a **UEFI Shell**: an
interactive command line that is itself just an EFI application. From it you can
explore disks, inspect the memory map, and launch other `.efi` programs.

> 🐍 **From Python — the intuition.** It's a REPL for the firmware. Where a
> BIOS-era setup screen only let you tweak settings, the UEFI shell can *run real
> programs* — including the one you'll write in the next chapter, and even a Linux
> kernel.

**For example,** a short shell session:

```
Shell> fs0:                 # switch to the first filesystem (the boot disk)
FS0:\> ls                   # list files
FS0:\> memmap               # dump the UEFI memory map
FS0:\> EFI\BOOT\BOOTX64.EFI # run an EFI application by path
```

**For us.** SeedOS's boot media includes a tiny `startup.nsh` script
(`@echo -off` then `\EFI\BOOT\BOOTX64.EFI`) — a UEFI shell script that auto-launches
the Limine bootloader if the firmware ever drops to the shell. In Chapters 2–3
we'll use the shell deliberately, to boot our own EFI program and then Linux.

## 4. What state UEFI leaves the CPU in

**The concept.** Legacy BIOS started the first boot code in **16-bit real mode** —
a primitive environment requiring a painful climb up to 32-bit and then 64-bit
operation. UEFI on a 64-bit PC does that climb for you: it runs your boot program
in **long mode** (full 64-bit), with a flat memory layout and paging already
enabled.

> 🧠 **First principles: why this matters.** Entering long mode by hand is dozens
> of fiddly steps (enable the A20 line, build initial page tables, set control
> register bits, far-jump into 64-bit code). UEFI eliminates all of it. This is a
> large part of *why* modern hobby kernels target UEFI: you start where you
> actually want to be.

**For example,** the EFI "Hello World" of the next chapter is plain 64-bit C from
its very first instruction — no mode-switching assembly in sight.

**For us.** This is the foundation of SeedOS's "modern hardware, not legacy"
stance. Because UEFI (and then Limine) hand us a long-mode CPU, SeedOS has *no*
real-mode startup code at all — its `_start` (Chapter 5) is already 64-bit.

## Reference & cross-links

- **Next:** [Chapter 2 — Hello, Bare Metal: Your First EFI Application](02-efi-hello-world.md)
  puts these services to work in a program you boot yourself.
- **Then:** [Chapter 3 — vmlinuz Is Just a PE Executable](03-vmlinuz-pe.md) shows
  the Linux kernel is an EFI program too.
- **The hand-off we'll delegate:**
  [Chapter 4 — From UEFI to Limine](04-uefi-to-limine.md).
- **Running UEFI (OVMF) under the emulator:**
  [Chapter 7 — The Toolchain](07-toolchain.md).
