# SeedOS Implementation Gaps

This document catalogs incomplete implementations, stubs, and features that exist in name only.

## Critical: Process Management is Fake

The recent "spawn" implementation does not create real concurrent processes. It is a synchronous blocking call that:

1. Creates a child process struct
2. Switches to child's address space
3. Runs until child calls exit
4. Destroys child and returns to parent

**Key Issues:**

| Issue | Location | Description |
|-------|----------|-------------|
| Only 2 process slots | `process.c:27` | `static struct process process_slots[2]` - cannot have more than parent+child |
| No concurrent execution | `syscall.c:900` | `sys_spawn` blocks until child exits - not true multitasking |
| Global exit code | `process.c:34` | `static int last_exit_code` - shared between all processes |
| fork() is fake | `unistd.c:113` | Always returns 0, pretending to be child |
| waitpid() is stub | `unistd.c:123` | Returns immediately with fake status |
| execvp uses spawn | `unistd.c:143` | Calls spawn() then _exit() - doesn't replace process image |

**What's Missing:**
- Process scheduler (round-robin, priority, etc.)
- Timer-based preemption (pit_handler just counts ticks)
- Process state machine (READY, RUNNING, BLOCKED, ZOMBIE)
- True fork() that duplicates address space
- Process table with more than 2 slots
- Parent-child relationships
- Wait queues

---

## Signals: Complete Stub

Signals are defined but not implemented. There is no signal delivery mechanism.

| Function | Location | Behavior |
|----------|----------|----------|
| `signal()` | `stubs.c:255` | Returns SIG_DFL, does nothing |
| `kill()` | `stubs.c:261` | Returns -1, errno = ENOSYS |

**Defined but unused signal numbers** (`signal.h:10-31`):
SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM, SIGCHLD, SIGCONT, SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU

**What's Missing:**
- Signal mask per process
- Signal handler registration
- Signal delivery on events (child exit, keyboard interrupt, etc.)
- sigaction(), sigprocmask(), sigsuspend()
- Ctrl+C handling (SIGINT)

---

## Memory Cleanup: Incomplete Error Paths

The process creation code has incomplete cleanup on failure:

| Location | Issue |
|----------|-------|
| `process.c:88` | `/* TODO: Free PML4 */` - PML4 leaked if code page alloc fails |
| `process.c:101` | `/* TODO: Free PML4 */` - PML4 leaked if stack alloc fails |
| `process.c:114` | `/* TODO: Free PML4 */` - PML4 leaked if code mapping fails |
| `process.c:123` | `/* TODO: Clean up properly */` - Partial stack cleanup |
| `process.h:162` | Documents that intermediate page tables not freed |

**Note:** `vmm_free_user_address_space()` exists and does full cleanup, but it's not called on error paths.

---

## Filesystem: Read-Only Stubs

All write operations return EROFS (Read-Only File System):

| Function | Location |
|----------|----------|
| `unlink()` | `stubs.c:77` |
| `rmdir()` | `stubs.c:83` |
| `link()` | `stubs.c:89` |
| `symlink()` | `stubs.c:96` |
| `chmod()` | `stubs.c:103` |
| `fchmod()` | `stubs.c:110` |
| `mkdir()` | `stubs.c:117` |
| `mknod()` | `stubs.c:124` |
| `chown()` | `stubs.c:132` |
| `fchown()` | `stubs.c:140` |
| `rename()` | `stubs.c:148` |
| `remove()` | `stubs.c:155` |
| `creat()` | `stubs.c:234` |
| `utime()` | `stubs.c:352` |

**Also duplicated in unistd.c:199-232**

---

## User/Group: Always Root

All user/group functions return UID/GID 0:

| Function | Location | Returns |
|----------|----------|---------|
| `getuid()` | `stubs.c:186` | 0 |
| `geteuid()` | `stubs.c:187` | 0 |
| `getgid()` | `stubs.c:188` | 0 |
| `getegid()` | `stubs.c:189` | 0 |
| `setuid()` | `stubs.c:191` | 0 (success) |
| `setgid()` | `stubs.c:196` | 0 (success) |

**Note:** These are duplicated in `unistd.c:244-249`

---

## Time Functions: Fake Values

| Function | Location | Behavior |
|----------|----------|----------|
| `time()` | `stubs.c:316` | Returns uptime in seconds (not real wall clock) |
| `localtime()` | `stubs.c:335` | Always returns Jan 1, 2025 12:00:00 |
| `gmtime()` | `stubs.c:340` | Always returns Jan 1, 2025 12:00:00 |
| `ctime()` | `stubs.c:347` | Always returns "Wed Jan 1 12:00:00 2025\n" |

**What's Missing:**
- Real-time clock (RTC) driver
- Timezone support
- strftime()

---

## Password/Group Database: Fake

| Function | Location | Returns |
|----------|----------|---------|
| `getpwuid()` | `stubs.c:291` | Always returns fake "root" passwd struct |
| `getpwnam()` | `stubs.c:296` | Always returns fake "root" passwd struct |
| `getgrgid()` | `stubs.c:301` | Always returns fake "root" group struct |
| `getgrnam()` | `stubs.c:306` | Always returns fake "root" group struct |

---

## Missing Syscalls

These common POSIX functions have no kernel support:

| Function | Description |
|----------|-------------|
| `pipe()` | Create pipe for IPC |
| `ioctl()` | Device control |
| `mmap()` | Memory-mapped I/O |
| `fcntl()` | File descriptor control |
| `select()` | I/O multiplexing |
| `poll()` | I/O multiplexing |
| `nanosleep()` | Sleep with nanosecond precision |

**Note:** `sleep()` in `stubs.c:176` busy-waits using uptime() - wastes CPU.

---

## Mounting: Not Supported

| Function | Location | Returns |
|----------|----------|---------|
| `mount()` | `stubs.c:363` | -1, ENOSYS |
| `umount()` | `stubs.c:374` | -1, ENOSYS |

---

## Command Stubs

These functions exist for compatibility but do nothing:

| Function | Location | Behavior |
|----------|----------|----------|
| `do_ar()` | `stubs.c:391` | Prints "ar: not available" |
| `do_dd()` | `stubs.c:397` | Prints "dd: not available" |
| `do_ed()` | `stubs.c:403` | Prints "ed: not available" |
| `do_tar()` | `stubs.c:409` | Prints "tar: not available" |

---

## Scheduler: Non-Existent

The PIT timer fires at 100 Hz but only increments a counter:

```c
// pit.c:65
void pit_handler(void) {
    ticks++;
    fb_cursor_blink_tick(ticks);  // Just blinks cursor
}
```

**What's Missing:**
- Preemptive multitasking
- Process quantum tracking
- Context switch on timer tick
- Process ready queue
- Blocking I/O with wait queues

---

## Terminal Control: Not Implemented

No termios support:

| Function | Status |
|----------|--------|
| `tcgetattr()` | Not implemented |
| `tcsetattr()` | Not implemented |
| `cfgetospeed()` | Not implemented |
| `cfsetospeed()` | Not implemented |

Stdin is line-buffered in the kernel (`syscall.c:319-395`) but there's no way for userspace to switch to raw mode.

---

## Duplicate Implementations

Some functions are implemented in multiple places:

| Function | Locations |
|----------|-----------|
| `execvp()` | `stubs.c:209` AND `unistd.c:143` |
| `execv()` | `stubs.c:217` AND `unistd.c:185` |
| `execve()` | `stubs.c:225` AND `unistd.c:194` |
| `getuid()` etc. | `stubs.c:186-199` AND `unistd.c:244-249` |
| `unlink()` etc. | `stubs.c:77-159` AND `unistd.c:199-232` |
| `sleep()` | `stubs.c:176` AND `unistd.c:234` |
| `sync()` | `stubs.c:171` AND `unistd.c:240` |

This creates potential conflicts depending on link order.

---

## Summary

| Category | Count | Severity |
|----------|-------|----------|
| Process management fake | 6 issues | Critical |
| Signals not implemented | 2 stubs | High |
| Memory cleanup incomplete | 5 TODOs | Medium |
| Filesystem read-only | 14 stubs | By design |
| User/Group always root | 6 stubs | Low |
| Time functions fake | 4 stubs | Medium |
| Missing syscalls | 7+ | High |
| No scheduler | 1 | Critical |
| No terminal control | 4+ | Medium |
| Duplicate implementations | 15+ | Low (cleanup needed) |

---

## Recommended Priority

1. **Fix process cleanup** - Memory leaks on error paths
2. **Remove duplicates** - Clean up stubs.c vs unistd.c conflicts
3. **Implement basic scheduler** - Even round-robin would help
4. **Add pipe() syscall** - Needed for shell pipelines
5. **Real sleep()** - Use HLT instruction instead of busy-wait
6. **Signal framework** - At minimum SIGINT for Ctrl+C
