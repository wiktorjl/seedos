# BusyBox Porting Plan for SeedOS

A plan for porting a minimal BusyBox toolset to SeedOS.

## Overview

### What is BusyBox?

BusyBox is a single executable that provides many common Unix utilities (ls, cat, echo, sh, cp, mv, etc.). It's designed for embedded systems with limited resources - perfect for a hobby OS.

### Why Port BusyBox?

1. **Real-world validation** - Forces proper POSIX-like syscall implementation
2. **Instant functionality** - Get 100+ utilities from one port
3. **Shell** - BusyBox includes `ash`, a POSIX shell
4. **Learning** - Understand what real programs need from an OS

### Challenges

1. **Syscall gap** - SeedOS has ~9 syscalls; BusyBox needs 50+
2. **libc** - BusyBox needs a C library (we have minimal usys.h)
3. **No fork/exec** - SeedOS can't spawn processes yet
4. **No filesystem writes** - TAR initrd is read-only
5. **No signals** - Required for job control, Ctrl+C, etc.

## Current SeedOS Syscall Status

```
Implemented:
  SYS_EXIT    (0)  - Exit process
  SYS_WRITE   (1)  - Write to fd
  SYS_READ    (2)  - Read from fd
  SYS_GETPID  (3)  - Get process ID
  SYS_UPTIME  (4)  - Get uptime
  SYS_SBRK    (5)  - Adjust heap
  SYS_OPEN    (6)  - Open file
  SYS_CLOSE   (7)  - Close fd
  SYS_LSEEK   (8)  - Seek in file

Missing (required for BusyBox):
  stat, fstat, lstat     - File metadata
  getdents               - Directory listing
  dup, dup2              - Duplicate fd
  pipe                   - Inter-process communication
  fork                   - Create process
  execve                 - Execute program
  waitpid                - Wait for child
  mmap, munmap           - Memory mapping
  ioctl                  - Device control
  fcntl                  - File control
  getcwd, chdir          - Working directory
  signal, sigaction      - Signal handling
  kill                   - Send signal
  time, gettimeofday     - Time functions
  uname                  - System info
```

## Strategy: Phased Approach

Rather than implementing everything at once, we'll take an incremental approach:

```
Phase 1: Foundation (libc + core syscalls)
    │
    ▼
Phase 2: Static Utilities (cat, echo, ls - no fork needed)
    │
    ▼
Phase 3: Process Management (fork, exec, wait)
    │
    ▼
Phase 4: Shell (ash)
    │
    ▼
Phase 5: Full BusyBox
```

---

## Phase 1: Foundation

### Goal
Get a minimal libc working with SeedOS syscalls.

### 1.1 Choose a libc

| Option | Pros | Cons |
|--------|------|------|
| **musl** | Clean, small, well-documented | Still complex (~100K lines) |
| **newlib** | Designed for embedded | Bloated, complex build |
| **PDCLib** | Public domain, minimal | Incomplete |
| **Custom** | Full control, minimal | Lots of work |

**Recommendation:** Start with a **custom minimal libc**, then consider musl later.

### 1.2 Minimal libc Implementation

Create `src/libc/` with:

```
src/libc/
├── include/
│   ├── stdio.h      # printf, fopen, fread, etc.
│   ├── stdlib.h     # malloc, free, exit, atoi
│   ├── string.h     # strlen, strcpy, memcpy, etc.
│   ├── unistd.h     # read, write, close, syscall wrappers
│   ├── fcntl.h      # open, O_RDONLY, etc.
│   ├── sys/stat.h   # stat, struct stat
│   ├── sys/types.h  # pid_t, size_t, etc.
│   ├── dirent.h     # opendir, readdir, struct dirent
│   ├── errno.h      # errno, error codes
│   └── ctype.h      # isalpha, isdigit, etc.
├── stdio.c          # printf implementation
├── stdlib.c         # malloc (using sbrk), atoi, etc.
├── string.c         # String functions
├── syscalls.c       # Thin wrappers around int 0x80
├── errno.c          # errno variable
└── crt0.c           # C runtime startup
```

### 1.3 Required Syscalls for Phase 1

| Syscall | Number | Purpose |
|---------|--------|---------|
| `stat` | 9 | Get file metadata (size, type) |
| `fstat` | 10 | stat on open fd |
| `getdents` | 11 | Read directory entries |
| `dup` | 12 | Duplicate file descriptor |
| `dup2` | 13 | Duplicate fd to specific number |
| `getcwd` | 14 | Get current directory |
| `chdir` | 15 | Change directory |

### 1.4 struct stat Implementation

```c
struct stat {
    uint64_t st_dev;      /* Device */
    uint64_t st_ino;      /* Inode */
    uint32_t st_mode;     /* File type and permissions */
    uint32_t st_nlink;    /* Number of hard links */
    uint32_t st_uid;      /* Owner UID */
    uint32_t st_gid;      /* Owner GID */
    uint64_t st_rdev;     /* Device type (if special file) */
    uint64_t st_size;     /* File size */
    uint64_t st_blksize;  /* Block size */
    uint64_t st_blocks;   /* Number of blocks */
    uint64_t st_atime;    /* Access time */
    uint64_t st_mtime;    /* Modification time */
    uint64_t st_ctime;    /* Status change time */
};

/* st_mode flags */
#define S_IFMT   0170000   /* File type mask */
#define S_IFREG  0100000   /* Regular file */
#define S_IFDIR  0040000   /* Directory */
#define S_ISCHR  0020000   /* Character device */

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
```

---

## Phase 2: Static Utilities

### Goal
Run simple BusyBox applets that don't require fork/exec.

### 2.1 Target Applets

These applets can work without process creation:

| Applet | Description | Syscalls Needed |
|--------|-------------|-----------------|
| `echo` | Print arguments | write |
| `cat` | Concatenate files | open, read, write, close |
| `head` | First N lines | open, read, write, close |
| `tail` | Last N lines | open, read, write, close, lseek |
| `wc` | Word count | open, read, close |
| `ls` | List directory | open, getdents, stat, write, close |
| `pwd` | Print working dir | getcwd, write |
| `true` | Return success | exit |
| `false` | Return failure | exit |
| `yes` | Print "y" forever | write |
| `sleep` | Wait N seconds | nanosleep (or busy-wait) |
| `uname` | System info | uname, write |
| `whoami` | Current user | getuid, write (stub) |
| `id` | User/group info | getuid, getgid, write (stub) |

### 2.2 BusyBox Configuration

Create minimal config:

```bash
# Start with allnoconfig
make allnoconfig

# Enable only static applets
CONFIG_STATIC=y
CONFIG_ECHO=y
CONFIG_CAT=y
CONFIG_LS=y
CONFIG_HEAD=y
CONFIG_TAIL=y
CONFIG_WC=y
CONFIG_PWD=y
CONFIG_TRUE=y
CONFIG_FALSE=y
CONFIG_UNAME=y
```

### 2.3 Build Approach

```bash
# Cross-compile for x86_64 with our libc
make CROSS_COMPILE=x86_64-elf- \
     CFLAGS="-I/path/to/seedos/libc/include -nostdinc" \
     LDFLAGS="-L/path/to/seedos/libc -lc -nostdlib" \
     busybox
```

### 2.4 Integration Options

**Option A: Single multi-call binary**
```
initrd/
└── bin/
    ├── busybox      # Main binary
    ├── cat -> busybox  # Symlink
    ├── ls -> busybox   # Symlink
    └── echo -> busybox # Symlink
```

**Option B: Individual binaries (initially easier)**
```
initrd/
└── bin/
    ├── bb-cat       # Standalone cat
    ├── bb-ls        # Standalone ls
    └── bb-echo      # Standalone echo
```

---

## Phase 3: Process Management

### Goal
Implement fork, exec, and wait to enable shell functionality.

### 3.1 Required Syscalls

| Syscall | Number | Description |
|---------|--------|-------------|
| `fork` | 16 | Create child process |
| `execve` | 17 | Execute program |
| `waitpid` | 18 | Wait for child process |
| `getppid` | 19 | Get parent PID |
| `pipe` | 20 | Create pipe |

### 3.2 Process Table

Expand from single static process to process table:

```c
#define MAX_PROCESSES 16

struct process {
    int pid;
    int ppid;                    /* Parent PID */
    int state;                   /* RUNNING, READY, BLOCKED, ZOMBIE */
    uint64_t pml4;
    uint64_t kernel_stack;
    uint64_t user_stack;
    struct fd_table fds;
    int exit_code;
    struct process *parent;
    /* ... */
};

static struct process process_table[MAX_PROCESSES];
```

### 3.3 fork() Implementation

```c
int sys_fork(void) {
    // 1. Allocate new process slot
    // 2. Copy parent's address space (or COW)
    // 3. Copy fd table (increment vnode refcounts)
    // 4. Set child's return value to 0
    // 5. Set parent's return value to child PID
    // 6. Add child to scheduler
    // 7. Return
}
```

### 3.4 execve() Implementation

```c
int sys_execve(const char *path, char *argv[], char *envp[]) {
    // 1. Find executable in filesystem
    // 2. Clear current address space (keep kernel mappings)
    // 3. Load ELF into fresh address space
    // 4. Set up stack with argv, envp
    // 5. Close O_CLOEXEC file descriptors
    // 6. Jump to entry point
}
```

### 3.5 Scheduler

Need a basic scheduler for multiple processes:

```c
void schedule(void) {
    // Round-robin through READY processes
    // Context switch to next process
}
```

---

## Phase 4: Shell

### Goal
Run BusyBox ash (Almquist shell).

### 4.1 Additional Syscalls

| Syscall | Purpose |
|---------|---------|
| `signal` / `sigaction` | Signal handling (Ctrl+C) |
| `kill` | Send signals |
| `setpgid` / `getpgid` | Process groups |
| `tcsetpgrp` / `tcgetpgrp` | Terminal control |
| `ioctl` | Terminal settings |

### 4.2 Terminal/TTY

BusyBox ash needs basic terminal support:

```c
/* Terminal ioctl commands */
#define TCGETS      0x5401   /* Get terminal attributes */
#define TCSETS      0x5402   /* Set terminal attributes */
#define TIOCGWINSZ  0x5413   /* Get window size */

struct termios {
    uint32_t c_iflag;   /* Input flags */
    uint32_t c_oflag;   /* Output flags */
    uint32_t c_cflag;   /* Control flags */
    uint32_t c_lflag;   /* Local flags */
    uint8_t  c_cc[20];  /* Control characters */
};
```

### 4.3 Signal Implementation

Minimal signal support:

```c
#define SIGINT   2   /* Ctrl+C */
#define SIGTERM 15   /* Termination */
#define SIGCHLD 17   /* Child status changed */

typedef void (*sighandler_t)(int);

struct sigaction {
    sighandler_t sa_handler;
    uint64_t sa_mask;
    int sa_flags;
};
```

---

## Phase 5: Full BusyBox

### Goal
Run full BusyBox with most applets.

### 5.1 Additional Syscalls

```
File operations:    link, unlink, rename, mkdir, rmdir, chmod, chown
Memory:             mmap, munmap, mprotect
Time:               time, gettimeofday, nanosleep
Network (future):   socket, bind, listen, accept, connect
```

### 5.2 Writable Filesystem

For full functionality, need writable storage:

1. **RAM disk** - Simple, but loses data on reboot
2. **FAT driver** - Standard, but complex
3. **Simple custom FS** - Easy to implement, but non-standard

---

## Implementation Order

### Sprint 1: Minimal libc (1-2 weeks equivalent effort)
- [ ] Create libc directory structure
- [ ] Implement string.h functions
- [ ] Implement stdio.h (printf, puts)
- [ ] Implement stdlib.h (malloc using sbrk, atoi)
- [ ] Implement syscall wrappers

### Sprint 2: File Metadata (1 week)
- [ ] Implement sys_stat / sys_fstat
- [ ] Implement sys_getdents
- [ ] Implement struct dirent
- [ ] Implement opendir/readdir/closedir in libc

### Sprint 3: First BusyBox Applet (1 week)
- [ ] Build BusyBox with minimal config
- [ ] Get `echo` working
- [ ] Get `cat` working
- [ ] Get `ls` working

### Sprint 4: Process Management (2 weeks)
- [ ] Implement process table
- [ ] Implement basic scheduler
- [ ] Implement fork()
- [ ] Implement execve()
- [ ] Implement waitpid()

### Sprint 5: Shell (2 weeks)
- [ ] Implement pipe()
- [ ] Implement dup/dup2()
- [ ] Implement basic signal handling
- [ ] Get ash shell running
- [ ] Test command execution

### Sprint 6: Polish (1 week)
- [ ] Fix bugs found during testing
- [ ] Improve error handling
- [ ] Add more applets
- [ ] Documentation

---

## Alternative: Simpler First Steps

If full BusyBox is too ambitious initially, consider these alternatives:

### Alternative A: Port Individual Utilities

Port simpler standalone programs first:
1. **sbase** - Suckless base utilities (simpler than BusyBox)
2. **toybox** - Android's BusyBox alternative
3. **Write custom** - Simple cat, ls, echo from scratch

### Alternative B: Minimal Shell First

Write a custom minimal shell before porting ash:
1. Parse commands
2. Execute single commands (no fork needed with current design)
3. Add pipes later
4. Add job control later

### Alternative C: WASM/Interpreter

Run BusyBox utilities compiled to WebAssembly or in an interpreter:
- Sidesteps many porting issues
- But adds complexity and overhead

---

## File Structure After Porting

```
seedos/
├── src/
│   ├── kernel/           # Existing kernel code
│   └── libc/             # New C library
│       ├── include/
│       │   ├── stdio.h
│       │   ├── stdlib.h
│       │   ├── string.h
│       │   ├── unistd.h
│       │   └── ...
│       ├── stdio.c
│       ├── stdlib.c
│       ├── string.c
│       ├── syscalls.c
│       └── crt0.S
├── busybox/              # BusyBox source (submodule or copy)
│   ├── .config           # Minimal configuration
│   └── ...
├── initrd/
│   └── bin/
│       ├── busybox
│       ├── sh -> busybox
│       ├── ls -> busybox
│       └── ...
└── docs/
    └── plans/
        └── BUSYBOX_PLAN.md
```

---

## Testing Strategy

### Unit Tests
- Test each syscall independently
- Test libc functions

### Integration Tests
- Run each BusyBox applet
- Compare output with Linux version

### Test Cases for Shell
```bash
# Basic execution
echo hello

# Pipes
echo hello | cat

# Redirection (requires writable fs)
echo hello > /tmp/test
cat /tmp/test

# Variables
FOO=bar
echo $FOO

# Control flow
if true; then echo yes; fi
```

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Too many syscalls needed | High | Start with minimal applet set |
| fork() too complex | High | Consider vfork() or spawn() first |
| BusyBox config issues | Medium | Start with individual utilities |
| libc compatibility | Medium | Test thoroughly, stub missing functions |
| TTY/terminal complexity | Medium | Implement minimal subset |

---

## Success Criteria

### Milestone 1: Static Utilities
- [ ] `echo`, `cat`, `ls` work
- [ ] Can read files from initrd
- [ ] Basic printf works

### Milestone 2: Process Creation
- [ ] fork() creates child process
- [ ] execve() runs program
- [ ] waitpid() waits for child

### Milestone 3: Shell
- [ ] ash starts and shows prompt
- [ ] Can run commands
- [ ] Pipes work
- [ ] Ctrl+C interrupts programs

### Milestone 4: Full BusyBox
- [ ] 20+ applets working
- [ ] Shell scripting works
- [ ] Reasonably stable

---

## References

- [BusyBox Source](https://busybox.net/downloads/)
- [BusyBox Configuration](https://busybox.net/FAQ.html#configure)
- [musl libc](https://musl.libc.org/)
- [Linux Syscall Table](https://syscalls.mebeim.net/)
- [POSIX Specification](https://pubs.opengroup.org/onlinepubs/9699919799/)
- [OSDev Wiki - Porting Software](https://wiki.osdev.org/Porting_Software)
