# Chapter 21 — User Mode: Rings, the GDT & the TSS

> Part V — User Space · Status: ✅ drafted

> **Reference notes:** [`arch-x86.md`](https://github.com/wiktorjl/seedos/blob/master/docs/reference/arch-x86.md)

## What this chapter covers

Everything so far has run in **ring 0** — full-privilege kernel mode. To run
*user* programs safely we need **ring 3**, and the CPU machinery that separates
the two: the **GDT** (which defines the privileged and unprivileged code/data
segments), the **TSS** (which supplies a trusted kernel stack when user code traps
in), and the **control registers** that enable the FPU user programs expect. This
chapter builds those, ending ready for the system-call gate in Chapter 22.

> 🧭 **A note on ordering.** These CPU tables are actually set up *early* in boot —
> the kernel needs a GDT and interrupt stacks before Part I's exception handling
> works. We cover them here, in Part V, because their *reason for existing* — the
> ring 0 / ring 3 boundary that protects the kernel from user programs — only
> becomes relevant now. Each section is concept → intuition → example → SeedOS
> code.

## Source files

- `arch/x86/kernel/gdt.c`, `gdt.h` — the GDT, TSS, and interrupt-stack setup
- `arch/x86/kernel/gdt_asm.S` — `gdt_reload`: activating the new segments
- `arch/x86/kernel/fpu.c`, `fpu.h` — enabling and saving FPU/SSE state
- `arch/x86/kernel/cpu.h` — the small `sti`/`cli`/`hlt` primitives

## 1. Segments, descriptors, and the GDT

**The concept.** On x86, memory accesses pass through **segments**, and the CPU
looks up each segment's properties in a table of **descriptors** called the
**Global Descriptor Table (GDT)**. A descriptor records a segment's privilege
level and type. In long mode, segmentation is mostly vestigial — the CPU ignores
segment base and limit — but two things still come from the GDT: the **privilege
level** of running code (ring 0 vs. ring 3) and whether a segment is code or data.

> 🐍 **From Python — the intuition.** Think of the GDT as a tiny lookup table the
> CPU consults to answer one question: "is the code running right now allowed to
> do privileged things?" Each entry is a row; a **selector** is just the index of
> a row (plus privilege bits). When the kernel runs, a CPU register holds the
> selector for the ring-0 code row; when a user program runs, it holds the ring-3
> code row.

> 🧠 **First principles: a selector.** A *selector* is a 16-bit value naming a GDT
> entry: `index × 8` plus low bits for the requested privilege. SeedOS's kernel
> code selector is `0x08`, kernel data `0x10`, user data `0x18`, user code
> `0x20` — the exact numbers you've already seen in the interrupt code (Chapter 10).

**For example,** consider returning to a user program via the `SYSRET` instruction
(Chapter 22). `SYSRET` doesn't take an arbitrary selector; it *computes* the user
segments by adding fixed offsets to a base in a CPU register. That forces a
specific ordering — user *data* must come immediately before user *code* — or the
return loads the wrong segments. The GDT layout is built to satisfy that.

**In SeedOS.** `gdt_init()` builds seven slots:

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

**The concept.** Loading the GDT's address with `lgdt` does *not* change which
selectors the CPU is currently using. They must be reloaded. Most segment
registers take a plain `mov`, but **`CS` (the code selector) cannot be written
directly**; the CPU only changes it through a jump or return that also changes the
instruction pointer.

> 🧠 **First principles: the far-return trick.** To swap `CS` atomically with the
> instruction pointer, push the new selector and a return address, then execute a
> *far return* (`lretq`), which pops both at once.

**In SeedOS.** `gdt_reload` (`gdt_asm.S`) reloads the data segments, clears
`FS`/`GS` (the per-CPU `GS` base is set up alongside the syscall entry, Chapter
22), and uses the far-return trick for `CS`:

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
(ring 0) — for a system call or an interrupt — it must switch to a *kernel* stack,
because trusting the user's stack pointer would be a security hole. It finds that
stack in the **Task State Segment (TSS)**. In 64-bit mode the TSS holds **stack
pointers**: `rsp0` (the kernel stack to switch to on a privilege change) and
`ist1`–`ist7` (the **Interrupt Stack Table**: known-good stacks that specific
interrupts are forced onto, no matter what `rsp` was).

> 🐍 **From Python — the intuition.** Imagine that whenever a thread calls into a
> privileged service, the runtime instantly swaps it onto a fresh, trusted stack
> so the service never runs on caller-controlled memory. `rsp0` is that "fresh
> trusted stack" address, updated on every context switch so the trap always
> lands on the *current* process's kernel stack.

**For example,** consider a "double fault" — an error that happens *while handling
another error*, when the regular kernel stack may already be corrupt. The IST lets
you say "for this vector, always use *this* dedicated stack," so even a
fault-during-a-fault has somewhere safe to run.

**In SeedOS.** `tss_init()` wires three IST entries to dedicated stacks and sets
the I/O-permission offset past the TSS so ring 3 can't touch I/O ports:

```c
tss.ist1 = (uint64_t)&ist_nmi_stack[IST_STACK_TOTAL];   /* NMI  */
tss.ist2 = (uint64_t)&ist_df_stack[IST_STACK_TOTAL];    /* #DF  */
tss.ist3 = (uint64_t)&ist_mce_stack[IST_STACK_TOTAL];   /* #MCE */
```

`gdt_set_tss_rsp0()` updates `rsp0` on each context switch. (Chapter 10 supplies
the other half: the IDT entries for these vectors carry the matching IST index.)

**A safety touch — guard pages.** Each IST stack is 8 KiB of usable space above a
4 KiB **guard page** that `gdt_install_ist_guards()` unmaps once paging is up
(Chapter 13). A handler that overflows its stack then takes a clean page fault
instead of silently corrupting kernel data.

## 4. Control registers and the FPU for user programs

**The concept.** Besides the general registers, the CPU has **control registers**
(`CR0`, `CR4`, …) whose bits switch hardware features on and off. Two here are the
**x87 FPU** and **SSE** (the vector unit used for modern floating point). They
start disabled; software enables them before any program uses a `float`.

> 🧠 **First principles: why the kernel skips the FPU.** SeedOS is compiled
> `-mno-sse` (Chapter 7), so the *kernel itself* never uses floating-point
> registers. `fpu_init()` exists purely so *user* programs can — which is why it
> belongs in this user-space Part. It also keeps kernel context switches cheap:
> the kernel has no FPU state to save.

**For example,** enabling SSE means setting `CR4.OSFXSR` (which also turns on the
`fxsave`/`fxrstor` instructions that save/restore the 512-byte FPU state) and
clearing `CR0.EM` (which would otherwise trap FPU instructions).

**In SeedOS.** `fpu_init()` flips those bits and resets the x87 state:

```c
cr0 &= ~CR0_EM;          /* use the real FPU, don't emulate */
cr0 |=  CR0_MP;
cr0 &= ~CR0_TS;          /* no lazy FPU switching for now    */
cr4 |=  CR4_OSFXSR;       /* enable fxsave/fxrstor and SSE    */
cr4 |=  CR4_OSXMMEXCPT;   /* enable SSE FP exceptions         */
__asm__ volatile("fninit");
```

Per-process FPU state (512 bytes) is saved/restored with `fxsave`/`fxrstor`,
seeded by `fpu_init_state()` with sane defaults (control word `0x037F`, `MXCSR`
`0x1F80` — all exceptions masked, round to nearest), and travels with the process
across context switches.

## 5. The CPU primitives

The smallest piece here is `cpu.h`, a few `static inline` wrappers used
everywhere:

```c
static inline void cpu_enable_interrupts(void)  { __asm__ volatile("sti"); }
static inline void cpu_disable_interrupts(void) { __asm__ volatile("cli"); }
static inline void cpu_halt(void)               { __asm__ volatile("hlt"); }
```

`kmain()` enables interrupts only *after* Part I's IDT and Part III's controllers
exist — taking an interrupt before there is anywhere to deliver it would be fatal.

## Reference & cross-links

- **Previous:** [Chapter 20 — Synchronization Primitives](20-synchronization.md).
- **Next:** [Chapter 22 — System Calls](22-syscalls.md) crosses the ring 0/3
  boundary these segments define.
- **The IDT entries that use the IST stacks:**
  [Chapter 10 — The IDT & Exception Handlers](10-idt-exceptions.md).
- **The `rsp0` updated on every switch:**
  [Chapter 19 — The Scheduler & Preemption](19-scheduler-preemption.md).
