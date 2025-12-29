# Known Issues

Issues identified during code review. These are lower priority and documented for future reference.

## High Severity

### ACPI Interrupt Overrides Ignored
**File:** `src/acpi.c:127-131`

The MADT interrupt override entries are parsed and logged but never applied. If the BIOS specifies that ISA IRQ X should map to I/O APIC GSI Y, this is ignored. Keyboard and other devices use hardcoded GSI values.

**Impact:** Keyboard/devices may not work on systems with non-standard interrupt routing.

**Fix:** Store overrides in a table and apply them during IRQ routing in `ioapic_route_irq()`.

---

### Weak Memory Barrier in LAPIC Write
**File:** `src/apic.c:56-60`

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

## Medium Severity

### ACPI MADT Zero-Length Entry Check Order
**File:** `src/acpi.c` (MADT parsing loop)

The check for `header->length == 0` comes after incrementing `entry_ptr`. With malformed ACPI tables this could cause issues.

**Fix:** Check for zero length before incrementing the pointer.

---

### VMM Page Table Leak on Partial Failure
**File:** `src/vmm.c` (vmm_map_page function)

When allocating intermediate page tables (PDPT, PD, PT) and a later allocation fails, cleanup only frees the most recently allocated tables. Earlier allocations may leak.

**Fix:** Track all allocated tables and free them all on failure.

---

### PIT Calibration Infinite Loop
**File:** `src/apic.c:88-94`

No timeout or iteration limit in the PIT polling loop. If PIT hardware is broken, kernel hangs.

**Fix:** Add iteration limit (e.g., 100000 iterations).

---

### Serial Port Infinite Loop
**File:** `src/serial.c:44-46`

`serial_putchar()` blocks forever if UART is non-functional.

**Fix:** Add timeout counter.

---

### Inconsistent I/O APIC Error Handling
**File:** `src/ioapic.c`

`ioapic_route_irq()` logs errors on invalid IRQ, but `ioapic_mask_irq()` fails silently.

**Fix:** Use consistent error handling pattern across all functions.

---

### Missing Volatile on timer_initial_count
**File:** `src/apic.c:36`

`timer_initial_count` is not volatile while adjacent `tick_count` is. Low risk but inconsistent.

**Fix:** Add volatile qualifier for consistency.
