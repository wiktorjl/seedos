# Chapter 5 — x86-64 CPU Setup: GDT, Long Mode, FPU

> Part II — Boot & Architecture · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

Limine hands the kernel a CPU that is already in long mode with paging on, so
"CPU setup" in SeedOS is not about *entering* 64-bit mode — it is about replacing
the bootloader's temporary tables with the kernel's own. This chapter covers the
Global Descriptor Table and its careful segment ordering, the far-return trick
needed to reload `CS`, the Task State Segment with its privilege-change stack and
Interrupt Stack Table, the guard pages that catch IST overflow, and turning on
the FPU/SSE for userspace.

## Source files

- `arch/x86/kernel/gdt.c`, `gdt.h` — the GDT, TSS, and IST setup
- `arch/x86/kernel/gdt_asm.S` — `gdt_reload`: reloading the segment registers
- `arch/x86/kernel/fpu.c`, `fpu.h` — enabling and saving FPU/SSE state
- `arch/x86/kernel/cpu.h` — the small `sti`/`cli`/`hlt` primitives

## 1. What "setup" means when long mode is already on

In long mode, segmentation is mostly vestigial: the CPU ignores segment base and
limit for code and data, and addresses are formed through paging instead. So why
build a GDT at all? Three reasons, and they map onto the three halves of this
chapter:

1. **User-mode segments.** Limine's GDT only has kernel (ring-0) segments.
   Running code in ring 3 — and returning from it with `SYSRET` — requires
   user code and data descriptors laid out in a specific order.
2. **A Task State Segment.** The 64-bit TSS is where the CPU finds the kernel
   stack to switch to on a privilege change, and the special "known-good" stacks
   for critical exceptions.
3. **The FPU/SSE.** The kernel is compiled without SSE, but user programs need
   it, so the control registers must be configured to allow it.

`kmain()` calls `gdt_init()` early — before the IDT — because interrupt gates
reference the kernel code selector the GDT defines.

## 2. The GDT: seven entries, carefully ordered

`gdt_init()` builds seven slots (`arch/x86/kernel/gdt.c`):

| Slot | Selector | Segment | Access byte |
|------|----------|---------|-------------|
| 0 | `0x00` | NULL (required) | — |
| 1 | `0x08` | Kernel code (DPL 0, 64-bit) | `0x9A` |
| 2 | `0x10` | Kernel data (DPL 0) | `0x92` |
| 3 | `0x18` | **User data** (DPL 3) | `0xF2` |
| 4 | `0x20` | **User code** (DPL 3, 64-bit) | `0xFA` |
| 5–6 | `0x28` | TSS (16-byte descriptor) | `0x89` |

The ordering of slots 3 and 4 is not arbitrary — the header calls it out:

```c
/* IMPORTANT: User Data must come before User Code for SYSRET compatibility.
 * SYSRET loads: SS = STAR[63:48] + 8, CS = STAR[63:48] + 16 */
```

`SYSRET` computes the user segment selectors by *adding fixed offsets* to a base
stored in the `STAR` MSR, so the GDT has to place user data immediately before
user code to match. Get this wrong and returning to userspace loads the wrong
selectors. The 64-bit code segments set only the long-mode (`L`) flag; base and
limit are filled in but ignored by the hardware.

## 3. Reloading the segment registers

Loading the GDT with `lgdt` does not change the selectors currently in the
segment registers — those still point into Limine's old table. `gdt_reload`
(`gdt_asm.S`) fixes that. The data segments are a simple `mov`, but **`CS`
cannot be written directly**; it can only change via a far jump or far return.
SeedOS uses the far-return trick:

```asm
gdt_reload:
    mov $GDT_KERNEL_DATA, %ax      # ds/es/ss = 0x10
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax                   # clear fs/gs (gs set up later by per-CPU)
    mov %ax, %fs
    mov %ax, %gs

    pushq $GDT_KERNEL_CODE          # new CS selector
    leaq  .reload_cs(%rip), %rax
    pushq %rax                      # return address
    lretq                           # pops RIP and CS atomically
.reload_cs:
    ret
```

`FS` and `GS` are deliberately cleared here; `GS` is later pointed at the per-CPU
data area (Chapter 22). After `gdt_reload`, the TSS is loaded with
`ltr $0x28`.

## 4. The TSS: one kernel stack and seven special ones

In 64-bit mode the TSS no longer holds a full task context — it holds **stack
pointers**. SeedOS uses three of its fields:

- **`rsp0`** — the stack the CPU switches to when entering the kernel from ring 3
  (a syscall or an interrupt taken in user mode). `gdt_set_tss_rsp0()` updates it
  on every context switch so traps land on the *current* process's kernel stack.
- **`ist1`–`ist7`** — the Interrupt Stack Table: up to seven known-good stacks
  that specific interrupt vectors can be forced onto regardless of the current
  `RSP`.
- **`iopb_offset`** — set *beyond* the TSS limit, which disables the I/O
  permission bitmap and makes any `in`/`out` from ring 3 raise `#GP`.

`tss_init()` wires three IST entries to dedicated stacks for the exceptions that
can fire at the worst possible moment — when the regular kernel stack may itself
be corrupt:

```c
tss.ist1 = (uint64_t)&ist_nmi_stack[IST_STACK_TOTAL];   /* NMI  */
tss.ist2 = (uint64_t)&ist_df_stack[IST_STACK_TOTAL];    /* #DF  */
tss.ist3 = (uint64_t)&ist_mce_stack[IST_STACK_TOTAL];   /* #MCE */
```

Chapter 6 shows the other half of this wiring: the IDT gates for vectors 2, 8,
and 18 are given IST indices 1, 2, and 3 so the CPU uses these stacks.

## 5. Guard pages under the IST stacks

Each IST stack is laid out as 8 KiB of usable space above a 4 KiB **guard page**:

```
+------------+ <- top  (TSS points here; stack grows down)
| usable 8KB |
+------------+
| guard 4KB  |  unmapped after VMM init
+------------+ <- base
```

Right after `vmm_init()` captures the kernel page tables, `kmain()` calls
`gdt_install_ist_guards()`, which unmaps the guard page below each IST stack:

```c
if (vmm_unmap_page(kpml4, (uint64_t)stacks[i].base) < 0)
    log_warn("GDT: %s IST guard not installed ...", stacks[i].name);
```

Now an IST handler that overflows its stack walks into an unmapped page and takes
a clean `#PF` instead of silently scribbling over adjacent kernel BSS. (It is a
soft-fail: if the kernel happened to be mapped with huge pages it logs a warning
rather than panicking, because Limine's default 4 KiB mapping makes this the
normal case.)

## 6. The FPU and SSE — for userspace

The kernel is built `-mno-sse` and never touches vector registers itself, so
`fpu_init()` exists purely so *user* programs can use floating point. It flips
the relevant control-register bits and resets the x87 state:

```c
cr0 &= ~CR0_EM;   /* don't emulate the FPU — use the real hardware */
cr0 |=  CR0_MP;   /* monitor coprocessor                           */
cr0 &= ~CR0_TS;   /* no lazy FPU switching for now                 */
/* ... */
cr4 |=  CR4_OSFXSR;     /* enable fxsave/fxrstor and SSE          */
cr4 |=  CR4_OSXMMEXCPT;  /* enable SSE floating-point exceptions  */
__asm__ volatile("fninit");
```

Per-process FPU state is 512 bytes saved and restored with `fxsave`/`fxrstor`
(`fpu_save`/`fpu_restore`), and `fpu_init_state()` seeds a new process with sane
defaults — control word `0x037F` and `MXCSR` `0x1F80`, i.e. all exceptions
masked and round-to-nearest. Because switching is not lazy (`CR0.TS` stays
clear), this state travels with the process across context switches.

## 7. The CPU primitives

The smallest piece of this layer is `cpu.h`, a handful of `static inline`
wrappers used throughout the kernel:

```c
static inline void cpu_enable_interrupts(void)  { __asm__ volatile("sti"); }
static inline void cpu_disable_interrupts(void) { __asm__ volatile("cli"); }
static inline void cpu_halt(void)               { __asm__ volatile("hlt"); }
```

`kmain()` does not call `cpu_enable_interrupts()` until *after* the IDT and the
interrupt controllers are up — taking an IRQ before there is anywhere to deliver
it would be fatal. That is the subject of the next two chapters.

## Reference & cross-links

- **Previous:** [Chapter 4 — The Boot Process](04-boot-process.md).
- **Next:** [Chapter 6 — Interrupts & Exceptions: The IDT and ISRs](06-interrupts.md)
  installs the gates that reference the kernel code segment defined here, and
  uses the IST stacks set up here.
- **The per-CPU `GS` base cleared in `gdt_reload`:**
  [Chapter 22 — Per-CPU Data](22-percpu.md).
- **How `rsp0` is updated on every switch:**
  [Chapter 13 — Kernel Threads & the Scheduler](13-threads-scheduler.md) and
  [Chapter 17 — User Mode](17-user-mode.md).
