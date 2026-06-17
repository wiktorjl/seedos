# Chapter 4 — The Boot Process: Limine to `kmain`

> Part II — Boot & Architecture · Status: ✅ drafted

> **Reference notes:** [`init-boot.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/init-boot.md)

## What this chapter covers

This chapter follows the very first moments of SeedOS: from UEFI firmware
handing control to the Limine bootloader, through the assembly entry stub, to
the first line of C in `kmain()`. By the end you will understand the Limine
request/response protocol, the magic-number handshake in `_start`, how the
linker script places the kernel in the higher half, and exactly what state the
machine is in when C code takes over.

## Source files

- `arch/x86/boot/boot.S` — `_start`: the assembly entry point
- `arch/x86/boot/limine.c` — protocol requests and accessor functions
- `arch/x86/boot/limine_asm.S` — the request-section start/end markers
- `arch/x86/boot/linker.ld` — places the kernel and its sections in memory
- `arch/x86/boot/limine.conf` — the bootloader's boot-entry configuration
- `arch/x86/boot/startup.nsh` — a UEFI-shell fallback launcher

## 1. Where the kernel begins

SeedOS does not write its own bootloader. It relies on **Limine**, a modern
bootloader that speaks the *Limine Boot Protocol*. The full chain is:

```
UEFI firmware → Limine → _start (boot.S) → kmain (init/main.c)
```

By the time Limine jumps to the kernel, it has already done the work that older
kernels do by hand: the CPU is in **64-bit long mode**, **paging is on**, the
kernel image is loaded at its linked address, and a set of *boot information* —
a framebuffer, a memory map, the ACPI pointer, and more — has been prepared for
the kernel to read. SeedOS's job is to *ask* for that information up front and
*collect* it once running.

## 2. Asking Limine for what we need

The protocol works by request and response. The kernel embeds a set of *request*
structures in a dedicated section of its binary; before jumping to the kernel,
Limine scans the image, finds those requests, and fills in their `response`
pointers. SeedOS declares five (`arch/x86/boot/limine.c`):

```c
LIMINE_FRAMEBUFFER_REQUEST;   /* a linear framebuffer to draw on   */
LIMINE_HHDM_REQUEST;          /* the Higher-Half Direct Map offset */
LIMINE_MEMMAP_REQUEST;        /* the physical memory map           */
LIMINE_RSDP_REQUEST;          /* the ACPI RSDP pointer             */
LIMINE_MODULE_REQUEST;        /* boot modules (our ext2 initrd)    */
```

Each has a thin accessor that returns the response or `NULL` if Limine did not
provide it — for example `limine_get_framebuffer()` returns the first
framebuffer, and `limine_get_module(0)` returns the initrd. The kernel never
touches Limine's structures directly after boot; it pulls what it needs through
these functions.

**How Limine finds the requests.** The requests live in a `.limine_requests`
section bracketed by two magic markers defined in `limine_asm.S`:

```asm
.section .limine_requests_start, "aw", @progbits
limine_requests_start_marker:
    .quad 0xf6b8f4b39de7d1ae
    .quad 0xfab91a6940fcb9cf
    ...
```

Limine searches the loaded image for those 64-bit constants to locate the
request area — which is why the linker script (Section 4) is careful to keep the
start marker, the requests, and the end marker contiguous and in order.

**Base revision 3.** The kernel also declares which protocol revision it speaks:

```c
volatile uint64_t limine_base_revision[3] __attribute__((section(".limine_requests"))) = {
    0xf9562b2d5c95a6c8,     /* magic0 */
    0x6a7b384944536bdc,     /* magic1 (replaced on success) */
    3                       /* requested revision */
};
```

Revision 3 has one important consequence noted right in the source: **ACPI and
reserved regions are *not* included in the HHDM**, so the kernel must map them
explicitly before touching them. That is exactly why the ACPI parser in
Chapter 7 maps tables by hand instead of just dereferencing a pointer.

## 3. The handshake in `_start`

The entry point is hand-written assembly (`arch/x86/boot/boot.S`). Its first job
is to confirm the bootloader actually honored the requested protocol revision.
Limine signals success by *overwriting* `magic1` with the supported revision; if
the value is unchanged, the protocol is unsupported and continuing would be
unsafe:

```asm
_start:
    mov limine_base_revision+8(%rip), %rax    # read magic1 (offset +8)
    mov $0x6a7b384944536bdc, %rcx             # the original magic1
    cmp %rcx, %rax
    je .halt                                  # unchanged → unsupported → halt

    lea stack_top(%rip), %rsp                 # install our own 16 KiB stack
    xor %rbp, %rbp                            # zero frame pointer (ends backtraces)
    call kmain
```

Three things happen on the happy path: the kernel switches to its **own 16 KiB
stack** (defined in `.bss` as `stack_bottom … stack_top`), it **zeroes `%rbp`**
so that stack-walking backtraces — like the one in Chapter 6 — terminate
cleanly, and it **calls `kmain`**. If the handshake fails, `_start` falls into a
`hlt`/`jmp` halt loop.

`boot.S` also embeds the two binary assets from Chapter 2 here, via `.incbin`:
`font_data` (the console font) and `logo_data` (the boot splash), so they are
part of the kernel image with no filesystem required.

## 4. Where the kernel lives

The linker script `arch/x86/boot/linker.ld` declares the entry symbol and the
load address, and lays out the sections:

```ld
ENTRY(_start)

SECTIONS
{
    . = 0xFFFFFFFF80000000;          /* higher half: top 2 GiB of the address space */

    .text             : { *(.text .text.*) }
    . = ALIGN(4096);
    .limine_requests  : {            /* kept contiguous so Limine can scan it */
        *(.limine_requests_start)
        *(.limine_requests)
        *(.limine_requests_end)
    }
    .rodata           : { *(.rodata .rodata.*) }
    .data             : { *(.data .data.*) }
}
```

The base address `0xFFFFFFFF80000000` is the higher-half convention from
Chapter 1: the kernel occupies the top of the canonical address space, leaving
the entire lower half for user programs. Limine reads these ELF headers and
loads each section at the address the script assigns before jumping to `_start`.

## 5. `limine.conf` and the UEFI fallback

On the media side, Limine reads a small text config (`arch/x86/boot/limine.conf`):

```
timeout: 0

/Seed OS
    protocol: limine
    path: boot():/boot/kernel.elf
    module_path: boot():/boot/initrd.ext2
```

`timeout: 0` boots instantly; the entry names the kernel ELF and the ext2 initrd
module (the one `kmain` later mounts). The companion `startup.nsh` is a UEFI
shell script — `\EFI\BOOT\BOOTX64.EFI` — that launches Limine automatically if
the firmware ever drops to the UEFI shell instead of booting the removable-media
path directly.

## 6. Into `kmain`

`kmain()` opens by collecting the framebuffer and bailing if there isn't one:

```c
struct limine_framebuffer *fb = limine_get_framebuffer();
if (fb == NULL)
    return;
console_init(fb);
```

From here the ordered bring-up described in [Chapter 1 §3](01-introduction.md)
takes over. Notice the dependency already in play: the memory map and HHDM
offset that Limine provided (`limine_get_memmap()`,
`limine_get_hhdm_offset()`) are handed straight into `pmm_init()` and
`vmm_init()` a few lines later — the bootloader's hand-off feeds directly into
the memory subsystem of the next Part.

## Reference & cross-links

- **Previous:** [Chapter 3 — A Tour of the Source Tree](03-source-tree.md).
- **Next:** [Chapter 5 — x86-64 CPU Setup: GDT, Long Mode, FPU](05-cpu-setup.md).
- **What the embedded font/logo are and how they're built:**
  [Chapter 2 §4](02-building-and-running.md).
- **Why ACPI tables must be mapped by hand under revision 3:**
  [Chapter 7 — Hardware Discovery](07-acpi-apic.md).
- **The memory map and HHDM that boot hands off:**
  [Chapter 8](08-physical-memory.md) and [Chapter 9](09-virtual-memory.md).
