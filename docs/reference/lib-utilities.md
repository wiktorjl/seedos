# SeedOS Library/Utilities Subsystem

## Overview

| Component | Files | Purpose |
|-----------|-------|---------|
| Logo Display | `logo.c/h` | Boot-time logo rendering |
| Matrix Animation | `matrix.c/h` | Multithreading demo |
| System Info | `sysinfo.c/h` | Hardware information |

## Logo Display

```c
void logo_display(void);  // Draws logo and positions cursor below
```

Logo data is embedded via `.incbin "logo.bin"` in boot.S. Dimensions defined in logo.h.

## Matrix Animation

Creates "falling characters" effect with one thread per screen column:

```c
void matrix_start(void);      // Start animation (up to 160 threads)
void matrix_stop(void);       // Stop and restore console
int matrix_is_running(void);  // Check status
```

Demonstrates preemptive multithreading - threads animate without explicit yielding.

### Configuration

```c
#define MAX_COLUMNS   160   // Max threads
#define SPEED_MIN     8     // Pixels per frame
#define SPEED_MAX     32
#define DELAY_MIN     30    // ms between frames
#define DELAY_MAX     50
```

## System Information

```c
typedef struct {
    int cpu_count;
    uint64_t total_memory_bytes;
    uint64_t free_memory_bytes;
    uint64_t heap_free_bytes;
} sysinfo_t;

void sysinfo_init(void);
sysinfo_t *sysinfo_get(void);
void sysinfo_print_summary(void);
```

Output example:
```
SeedOS System Summary
  CPU:  4 processor(s)
  RAM:  2 GB total, 1 GB free
  Heap: 512 MB available
```
