# Chapter 2 — Hello, Bare Metal: Your First EFI Application

> Part 0 — Understanding the Boot Environment · Status: ✅ drafted

## What this chapter covers

The fastest way to demystify "bare metal" is to write a program that runs with no
operating system under it at all — a **UEFI application**. It's only a few lines,
it prints to the screen using the firmware services from Chapter 1, and you boot
it yourself in the emulator. By the end you'll have seen the simplest possible
"first program on a bare machine," which makes everything Limine and SeedOS do
afterward far less mysterious.

## 1. What an EFI application is

**The concept.** A UEFI application is an ordinary compiled executable that the
firmware loads into memory and calls at a known entry point. That entry point
receives two things: a handle to its own loaded image, and a pointer to the **EFI
System Table** from Chapter 1 — its gateway to every firmware service.

> 🐍 **From Python — the intuition.** It's like writing a plugin for a host that
> already exists. The "host" is the firmware; it loads your file and calls a
> function with a known signature, handing you an object (the System Table) full
> of services. You don't set up a runtime — the firmware *is* the runtime, for
> now.

**For example,** a complete, bootable "hello world" is this short:

```c
#include <efi.h>

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                      L"Hello from bare metal!\r\n");
    for (;;) { }            /* nothing to return to — just stop */
    return EFI_SUCCESS;
}
```

That's a real program that runs on a machine with no OS. The `L"..."` is a
*wide* (UTF-16) string, because UEFI text output speaks UTF-16.

**For us.** SeedOS's *kernel* isn't loaded this way — Limine loads it — but this
is the same kind of thing Limine itself is: a program the firmware launches. Seeing
the minimal version makes the bootloader feel ordinary rather than magical.

## 2. Building it

**The concept.** UEFI executables use the **PE/COFF** format (the same container
Windows `.exe` files use), with a special subsystem flag marking them as "EFI
Application." So building one means producing a PE, not the Linux **ELF** format
you might expect — typically with a UEFI toolkit like *gnu-efi*, or a compiler
targeting PE directly.

> 🧠 **First principles: why PE, not ELF?** The UEFI spec simply chose the PE/COFF
> format. It's a historical decision (UEFI grew out of Intel's work with
> Microsoft). The practical upshot: an EFI app is "a Windows-style executable that
> the firmware knows how to run," which is also why the next chapter can show that
> the *Linux* kernel image is a PE file.

**For example,** one common recipe links a freestanding object into a PE with the
EFI Application subsystem:

```bash
# with clang/lld targeting PE directly:
clang -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
      -mno-red-zone -c hello.c -o hello.o
lld-link -subsystem:efi_application -entry:efi_main hello.o -out:BOOTX64.EFI
```

The output, `BOOTX64.EFI`, is then placed on a FAT-formatted **EFI System
Partition** at the standard removable-media path `\EFI\BOOT\BOOTX64.EFI`, which
firmware launches automatically.

**For us.** Notice the freestanding flags (`-ffreestanding`, `-mno-red-zone`) —
the same family SeedOS uses in Chapter 7. The difference is only the *output
format*: a PE for the firmware here, versus an ELF for Limine later.

## 3. Running it

**The concept.** You don't need real hardware. QEMU plus the OVMF firmware can
boot your EFI app from a directory presented as a FAT disk.

**For example:**

```bash
mkdir -p esp/EFI/BOOT
cp BOOTX64.EFI esp/EFI/BOOT/BOOTX64.EFI
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
    -drive format=raw,file=fat:rw:esp
```

QEMU's `fat:` feature turns the `esp/` directory into a virtual FAT disk; OVMF
finds `\EFI\BOOT\BOOTX64.EFI` and runs it, and your message appears. (You can
also drop to the UEFI shell from Chapter 1 and launch it by hand.)

## 4. A taste of what the System Table can do

Printing is just the start. Through Boot Services you can do everything a
bootloader needs:

- **Allocate memory** — `BootServices->AllocatePool` / `AllocatePages`.
- **Get the memory map** — `BootServices->GetMemoryMap`, the list of usable RAM
  regions (the same information SeedOS's physical allocator needs in Part II).
- **Draw graphics** — locate the Graphics Output Protocol for a framebuffer.
- **Read files** — open the EFI System Partition and load more data (like a
  kernel).

The pivotal moment is **`ExitBootServices`**: once a loader has gathered
everything, it calls this to tell the firmware "step aside," after which Boot
Services are gone and the loaded OS owns the machine.

> 🧠 **First principles: `ExitBootServices` is the point of no return.** Up to that
> call, you can lean on the firmware's helpers. After it, there is *no* `malloc`,
> *no* file API, *no* print routine — only the framebuffer pointer, the memory
> map, and whatever you saved. That stark "now you're on your own" moment is
> exactly the world SeedOS lives in from its first instruction.

## 5. Why we don't keep writing raw EFI apps

You *could* build an entire OS as one giant EFI application — but it gets painful
fast. UEFI's APIs are verbose, the services disappear at `ExitBootServices`
anyway, and you'd be reimplementing memory and graphics setup that a bootloader
already does well. So in practice you write a small loader (or use an existing
one) to do the UEFI dance, and start your kernel in a clean, known state.

**For us.** That existing loader is **Limine**. The next chapter first shows that
even Linux rides on this same EFI-application mechanism, and Chapter 4 draws the
line between "what UEFI gives us" and "what we hand to Limine."

## Reference & cross-links

- **Previous:** [Chapter 1 — UEFI: The Firmware Beneath Everything](01-uefi-firmware.md).
- **Next:** [Chapter 3 — vmlinuz Is Just a PE Executable](03-vmlinuz-pe.md).
- **The bootloader we'll delegate the UEFI work to:**
  [Chapter 4 — From UEFI to Limine](04-uefi-to-limine.md) and
  [Chapter 5 — Minimal Boot via Limine](05-limine-boot.md).
- **The freestanding build flags, in our own kernel:**
  [Chapter 7 — The Toolchain](07-toolchain.md).
