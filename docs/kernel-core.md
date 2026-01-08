# SeedOS Kernel Core Subsystem

Documentation for the kernel core: kprintf, kshell, kthread, and synchronization.

## Overview

| Component | Purpose | Files |
|-----------|---------|-------|
| **kprintf** | Formatted console output | `kernel/kprintf.c/h` |
| **kshell** | Interactive command-line interface | `kernel/kshell.c/h` |
| **kthread** | Kernel threading with scheduling | `kernel/kthread.c/h` |
| **sync** | Synchronization primitives | `kernel/sync.c/h` |

## kprintf - Formatted Printing

### Format Specifiers

| Specifier | Description |
|-----------|-------------|
| `%c` | Character |
| `%s` | String |
| `%d`, `%i` | Signed decimal |
| `%u` | Unsigned decimal |
| `%x`, `%X` | Hexadecimal |
| `%p` | Pointer with 0x prefix |
| `%b` | Binary |

### Log Levels

```c
log_panic("...");  // Red, unrecoverable
log_error("...");  // Red, recoverable
log_warn("...");   // Yellow
log_info("...");   // Green
log_debug("...");  // Gray
```

## kshell - Kernel Shell

Built-in commands: `help`, `clear`, `echo`, `version`, `meminfo`, `matrix`, `spintest`, `mutextest`, `condtest`

### Adding Commands

```c
static void cmd_mycommand(int argc, char *argv[]) {
    kprintf("Hello from my command!\n");
}
// Add to commands[] array
```

## kthread - Kernel Threading

### Thread States

- `THREAD_READY`: Runnable
- `THREAD_RUNNING`: Currently executing
- `THREAD_BLOCKED`: Waiting for event
- `THREAD_EXITED`: Terminated

### API

```c
uint64_t kthread_create(const char *name, void (*entry)(void *), void *arg);
void kthread_exit(void);
void kthread_yield(void);
void kthread_sleep(uint64_t ms);  // 10ms granularity
void preempt_disable(void);
void preempt_enable(void);
```

## Synchronization Primitives

### Spinlock (busy-wait)

```c
spinlock_t lock = SPINLOCK_INIT;
spin_lock(&lock);
// critical section
spin_unlock(&lock);
```

### Mutex (sleep-wait)

```c
mutex_t m = MUTEX_INIT;
mutex_lock(&m);
// critical section
mutex_unlock(&m);
```

### Condition Variable

```c
cond_t c = COND_INIT;
mutex_lock(&m);
while (!condition) cond_wait(&c, &m);
mutex_unlock(&m);
// Signal: cond_signal(&c);
```
