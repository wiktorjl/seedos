# BusyBox Bring-Up Plan

Plan to evolve SeedOS to the point where a statically-linked BusyBox
multi-call binary can serve as `/init` and run an interactive shell with
basic utilities. **Scope: multitasking + filesystem only. No networking.**

This plan extends [`USERSPACE_PLAN.md`](USERSPACE_PLAN.md) and
[`FORK_COMPLETION.md`](FORK_COMPLETION.md); it picks up from where those
leave off and tracks the work remaining to reach a working shell.

---

## 1. Where we are today

### Syscall ABI

Linux x86-64 numbers (verified — `userspace/crt/crt0.S` issues
`mov $60, %eax; syscall` for exit). Currently registered handlers in
`arch/x86/kernel/syscall.c`:

```
read (0)        write (1)        open (2)         close (3)
mmap (9)        munmap (11)      brk (12)         arch_prctl (158)
fork (57)       wait4 (61)       exit (60)        getpid (39)
getppid (110)   uname (63)
```

14 calls. **`execve` (59) is reserved in the userspace header but NOT
wired into the kernel table** — this is the critical blocker.

### Process / exec

- `process_t` with per-process PML4, kernel stack, fd table.
- ELF64 loader with proper Linux-compatible stack (argc/argv/envp/auxv).
- `fork()` with CoW page refcounting (`mm/page.c`).
- `userspace/tests/08_fork_open.c` exercises fork + open + read + close.

### Filesystem

- ext2 **read-only** on a Limine-loaded initrd.
- `vfs_file_t` with read/write/close/ref/unref.
- No mount table — there is exactly one root FS.

### TTY

- Console + keyboard work for the in-kernel shell. With the recent
  serial-RX feature, the kernel shell can also be driven over `-serial`.
- **No line discipline**, no `ioctl(TCGETS/TCSETS)`, no canonical mode.
- `read(0, …)` does not properly block a user process on the tty.

### Userspace build

- `userspace/Makefile` builds `-nostdlib` static ELFs against a
  hand-written `crt0.S`.
- No libc, no BusyBox target, no cross toolchain integration.

---

## 2. Target

**BusyBox 1.36+, statically linked against musl libc.** Single multi-call
binary with symlinks. Initrd ships `/bin/busybox` + symlinks +
`/etc/passwd`. `/init` execs `/bin/busybox sh`.

Required working applets:

```
sh, ls, cat, echo, mkdir, cp, mv, rm, pwd, env,
true, false, sleep, test, wc, head, tail, grep, ps
```

Explicitly **out of scope**: networking, SMP, swap, mount of non-initrd
storage, X/framebuffer graphics from userspace.

---

## 3. Milestones

### M1 — execve end-to-end *(1–2 days)*

Wire `SYS_execve = 59` into `syscall_table` and implement `sys_execve(path,
argv, envp)`:

1. Copy `path`, `argv`, `envp` strings out of user memory using the
   `copy_from_user` helpers from the user-mode hardening pass.
2. Resolve `path` through VFS, read the full file, validate ELF.
3. Build the new image into a fresh PML4 (don't touch the current one
   yet — exec must be transactional).
4. On success: swap CR3, free the old PML4, reset `brk`, build the new
   user stack, reset signal handlers, **keep fds** (defer `O_CLOEXEC`).
5. Return to userspace via the same iretq path the fork child uses,
   landing at the new entry point.
6. On failure: tear down the new image, return `-errno` to the caller.

**Test gate:** two-stage userspace test — program A execs program B,
B exits 42, harness checks status via `wait4`.

**Risk:** the old PML4 is torn down while the kernel stack is still
active. Kernel stacks live in HHDM-mapped heap, not the user PML4, so
this should be safe — verify before the swap.

---

### M2 — Read-path syscalls BusyBox needs *(3–4 days)*

Add the following handlers. Most are 10–40 LOC each.

| # | Name | Notes |
|---|------|-------|
| 4 | stat | ext2 inode → `struct stat` translation |
| 5 | fstat | same, via fd |
| 6 | lstat | same; without symlink follow (we have no symlinks yet — alias to stat) |
| 8 | lseek | VFS offset; reject for pipes/tty |
| 16 | ioctl | minimum: `TCGETS`, `TCSETS`, `TIOCGWINSZ` |
| 21 | access | mode check vs inode |
| 32 | dup | fd table copy |
| 33 | dup2 | same, with target fd |
| 79 | getcwd | per-process cwd string |
| 80 | chdir | resolve + update cwd |
| 89 | readlink | no symlinks yet → return -EINVAL |
| 217 | getdents64 | ext2 dir block walk; mind `linux_dirent64` reclen alignment |
| 262 | newfstatat | wraps stat with dirfd |
| 269 | faccessat | wraps access with dirfd |
| 293 | pipe2 | kernel-buffered FIFO |

Stub-and-return-0 (musl init touches all of these):

```
set_tid_address (218)   set_robust_list (273)   prlimit64 (302)
umask (95)              getuid (102)            geteuid (107)
getgid (104)            getegid (108)           rt_sigprocmask (14, partial)
```

**Test gate:** static userspace `ls -l /` against the initrd shows the
expected files with sizes and types.

**Risk:** `getdents64`'s `struct linux_dirent64` layout is fiddly
(variable-length records, 8-byte alignment on `d_reclen`). `ls` will
crash immediately if this is off; get it right early with a unit test.

---

### M3 — tmpfs + multi-mount VFS *(2 days)*

Today the VFS roots at one filesystem. Add:

1. **Mount table**: `{mountpoint_path, fs_ops, fs_private}` linked list.
   Path resolution walks the table longest-match-first.
2. **tmpfs**: in-memory FS. `tmpfs_inode` holds a list of `kmalloc`'d
   blocks; directories hold a list of dirents in memory. Implements the
   same `fs_ops` interface as ext2 (lookup, read, write, getdents,
   stat, mkdir, unlink, etc.). ~300 LOC.
3. Mount tmpfs at `/tmp` (and optionally `/run`, `/home`) at boot.

This unlocks shell redirection (`echo hi > /tmp/x`) without touching
ext2 write paths. **Defer ext2 write entirely.** BusyBox shell does not
need writable ext2 to function.

**Test gate:** `echo hello > /tmp/x && cat /tmp/x` once we have a
shell (M5).

---

### M4 — TTY line discipline + minimal signals *(3–4 days, hardest)*

Two intertwined pieces:

#### 4a. Line discipline

- Add a per-tty line buffer between the input ring (keyboard + serial RX)
  and userspace.
- **Canonical mode** (default): collect bytes, echo them back, handle
  backspace/`^H`/`^?`, deliver to userspace only on `\n` or `^D`.
- **Raw mode** (set by `ioctl(TCSETS)` with `c_lflag & ~ICANON`): each
  byte delivered immediately, no echo.
- `read(0, …)` blocks the calling process on a wait queue. The keyboard
  IRQ (or our serial-RX IRQ) wakes it when a complete line is available
  (canonical) or any byte (raw).
- `ioctl(TIOCGWINSZ)` returns a sensible default (80×25) until we
  detect screen size.

#### 4b. Signals

Implement:

```
rt_sigaction (13)     rt_sigreturn (15)    rt_sigprocmask (14)
kill (62)             tgkill (234, alias to kill for now)
```

- Default actions for `SIGINT, SIGTERM, SIGKILL, SIGCHLD, SIGPIPE`.
- Per-process `sigaction[64]` table; pending mask; blocked mask.
- On return-to-user, if a pending unblocked signal exists, build a
  signal frame on the user stack: saved register state + a small
  trampoline that issues `rt_sigreturn`.
- Generate `SIGINT` from the tty on `^C`.
- Deliver `SIGCHLD` on child exit so `wait4` wakeups behave correctly.

**Defer**: job control (`setpgid`, `tcsetpgrp`, `setsid`, `^Z`/SIGTSTP).
BusyBox `ash` runs without it — no fg/bg, but `^C` still kills the
foreground.

**Test gate:** interactive Python harness — start `/bin/busybox sh`,
type a command, hit Enter, see output; `^C` interrupts `sleep 100`.

**Risk:** the signal frame must be exactly right for `rt_sigreturn` to
unwind cleanly. Build a single-program test (`SIGUSR1` handler that
increments a counter and returns) before turning BusyBox loose.

---

### M5 — musl + BusyBox build + bring-up *(2–3 days)*

1. **Toolchain.** Add `userspace/toolchain/` with either a vendored
   `musl-cross-make` config or a documented fetch step for a prebuilt
   `x86_64-linux-musl-gcc`. Pin the version.
2. **BusyBox.** Add `userspace/busybox/` with:
   - Pinned tarball + checksum.
   - Minimal `.config`: `CONFIG_STATIC=y`, only the applets listed in §2,
     `CONFIG_FEATURE_INSTALL_SUSV=n`.
3. **Makefile target.** `make busybox-initrd` builds BusyBox, runs
   `busybox --install -s` against a staging dir, and bakes the result
   into the ext2 initrd alongside `/etc/passwd` (single root user) and
   a one-line `/init` script that execs `/bin/busybox sh`.
4. **First boot.** Expect 5–10 more syscalls to surface in logs:
   probably `clock_gettime` (228), `nanosleep` (35), `time` (201),
   `gettid` (186), possibly `mprotect` (10). Add as discovered.

**Test gate:** interactive session over serial:

```
seedos> /bin/busybox sh
# ls /
# cat /etc/passwd
# echo hi | wc -c
# sleep 1 && echo done
```

---

### M6 — Stabilize and expand *(ongoing)*

Pick up as needed:

- **ext2 write paths** (block + inode allocation, dirent insert/remove,
  bitmap maintenance). ~800–1200 LOC. Required for non-tmpfs
  persistence.
- **`/proc`** — just enough for `ps`: per-pid dirs with `stat` and
  `cmdline`. Maps cleanly onto the existing process list.
- **Job control** — `setpgid`, `tcsetpgrp`, `setsid`, `^Z`/SIGTSTP.
- **More applets** — `vi` (needs more termios), `awk`, `sed`.
- **clone()** — pthread support, only if we ever build a multithreaded
  userspace program.

---

## 4. Risks / what will probably bite

1. **CoW + exec interaction.** Exec frees the old PML4 while the kernel
   stack is in use. Kernel stacks are HHDM-mapped (not in the user
   PML4) — verify, but don't be casual about ordering.
2. **musl TLS via `arch_prctl(SET_FS)`.** Already implemented; check
   FSBASE survives every fork, exec, and context switch.
3. **Signal frame layout.** A bad sigreturn frame means every handled
   signal crashes. Test in isolation before BusyBox.
4. **TTY blocking-read + existing serial-input.** M4 needs proper wait
   queues; the current serial-input path injects into the keyboard
   ring. Don't regress the kernel shell during the change — keep both
   kshell and userspace consumers on the same line discipline.
5. **`getdents64` reclen alignment.** Spec is fiddly; verify with a
   minimal C reader before turning BusyBox `ls` on it.
6. **musl unexpected syscalls.** Static musl init touches a handful of
   "stub me" calls that aren't obvious from documentation. Plan on a
   half-day of "boot, log unknown syscall N, stub it, repeat."

---

## 5. Effort estimate

Roughly **2–3 focused weeks** of work to reach `busybox sh` running
interactively, assuming no surprises in fork/CoW correctness.

- M1: ~1–2 days
- M2: ~3–4 days
- M3: ~2 days
- M4: ~3–4 days (will dominate)
- M5: ~2–3 days plus stub iteration
- M6: open-ended

---

## 6. Build/test strategy

- Keep the existing `make test TEST=<name>` harness for unit-style
  userspace tests; add tests gated to each milestone (M1: `09_exec_basic.c`,
  M2: `10_ls_l.c`, etc.).
- Reuse the Python serial-driver harness (`/tmp/seedos_test.py`) for
  integration tests once M4 is in. Add a "BusyBox shell smoke test"
  that boots, sends a sequence of commands, and grades output.
- Keep `/init` configurable via the existing `CONFIG_AUTO_INIT` flag so
  the in-kernel `kshell` stays available as a fallback while userspace
  is unstable.
