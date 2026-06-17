# Chapter 4 — The Boot Process: Limine to `kmain`

> Part II — Boot & Architecture · Status: ✅ drafted

> **Reference notes:** [`init-boot.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/init-boot.md)

## What this chapter covers

This chapter follows the very first moments of SeedOS: from the power button,
through firmware and the Limine bootloader, through a tiny assembly stub, to the
first line of C in `kmain()`. Each section introduces a concept (booting, a boot
protocol, the stack, memory layout), gives you a programmer's way to think about
it, shows a small example, and then points at the exact SeedOS code that does it.

## Source files

- `arch/x86/boot/boot.S` — `_start`: the assembly entry point
- `arch/x86/boot/limine.c` — protocol requests and accessor functions
- `arch/x86/boot/limine_asm.S` — the request-section start/end markers
- `arch/x86/boot/linker.ld` — places the kernel and its sections in memory
- `arch/x86/boot/limine.conf` — the bootloader's boot-entry configuration
- `arch/x86/boot/startup.nsh` — a UEFI-shell fallback launcher

## 1. What it means to "boot"

**The concept.** When a computer powers on, memory is empty and no program is
loaded — yet *something* must run. The CPU resolves this by starting execution at
a fixed address baked into the motherboard's **firmware** (a chip of read-only
code). The firmware initializes just enough hardware to find and load a
**bootloader**, which in turn loads the **kernel**. This staged hand-off — each
stage just capable enough to start the next — is *booting* (from "pulling
yourself up by your bootstraps").

> 🐍 **From Python — the intuition.** You have never seen this happen, because by
> the time `python` runs, the firmware, bootloader, and OS all started long ago.
> Booting is the answer to a chicken-and-egg question: *who runs first when
> nothing is loaded yet?* The answer is a relay — firmware → bootloader → kernel —
> where each runner is more specialized than the last and hands off the baton.

**For example,** the full SeedOS relay is:

```
power on → UEFI firmware → Limine (bootloader) → SeedOS kernel → the shell
```

**In SeedOS.** The kernel does not write its own firmware or bootloader — those
are huge, hardware-specific programs. It uses **Limine**, an existing bootloader,
and picks up the baton at a symbol called `_start`:

```
UEFI firmware → Limine → _start (boot.S) → kmain (init/main.c)
```

By the time Limine jumps to `_start`, it has already done the hard early work:
the CPU is in 64-bit **long mode**, paging is on, and the kernel is loaded at its
intended address. SeedOS's job is to collect the information Limine gathered and
take over.

## 2. The bootloader contract: a boot protocol

**The concept.** A **boot protocol** is the agreed-upon interface between a
bootloader and a kernel. It specifies how the kernel is loaded, what state the
machine is in at entry, and how the bootloader passes along facts the kernel
needs — chiefly the **memory map** (which physical address ranges are usable RAM)
and a **framebuffer** (a block of memory whose bytes are screen pixels). SeedOS
speaks the *Limine Boot Protocol*.

> 🐍 **From Python — the intuition.** Think of it as an API contract between two
> programs written by different people, or an HTTP request/response. The kernel
> *declares what it wants*; the bootloader *fills in the answers* before handing
> over control. Neither side hard-codes the other's internals — they agree on a
> data format.

**For example,** the kernel embeds a "framebuffer request" structure in its
binary; before jumping, the bootloader locates that structure and writes back a
pointer to a ready-to-use framebuffer. The kernel then reads the pointer and
starts drawing.

**In SeedOS.** `arch/x86/boot/limine.c` declares five requests and a thin
accessor for each:

```c
LIMINE_FRAMEBUFFER_REQUEST;   /* a linear framebuffer to draw on   */
LIMINE_HHDM_REQUEST;          /* the direct-map offset (Chapter 9)  */
LIMINE_MEMMAP_REQUEST;        /* the physical memory map           */
LIMINE_RSDP_REQUEST;          /* the ACPI pointer (Chapter 7)       */
LIMINE_MODULE_REQUEST;        /* boot modules — our ext2 RAM disk   */
```

How does Limine *find* these requests inside the kernel binary? They sit in a
dedicated section bracketed by two unique 64-bit "magic" numbers, defined in
`limine_asm.S`; Limine scans the loaded image for those constants:

```asm
.section .limine_requests_start, "aw", @progbits
limine_requests_start_marker:
    .quad 0xf6b8f4b39de7d1ae
    .quad 0xfab91a6940fcb9cf
    ...
```

The kernel also states which protocol revision it speaks. Revision 3 carries one
consequence worth flagging now, because it explains code in Chapter 7:

```c
/* Limine base revision 3: ACPI/reserved regions are NOT in the direct map,
 * so the kernel must map them explicitly before access. */
```

## 3. The first instructions: assembly, the stack, and `_start`

**The concept.** At `_start` the kernel is running raw machine code — there is no
C runtime yet. Recall from Chapter 1 that the CPU has a few dozen **registers**.
One of them, `rsp` (the *stack pointer*), points at the **stack**: a region of
memory that grows downward and holds function return addresses and local
variables. The CPU's `call`, `push`, and `pop` instructions all use `rsp`. So
**C function calls do not work until `rsp` points at real memory** — setting that
up is the entry stub's first duty.

> 🐍 **From Python — the intuition.** You already know the call stack: it's what a
> traceback prints, one frame per active function. In Python it's invisibly
> managed. Down here the stack is *literally* a block of bytes in RAM, and `rsp`
> is *literally* a number holding the address of the top byte. "Setting up the
> stack" just means pointing that number at some memory you've reserved.

**For example,** a `call kmain` instruction pushes the return address onto the
stack (decrementing `rsp` by 8) before jumping; when `kmain` eventually returns,
the CPU pops that address back. With `rsp` pointing at garbage, that first push
corrupts memory and the kernel dies instantly.

**In SeedOS.** `boot.S` reserves a 16 KiB stack and `_start` installs it. First,
though, it verifies Limine honored the requested protocol revision — Limine
signals success by *overwriting* the kernel's `magic1` value, so an unchanged
value means "unsupported, do not continue":

```asm
_start:
    mov limine_base_revision+8(%rip), %rax    # read magic1
    mov $0x6a7b384944536bdc, %rcx             # the original value
    cmp %rcx, %rax
    je .halt                                  # unchanged → unsupported → halt

    lea stack_top(%rip), %rsp                 # point rsp at our 16 KiB stack
    xor %rbp, %rbp                            # zero the frame pointer
    call kmain                                # into C
```

Two small touches matter later. The stack lives in the `.bss` section as
`stack_bottom … stack_top`. And `rbp` (the *frame pointer*, which chains stack
frames together) is zeroed so that the crash-time backtrace in Chapter 6 has a
clean stopping point at the very bottom of the call chain. `boot.S` also uses
`.incbin` to bake the font and logo binaries into the kernel image here — there
is no filesystem yet to load them from.

## 4. Where the kernel lives: sections and the linker script

**The concept.** A compiled program is split into **sections**: `.text` (the
machine-code instructions), `.rodata` (constants), `.data` (initialized
globals), and `.bss` (globals that start as zero, like our stack). Each section
is assigned a **virtual address** — the address the code will use at run time. A
**linker script** is the file that decides those addresses and the section order.
Most programs let the toolchain choose defaults; a kernel cannot, because the
bootloader loads it at the exact address recorded in its headers.

> 🐍 **From Python — the intuition.** You never think about *where* a function
> lives in memory — the import system and the OS handle it. A kernel has to pin
> everything down to fixed addresses, because there is no OS underneath to
> relocate it, and the higher-half design (Chapter 1) specifically requires the
> kernel to sit at the top of the address space.

**For example,** a linker script can say "begin at address *X*, then lay down
code, then read-only data, then writable data." Everything downstream — every
pointer, every jump — is computed relative to that starting address.

**In SeedOS.** `linker.ld` names the entry symbol and the load address, and
orders the sections — keeping the Limine request markers contiguous so the
bootloader's scan (Section 2) works:

```ld
ENTRY(_start)

SECTIONS
{
    . = 0xFFFFFFFF80000000;          /* higher half: top 2 GiB of the space */

    .text             : { *(.text .text.*) }
    . = ALIGN(4096);
    .limine_requests  : {            /* start marker, requests, end marker  */
        *(.limine_requests_start)
        *(.limine_requests)
        *(.limine_requests_end)
    }
    .rodata           : { *(.rodata .rodata.*) }
    .data             : { *(.data .data.*) }
}
```

That base address, `0xFFFFFFFF80000000`, *is* the "higher half." Limine reads
these section addresses from the kernel's ELF headers and loads each piece there
before jumping to `_start`.

## 5. `limine.conf` and the UEFI fallback

On the boot media, Limine reads a short text config telling it what to launch:

```
timeout: 0
/Seed OS
    protocol: limine
    path: boot():/boot/kernel.elf
    module_path: boot():/boot/initrd.ext2
```

`timeout: 0` boots instantly; the entry names the kernel and the ext2 RAM disk
module that `kmain` later mounts. The companion `startup.nsh` is a tiny UEFI-shell
script (`\EFI\BOOT\BOOTX64.EFI`) that launches Limine automatically if the
firmware ever drops to its shell instead of booting the media directly.

## 6. Into `kmain`

The baton finally reaches C. `kmain()` opens by collecting the framebuffer Limine
prepared, and bailing out if there isn't one:

```c
struct limine_framebuffer *fb = limine_get_framebuffer();
if (fb == NULL)
    return;
console_init(fb);
```

From here the ordered bring-up from [Chapter 1 §7](01-introduction.md) takes
over. Notice the protocol payload flowing straight into the next Part: the memory
map and direct-map offset Limine provided (`limine_get_memmap()`,
`limine_get_hhdm_offset()`) are handed directly to `pmm_init()` and `vmm_init()`
a few lines later. The bootloader's answers become the memory subsystem's inputs.

## Reference & cross-links

- **Previous:** [Chapter 3 — A Tour of the Source Tree](03-source-tree.md).
- **Next:** [Chapter 5 — x86-64 CPU Setup: GDT, Long Mode, FPU](05-cpu-setup.md).
- **What the embedded font/logo are and how they're built:**
  [Chapter 2 §4](02-building-and-running.md).
- **Why ACPI tables must be mapped by hand under revision 3:**
  [Chapter 7 — Hardware Discovery](07-acpi-apic.md).
- **The memory map and direct map this hands off:**
  [Chapter 8](08-physical-memory.md) and [Chapter 9](09-virtual-memory.md).
