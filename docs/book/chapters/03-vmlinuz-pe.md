# Chapter 3 — vmlinuz Is Just a PE Executable

> Part 0 — Understanding the Boot Environment · Status: 🚧 outline

## What this chapter covers

A demystifying detour: the Linux kernel image (`vmlinuz`) you can boot from the
UEFI shell is, itself, a PE/COFF executable with an **EFI boot stub**. Examining
it proves that "a kernel" is not a magical artifact — it's a program the firmware
launches, exactly like the one we wrote in Chapter 2. This builds intuition for
what SeedOS's own kernel image is.

## Outline

1. **Recap: PE/COFF and the EFI Application subsystem** (from Chapter 2).
2. **The EFI boot stub** — how Linux made `vmlinuz` directly bootable by UEFI,
   no separate bootloader required.
3. **Booting `vmlinuz` from the UEFI shell** — a hands-on example.
4. **Examining the image** — reading the PE header and sections with
   `llvm-readobj` / `objdump`; spotting the EFI entry point.
5. **What this teaches us** — a kernel is just a freestanding program with a
   known entry point and a way to receive boot information; SeedOS's `kernel.elf`
   is the same idea in ELF form (Chapter 5).

## Reference & cross-links

- **Previous:** [Chapter 2 — Hello, Bare Metal](02-efi-hello-world.md).
- **Next:** [Chapter 4 — From UEFI to Limine](04-uefi-to-limine.md).
