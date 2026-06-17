# Chapter 5 — x86-64 CPU Setup: GDT, Long Mode, FPU

> Part II — Boot & Architecture · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

Limine hands the kernel a CPU already in 64-bit long mode, so "CPU setup" here is
not about *entering* 64-bit mode — it is about replacing the bootloader's
temporary tables with the kernel's own. We'll cover three CPU data structures and
why each exists: the **GDT** (segments and privilege levels), the **TSS**
(stacks for crossing into the kernel), and the **control registers** that switch
on the floating-point hardware for user programs. Each follows the same arc:
concept, intuition, example, then the SeedOS code.

## Source files

- `arch/x86/kernel/gdt.c`, `gdt.h` — the GDT, TSS, and interrupt-stack setup
- `arch/x86/kernel/gdt_asm.S` — `gdt_reload`: activating the new segments
- `arch/x86/kernel/fpu.c`, `fpu.h` — enabling and saving FPU/SSE state
- `arch/x86/kernel/cpu.h` — the small `sti`/`cli`/`hlt` primitives

## 1. Segments, descriptors, and the GDT

**The concept.** On x86, memory accesses pass through **segments**, and the CPU
looks up each segment's properties in a table of **descriptors** called the
**Global Descriptor Table (GDT)**. A descriptor records a segment's privilege
level and type (code vs. data). In long mode, segmentation is mostly vestigial —
the CPU ignores segment base and limit — but two things still matter and *must*
come from the GDT: the **privilege level** of running code (ring 0 vs. ring 3,
from Chapter 1) and whether a segment is code or data.

> 🐍 **From Python — the intuition.** Forget the historical baggage and think of
> the GDT as a tiny lookup table the CPU consults to answer one question: "is the
> code running right now allowed to do privileged things?" Each entry is a row;
> a **selector** is just the index of a row (plus the privilege bits). When the
> kernel runs, a CPU register holds the selector for the ring-0 code row; when a
> user program runs, it holds the ring-3 code row.

> 🧠 **First principles: a selector.** A *selector* is a 16-bit value naming a GDT
> entry: `index × 8` plus low bits for the requested privilege. SeedOS's kernel
> code selector is `0x08`, kernel data `0x10`, user data `0x18`, user code
> `0x20`. You'll see these exact numbers in the interrupt and syscall code.

**For example,** consider entering the kernel from a user program via the
`SYSRET` instruction (Chapter 18). `SYSRET` doesn't take an arbitrary selector;
it *computes* the user segments by adding fixed offsets to a base in a CPU
register. That forces a specific ordering in the table — user *data* must come
immediately before user *code* — or returning to userspace loads the wrong
segments. The GDT layout is built to satisfy that constraint.

**In SeedOS.** `gdt_init()` (`arch/x86/kernel/gdt.c`) builds seven slots:

| Slot | Selector | Segment | Access byte |
|------|----------|---------|-------------|
| 0 | `0x00` | NULL (required by the CPU) | — |
| 1 | `0x08` | Kernel code (ring 0, 64-bit) | `0x9A` |
| 2 | `0x10` | Kernel data (ring 0) | `0x92` |
| 3 | `0x18` | **User data** (ring 3) | `0xF2` |
| 4 | `0x20` | **User code** (ring 3, 64-bit) | `0xFA` |
| 5–6 | `0x28` | TSS — 16-byte descriptor (§3) | `0x89` |

The header spells out the ordering rule from our example:

```c
/* IMPORTANT: User Data must come before User Code for SYSRET compatibility.
 * SYSRET loads: SS = STAR[63:48] + 8, CS = STAR[63:48] + 16 */
```

## 2. Activating the GDT: reloading the segment registers

**The concept.** Loading the GDT's address with the `lgdt` instruction does *not*
change which selectors the CPU is currently using — the segment registers still
hold the bootloader's old selectors. They must be reloaded. Most segment
registers take a plain `mov`, but **`CS` (the code selector) cannot be written
directly**; the CPU only changes it through a jump or return that also changes
the instruction pointer.

> 🧠 **First principles: the far-return trick.** To swap `CS` atomically with the
> instruction pointer, you push the new selector and a return address on the
> stack and execute a *far return* (`lretq`), which pops both at once. It's a
> controlled "jump that also changes privilege/segment context."

**In SeedOS.** `gdt_reload` (`gdt_asm.S`) reloads the data segments with a `mov`,
clears `FS`/`GS` (the per-CPU `GS` base is set up later, Chapter 22), and uses the
far-return trick for `CS`:

```asm
    pushq $GDT_KERNEL_CODE          # new CS selector (0x08)
    leaq  .reload_cs(%rip), %rax
    pushq %rax                      # return address
    lretq                           # pops RIP and CS together
.reload_cs:
    ret
```

After this, `gdt_init()` loads the TSS with `ltr $0x28`.

## 3. The TSS: a stack for crossing into the kernel

**The concept.** When the CPU jumps from a user program (ring 3) into the kernel
(ring 0) — for a system call or an interrupt — it must switch to a *kernel*
stack, because trusting the user's stack pointer would be a security hole. Where
does it find that kernel stack? In the **Task State Segment (TSS)**. In 64-bit
mode the TSS no longer holds a full task context; it holds **stack pointers**:
`rsp0` (the kernel stack to switch to on a privilege change) and `ist1`–`ist7`
(the **Interrupt Stack Table**: known-good stacks that specific interrupts are
forced onto, no matter what `rsp` was).

> 🐍 **From Python — the intuition.** Imagine that whenever any thread calls into
> a privileged service, the runtime instantly swaps it onto a fresh, trusted
> stack so the service never runs on caller-controlled memory. `rsp0` is that
> "fresh trusted stack" address, and the kernel updates it on every context
> switch so the trap always lands on the *current* process's kernel stack.

**For example,** consider a "double fault" — an error that happens *while
handling another error*. The regular kernel stack may already be corrupt or
exhausted. The IST lets you tell the CPU "for this particular vector, always use
*this* dedicated stack," so even a fault-during-a-fault has somewhere safe to run.

**In SeedOS.** `tss_init()` wires three IST entries to dedicated stacks for the
exceptions that can fire at the worst moment, and sets the I/O-permission offset
past the TSS so ring 3 can't touch I/O ports:

```c
tss.ist1 = (uint64_t)&ist_nmi_stack[IST_STACK_TOTAL];   /* NMI  */
tss.ist2 = (uint64_t)&ist_df_stack[IST_STACK_TOTAL];    /* #DF  */
tss.ist3 = (uint64_t)&ist_mce_stack[IST_STACK_TOTAL];   /* #MCE */
```

`gdt_set_tss_rsp0()` updates `rsp0` on each context switch. (Chapter 6 supplies
the other half: the IDT entries for these vectors carry the matching IST index.)

**A safety touch — guard pages.** Each IST stack is laid out as 8 KiB of usable
space above a 4 KiB **guard page**:

```
+------------+ <- top  (the TSS points here; the stack grows down)
| usable 8KB |
+------------+
| guard 4KB  |  unmapped after paging is up
+------------+ <- base
```

Right after `vmm_init()`, `kmain()` calls `gdt_install_ist_guards()`, which
unmaps that guard page. Now a handler that overflows its stack walks into
unmapped memory and takes a clean page fault, instead of silently scribbling over
the kernel's data.

## 4. Control registers and turning on the FPU/SSE

**The concept.** Besides the general registers, the CPU has a few **control
registers** (`CR0`, `CR4`, …) whose individual bits switch hardware features on
and off. Two features here are the **x87 FPU** (the classic floating-point unit)
and **SSE** (the vector unit, used for modern floating-point). They start
disabled; software must enable them before any program uses a `float`.

> 🐍 **From Python — the intuition.** Control-register bits are the CPU's
> global feature flags — like environment variables that gate hardware behavior,
> except writing them is a privileged instruction. Flipping one bit can enable an
> entire instruction set.

> 🧠 **First principles: why the kernel skips the FPU.** SeedOS is compiled
> `-mno-sse` (Chapter 2), so the *kernel itself* never uses floating-point
> registers. `fpu_init()` exists purely so *user* programs can. This keeps the
> kernel's own context switches cheap — it has no FPU state to save.

**For example,** enabling SSE is a matter of setting `CR4.OSFXSR` (which also
turns on the `fxsave`/`fxrstor` instructions used to save and restore the 512-byte
FPU/SSE state) and clearing `CR0.EM` (which would otherwise force FPU
instructions to be emulated/trapped).

**In SeedOS.** `fpu_init()` flips exactly those bits and resets the x87 state:

```c
cr0 &= ~CR0_EM;          /* use the real FPU, don't emulate */
cr0 |=  CR0_MP;
cr0 &= ~CR0_TS;          /* no lazy FPU switching for now    */
cr4 |=  CR4_OSFXSR;       /* enable fxsave/fxrstor and SSE    */
cr4 |=  CR4_OSXMMEXCPT;   /* enable SSE FP exceptions         */
__asm__ volatile("fninit");
```

Each user process gets 512 bytes of FPU/SSE state saved and restored with
`fxsave`/`fxrstor`, seeded by `fpu_init_state()` with sane defaults (control word
`0x037F`, `MXCSR` `0x1F80` — all exceptions masked, round to nearest).

## 5. The CPU primitives

The smallest piece of this layer is `cpu.h`, a few `static inline` wrappers used
everywhere:

```c
static inline void cpu_enable_interrupts(void)  { __asm__ volatile("sti"); }
static inline void cpu_disable_interrupts(void) { __asm__ volatile("cli"); }
static inline void cpu_halt(void)               { __asm__ volatile("hlt"); }
```

`kmain()` deliberately does not call `cpu_enable_interrupts()` until *after* the
IDT and the interrupt controllers exist — taking an interrupt before there is
anywhere to deliver it would be fatal. That is the next two chapters.

## Reference & cross-links

- **Previous:** [Chapter 4 — The Boot Process](04-boot-process.md).
- **Next:** [Chapter 6 — Interrupts & Exceptions: The IDT and ISRs](06-interrupts.md)
  installs gates that reference the kernel code selector and IST stacks defined
  here.
- **The user/kernel privilege boundary these segments enforce:**
  [Chapter 17 — User Mode](17-user-mode.md) and
  [Chapter 18 — The Syscall Interface](18-syscalls.md).
- **The per-CPU `GS` base cleared in `gdt_reload`:**
  [Chapter 22 — Per-CPU Data](22-percpu.md).
