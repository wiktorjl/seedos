# Chapter 11 — A Panic Handler with Backtrace

> Part I — Foundation · Status: ✅ drafted

> **Source files:** `arch/x86/kernel/idt.c` (panic path + `backtrace`), `include/seedos/log.h`

## What this chapter covers

When an exception is a genuine bug — a stray pointer, a stack overflow — the
kernel can't just shrug it off the way a Python program catches an exception:
there is no outer runtime to catch it. The kernel must stop the machine in a
controlled way and tell you *why* and *where*. This chapter builds the **panic**
path and the **backtrace** that prints the chain of function calls that led to the
crash. It's the pay-off to Chapter 10: now those fatal exceptions become readable
diagnostics instead of a frozen black screen.

## 1. What a panic is

**The concept.** A **panic** is the kernel's response to an unrecoverable error:
print as much diagnostic information as possible, then halt the CPU. There is
nothing to "return to" — the kernel *is* the bottom of the software stack — so the
only safe move is to stop.

> 🐍 **From Python — the intuition.** It's an unhandled exception that prints a
> traceback and exits the process — except the "process" is the entire computer,
> and after printing there's nowhere to exit *to*, so the CPU just halts in a
> `hlt` loop. A kernel panic is the bluescreen / `Kernel panic - not syncing`
> you've seen; this chapter is where it comes from.

**For example,** dividing by zero in kernel code raises exception vector 0; the
dispatcher from Chapter 10 sees a fatal exception, prints its name and the saved
register state, walks the stack, and halts.

**In SeedOS.** The fatal branch of `interrupt_handler()` dumps the named exception
and registers via the `log_panic` macro, then enters an infinite `hlt`:

```c
log_panic("EXCEPTION: %s (int %d, error=0x%x)",
          exception_names[int_no], int_no, frame->error_code);
log_panic("RIP: 0x%016llx  RSP: 0x%016llx", frame->rip, frame->rsp);
/* … more registers … */
backtrace(frame->rip, frame->rbp);
while (1) __asm__ volatile("hlt");
```

Because the saved `interrupt_frame_t` (Chapter 10) holds every register at the
moment of the fault, the panic can report the exact machine state that crashed.

## 2. The backtrace

**The concept.** A **backtrace** reconstructs the chain of active function
calls — who called whom — that led to the current point. It does this by walking
**stack frames**: each function call leaves a small record on the stack linking
back to its caller.

> 🧠 **First principles: the frame pointer.** By convention the register `RBP`
> points at the current function's stack frame, and the first thing stored there
> is the *caller's* `RBP`, with the return address right beside it. So the frames
> form a linked list: follow `RBP` to the previous frame, read the return address,
> repeat. That chain is exactly what a backtrace walks.

> 🐍 **From Python — the intuition.** It's the traceback Python prints — "called
> from line X, called from line Y" — but Python builds it from its own frame
> objects. Here we reconstruct it from raw bytes on the stack, by hand.

**For example,** walking the chain looks like: read the return address at
`RBP+8`, print it, set `RBP` to the value at `RBP` (the previous frame), and loop
until `RBP` is zero or invalid.

**In SeedOS.** `backtrace()` does precisely that, with a sanity check that each
frame pointer is a valid kernel address:

```c
for (int i = 1; i < 10 && rbp != 0; i++) {
    if (!is_valid_kernel_addr(rbp)) break;
    uint64_t *frame = (uint64_t *)rbp;
    log_debug("  [%d] 0x%016llx", i, frame[1] - 1);  /* return address */
    rbp = frame[0];                                   /* previous frame */
}
```

This only works because of two deliberate decisions made earlier in the book:

- the kernel is compiled `-fno-omit-frame-pointer` (Chapter 7), so `RBP` actually
  chains the frames instead of being reused as a scratch register; and
- `_start` zeroed `RBP` (Chapter 5), giving the walk a clean terminator at the
  very bottom of the call stack.

## 3. Now exceptions are readable

With Chapters 10 and 11 in place, the foundation is complete: the IDT routes every
fault, the dispatcher decides fatal-vs-handled, and this panic path turns a fatal
fault into a named exception, a register dump, and a call trace. From here on,
when something goes wrong while building the later subsystems, the kernel *tells
you* — which is exactly why this comes so early in the book.

## Reference & cross-links

- **Previous:** [Chapter 10 — The IDT & Exception Handlers](10-idt-exceptions.md).
- **Next:** [Chapter 12 — The Physical Memory Manager](12-pmm.md) begins Part II.
- **The compiler flag that makes backtraces possible:** [Chapter 7 — The Toolchain](07-toolchain.md).
- **The zeroed frame pointer at boot:** [Chapter 5 — Minimal Boot via Limine](05-limine-boot.md).
