# Known Issues

Issues identified during code review. Organized by severity.

---

## Critical Severity

### Race Condition in mutex_lock() - Deadlock Risk
**File:** `src/sync.c:103-109`

```c
while (!__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
    /* Lock is held - add ourselves to wait queue and sleep */
    waitq_add(&m->wait_head, &m->wait_tail, kthread_current());
    preempt_enable();   /* Must enable before blocking! */
    kthread_block();
    preempt_disable();  /* Re-disable to safely retry CAS */
}
```

**Race Condition:**
1. Thread A: CAS fails (lock held by B)
2. Thread B: Unlocks mutex, removes waiter from queue (empty), calls kthread_unblock
3. Thread A: Adds itself to queue AFTER the wakeup already happened
4. Thread A: Blocks forever (missed wakeup)

**Impact:** Permanent thread starvation.

**Fix:** Add thread to wait queue BEFORE checking lock, or use handoff-style locking where the unlocker transfers ownership directly to a waiter.

---

### Race Conditions in cond_wait()
**File:** `src/sync.c:161-193`

**Problem 1 - Mutex Re-acquisition (lines 182-191):**
```c
preempt_disable();
while (m->locked) {
    waitq_add(&m->wait_head, &m->wait_tail, kthread_current());
    preempt_enable();
    kthread_block();
    preempt_disable();
}
m->locked = 1;
m->owner = kthread_current();
```

This doesn't use CAS! Multiple threads waking from condition variable could all:
1. See `m->locked == 0`
2. All set `m->locked = 1`
3. All think they own the mutex

**Impact:** Breaks mutual exclusion, causes data corruption.

**Fix:** Re-acquiring the mutex should use `mutex_lock()` or the same CAS logic.

---

### Stack Misalignment in kthread_create()
**File:** `src/kthread.c:161-194`

```c
uint64_t stack_top = (uint64_t)new_thread->stack_base + KTHREAD_STACK_SIZE;
stack_top &= ~0xF;  // Align to 16 bytes
// ... later ...
uint64_t *stack_ptr = (uint64_t *)new_thread->rsp;
*(--stack_ptr) = (uint64_t)kthread_trampoline; // 8 bytes
for(int i = 0; i < 4; i++) { *(--stack_ptr) = 0; } // 32 bytes (R15-R12)
for(int i = 0; i < 2; i++) { *(--stack_ptr) = 0; } // 16 bytes (RBP, RBX)
// Total pushed: 8 + 32 + 16 = 56 bytes
```

Stack starts 16-byte aligned, but we push 56 bytes (not a multiple of 16). When `kthread_trampoline` calls the entry function, RSP will be 8-byte aligned, not 16-byte aligned.

**Impact:** Crashes with SSE/AVX instructions (movaps, movdqa) that require 16-byte alignment.

**Fix:** Push 8 more bytes (dummy value or RFLAGS) to make total 64 bytes.

---

## High Severity

### Non-Atomic preempt_count Read in Scheduler
**File:** `src/kthread.c:218`

```c
void kthread_schedule(void) {
    if (preempt_count > 0) {  // Non-atomic read
        return;
    }
    // ...
}
```

`preempt_count` is modified atomically via `__sync_fetch_and_add/sub`, but read non-atomically here.

**Fix:** Use `preempt_enabled()` function or atomic read:
```c
if (__sync_fetch_and_add(&preempt_count, 0) > 0) return;
```

---

### Unconditional Interrupt Enable in Context Switch
**File:** `src/kthread_switch.S:51`

```asm
kthread_switch:
    # ... save/restore registers ...
    sti    # Always enables interrupts!
    ret
```

Context switch unconditionally enables interrupts, violating interrupt state preservation.

**Scenario:**
1. Thread A disables interrupts for critical section
2. Timer interrupt fires, scheduler runs
3. Context switch to Thread B
4. Thread B returns with interrupts enabled (STI)
5. Thread A's critical section assumption violated

**Fix:** Save/restore RFLAGS as part of context switch, or document this as intentional.

---

### Thread List Traversal Without Synchronization
**File:** `src/kthread.c` (multiple locations: 86-94, 224-231, 369-386)

Multiple functions traverse the global thread list without proper synchronization:
- `kthread_get_kthread()` - no preempt disable
- `kthread_schedule()` - has preempt check, but list could change
- `kthread_reap()` - uses preempt_disable, but modifies list

**Race Scenario:**
Thread A traversing list, timer interrupt fires, Thread B runs `kthread_reap()` and frees the thread A was pointing to. Thread A continues with dangling pointer.

**Fix:** Use a spinlock for thread list access, or ensure preempt_disable() is used consistently everywhere.

---

### Integer Overflow in hal_timer_ms_to_ticks()
**File:** `src/hal/hal_timer.h:96`

```c
static inline uint64_t hal_timer_ms_to_ticks(uint64_t ms) {
    uint32_t freq = hal_timer_get_frequency();
    return (ms * freq + 999) / 1000;  // Overflow if ms is large!
}
```

With ms = 0x100000000 (49 days), `ms * freq` overflows.

**Fix:**
```c
uint64_t ticks = (ms / 1000) * freq;
uint64_t remainder = (ms % 1000) * freq;
return ticks + (remainder + 999) / 1000;
```

---

### Missing NULL Check in kthread_unblock()
**File:** `src/kthread.c:349-353`

```c
void kthread_unblock(kthread_t *thread) {
    if (thread->state == THREAD_BLOCKED) {  // Crashes if thread == NULL!
        thread->state = THREAD_READY;
    }
}
```

**Fix:** Add `if (thread == NULL) return;` at start.

---

## Medium Severity

### Weak Memory Barrier in LAPIC Write
**File:** `src/apic.c:61-65`

```c
static inline void lapic_write(uint32_t reg, uint32_t value) {
    lapic_base[reg / 4] = value;
    (void)lapic_base[LAPIC_ID / 4];  // May be optimized away
}
```

The `(void)` cast on a volatile read can be optimized away by aggressive compilers.

**Fix:** Use proper memory barrier:
```c
__asm__ volatile("" ::: "memory");
```

---

### ACPI MADT Zero-Length Entry Check Order
**File:** `src/acpi.c:152-155`

```c
entry_ptr += header->length;
if (header->length == 0) break;  /* Prevent infinite loop */
```

The check for `header->length == 0` comes after incrementing `entry_ptr`. With malformed ACPI tables this ordering is fragile.

**Fix:** Check for zero length before incrementing the pointer.

---

### PIT Calibration Potential Infinite Loop
**File:** `src/apic.c:93-99`

```c
while (1) {
    outb(PIT_COMMAND, 0x00);
    uint8_t lo = inb(PIT_CHANNEL0_DATA);
    uint8_t hi = inb(PIT_CHANNEL0_DATA);
    uint16_t current = lo | (hi << 8);
    if (current == 0 || current > count) break;
}
```

No timeout or iteration limit. If PIT hardware is broken and returns constant values, kernel hangs.

**Fix:** Add iteration limit (e.g., 100000 iterations).

---

### Serial Port Infinite Loop
**File:** `src/serial.c:44-46`

```c
void serial_putchar(char c) {
    while (!serial_tx_ready());   /* Wait for transmit buffer */
    outb(COM1, c);
}
```

Blocks forever if UART is non-functional.

**Fix:** Add timeout counter.

---

### Inconsistent I/O APIC Error Handling
**File:** `src/ioapic.c`

`ioapic_route_irq()` logs errors on invalid IRQ, but `ioapic_mask_irq()` and `ioapic_unmask_irq()` fail silently.

**Fix:** Use consistent error handling pattern across all functions.

---

### Missing Volatile on timer_initial_count
**File:** `src/apic.c:38`

```c
static uint32_t timer_initial_count;
```

Not volatile while adjacent `tick_count` is. Low risk but inconsistent.

**Fix:** Add volatile qualifier for consistency, or document why it's not needed.

---

### Code Duplication in kthread_schedule()
**File:** `src/kthread.c:222-231, 272-280`

Wake-sleepers logic appears twice in the same function.

**Fix:** Call `kthread_wake_sleepers()` instead of duplicating the loop.

---

### Hardcoded LAPIC Virtual Address
**File:** `src/apic.c:204`

```c
uint64_t lapic_virt = 0xFFFFFFFD00000000ULL;
```

Magic number that could conflict with kernel layout changes. No documentation of reserved VA ranges.

**Fix:** Define in memory map header with other kernel virtual address constants.

---

### outw() Defined in platform.c Instead of io.h
**File:** `src/platform/pc/platform.c:24-26`

`io.h` has `outb/inb` but not `outw`. Defining it locally is inconsistent.

**Fix:** Move to `src/io.h` with other I/O port functions.

---

## Low Severity

### No Validation of Thread Name
**File:** `src/kthread.c:148`

```c
new_thread->name = kthread_friendly_name;  // Stores pointer without validation
```

If caller passes NULL or temporary string, later logging may crash.

**Fix:** `new_thread->name = kthread_friendly_name ? kthread_friendly_name : "(unnamed)";`

---

### Dead Commented Code
**File:** `src/kthread.c:74`

```c
// while(1) { asm volatile("hlt"); }
```

**Fix:** Remove it.

---

### Magic Values Reduce Readability
**File:** `src/kthread.c:118`

```c
genesis_kthread.entry = (void*) 0x1337; // Not used
```

**Fix:** Use NULL or a named constant with explanation.

---

### Magic Numbers in Stack Setup
**File:** `src/kthread.c:188-193`

```c
for(int i = 0; i < 4; i++) { *(--stack_ptr) = 0; }  // Why 4?
for(int i = 0; i < 2; i++) { *(--stack_ptr) = 0; }  // Why 2?
```

**Fix:** Add comments explaining these are callee-saved registers (R15-R12, RBP, RBX).

---

## Resolved Issues

### VMM Page Table Leak on Partial Failure
**File:** `src/vmm.c` (vmm_map_page function)
**Status:** Fixed

The code now properly tracks newly allocated tables (new_pdpt_phys, new_pd_phys, new_pt_phys) and frees them on allocation failure.
