# SASH Porting Plan for SeedOS

A plan for porting SASH (Stand-Alone SHell) to SeedOS.

## Overview

### What is SASH?

SASH is a stand-alone shell designed to work without shared libraries. It includes built-in implementations of common Unix utilities (ls, cp, mv, rm, etc.), making it ideal for rescue situations or minimal systems. For SeedOS, this is valuable because:

1. **Self-contained** - No external dependencies beyond libc
2. **Built-in commands** - Many utilities are compiled in, reducing syscall requirements
3. **Simpler than ash** - Easier to port than BusyBox's full shell
4. **Educational** - Clean, readable codebase

Source: https://github.com/multishell/sash

### Why SASH over BusyBox?

| Aspect | SASH | BusyBox ash |
|--------|------|-------------|
| Complexity | ~5,000 lines | ~30,000+ lines |
| Dependencies | Minimal libc | Full libc + shell features |
| Built-in commands | Yes (ls, cp, mv, etc.) | Separate applets |
| Job control | Basic | Full |
| Scripting | Limited | Full POSIX |

SASH is a good stepping stone before attempting full BusyBox.

---

## Current SeedOS Syscall Status

```
Implemented (9 syscalls):
  SYS_EXIT    (0)  - Exit process
  SYS_WRITE   (1)  - Write to fd
  SYS_READ    (2)  - Read from fd
  SYS_GETPID  (3)  - Get process ID
  SYS_UPTIME  (4)  - Get uptime
  SYS_SBRK    (5)  - Adjust heap
  SYS_OPEN    (6)  - Open file
  SYS_CLOSE   (7)  - Close fd
  SYS_LSEEK   (8)  - Seek in file
```

---

## SASH Requirements Analysis

### Core Shell (sash.c)

| Function | Category | Purpose | SeedOS Status |
|----------|----------|---------|---------------|
| `fork()` | Syscall | Create child process | **Missing** |
| `execvp()` | Libc | Execute program | **Missing** |
| `waitpid()` | Syscall | Wait for child | **Missing** |
| `signal()` | Syscall | Signal handling | **Missing** |
| `getenv()` | Libc | Environment variables | **Missing** |
| `setenv()` | Libc | Set environment variable | **Missing** |
| `getcwd()` | Syscall | Get current directory | **Missing** |
| `chdir()` | Syscall | Change directory | **Missing** |
| `isatty()` | Libc | Check if fd is terminal | **Missing** |

### Built-in Commands (cmds.c)

| Function | Category | Used By | SeedOS Status |
|----------|----------|---------|---------------|
| `stat()` | Syscall | ls, test, many | **Missing** |
| `lstat()` | Syscall | ls (symlinks) | **Missing** |
| `fstat()` | Syscall | Various | **Missing** |
| `chmod()` | Syscall | chmod command | **Missing** |
| `chown()` | Syscall | chown command | **Missing** |
| `mkdir()` | Syscall | mkdir command | **Missing** |
| `rmdir()` | Syscall | rmdir command | **Missing** |
| `unlink()` | Syscall | rm command | **Missing** |
| `rename()` | Syscall | mv command | **Missing** |
| `link()` | Syscall | ln command | **Missing** |
| `symlink()` | Syscall | ln -s command | **Missing** |
| `readlink()` | Syscall | ls -l (symlinks) | **Missing** |
| `mount()` | Syscall | mount command | **Missing** |
| `umount()` | Syscall | umount command | **Missing** |
| `sync()` | Syscall | sync command | **Missing** |
| `kill()` | Syscall | kill command | **Missing** |
| `ioctl()` | Syscall | Terminal control | **Missing** |
| `utime()` | Syscall | touch command | **Missing** |
| `dup()` | Syscall | I/O redirection | **Missing** |
| `dup2()` | Syscall | I/O redirection | **Missing** |
| `pipe()` | Syscall | Pipes | **Missing** |

### Directory Listing (cmd_ls.c, utils.c)

| Function | Category | Purpose | SeedOS Status |
|----------|----------|---------|---------------|
| `opendir()` | Libc | Open directory | **Missing** |
| `readdir()` | Libc | Read directory entry | **Missing** |
| `closedir()` | Libc | Close directory | **Missing** |
| `getpwuid()` | Libc | User name lookup | **Missing** |
| `getgrgid()` | Libc | Group name lookup | **Missing** |

### String/Memory (libc)

| Function | Category | Purpose | SeedOS Status |
|----------|----------|---------|---------------|
| `malloc()` | Libc | Dynamic allocation | Partial (sbrk) |
| `free()` | Libc | Free memory | **Missing** |
| `realloc()` | Libc | Resize allocation | **Missing** |
| `strdup()` | Libc | Duplicate string | **Missing** |
| `qsort()` | Libc | Sort array | **Missing** |
| `printf()` | Libc | Formatted output | **Missing** |
| `sprintf()` | Libc | Format to string | **Missing** |
| `sscanf()` | Libc | Parse string | **Missing** |
| `strtol()` | Libc | String to long | **Missing** |
| `atoi()` | Libc | String to int | **Missing** |

### File I/O (libc wrappers)

| Function | Category | Purpose | SeedOS Status |
|----------|----------|---------|---------------|
| `fopen()` | Libc | Open FILE stream | **Missing** |
| `fclose()` | Libc | Close FILE stream | **Missing** |
| `fread()` | Libc | Buffered read | **Missing** |
| `fwrite()` | Libc | Buffered write | **Missing** |
| `fgets()` | Libc | Read line | **Missing** |
| `fputs()` | Libc | Write string | **Missing** |
| `fprintf()` | Libc | Formatted FILE output | **Missing** |
| `fflush()` | Libc | Flush buffer | **Missing** |

---

## Implementation Strategy

### Approach: Phased with Stubbing

Since SASH is a monolithic binary, we can't run parts of it incrementally. Instead:

1. **Create minimal libc** with all required functions (stub non-critical ones)
2. **Implement critical syscalls** in priority order
3. **Build and test** with stubbed functionality
4. **Replace stubs** with real implementations incrementally

### Phase 1: Minimal Libc (Foundation)

Create `src/libc/` with:

```
src/libc/
├── include/
│   ├── stdio.h      # FILE, printf, sprintf, fopen, etc.
│   ├── stdlib.h     # malloc, free, atoi, qsort, exit
│   ├── string.h     # strlen, strcpy, strcmp, strdup, etc.
│   ├── unistd.h     # read, write, fork, exec, getcwd, etc.
│   ├── fcntl.h      # open, O_RDONLY, etc.
│   ├── dirent.h     # DIR, dirent, opendir, readdir
│   ├── sys/stat.h   # stat, struct stat, S_ISDIR, etc.
│   ├── sys/types.h  # pid_t, uid_t, gid_t, etc.
│   ├── sys/wait.h   # waitpid, WEXITSTATUS, etc.
│   ├── signal.h     # signal, kill, SIG* constants
│   ├── errno.h      # errno, E* constants
│   ├── ctype.h      # isalpha, isdigit, isspace, etc.
│   ├── time.h       # time_t, struct tm (stubs ok)
│   ├── pwd.h        # getpwuid (can stub)
│   └── grp.h        # getgrgid (can stub)
├── stdio.c          # printf, sprintf, FILE operations
├── stdlib.c         # malloc (bump allocator), atoi, qsort
├── string.c         # String functions
├── syscalls.c       # Thin wrappers around INT 0x80
├── dirent.c         # opendir/readdir using getdents
├── errno.c          # errno variable
├── stubs.c          # Stub functions for non-critical features
└── crt0.S           # C runtime startup
```

#### Priority 1: Core Functions

```c
/* Must work correctly */
void *malloc(size_t size);
void free(void *ptr);           /* Can be no-op initially */
char *strdup(const char *s);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int atoi(const char *s);
long strtol(const char *s, char **endptr, int base);

/* String functions */
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
```

#### Priority 2: File I/O

```c
/* Required for basic operation */
int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
```

#### Priority 3: Can Be Stubbed Initially

```c
/* Return error or dummy values */
int chmod(const char *path, mode_t mode);   /* return -1, EROFS */
int chown(const char *path, uid_t, gid_t);  /* return -1, EROFS */
int mkdir(const char *path, mode_t mode);   /* return -1, EROFS */
int unlink(const char *path);               /* return -1, EROFS */
int rename(const char *, const char *);     /* return -1, EROFS */
uid_t getuid(void);                         /* return 0 (root) */
gid_t getgid(void);                         /* return 0 */
struct passwd *getpwuid(uid_t);             /* return NULL */
struct group *getgrgid(gid_t);              /* return NULL */
```

---

### Phase 2: Critical Syscalls

#### 2.1 Directory Operations

```c
/* Syscall 9: getdents */
struct dirent {
    uint64_t d_ino;
    uint64_t d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
};

int sys_getdents(int fd, struct dirent *dirp, size_t count);
```

Implementation in kernel:
- VFS already has vnode abstraction
- Add `vn_readdir()` operation to vnode_ops
- tarfs implementation iterates TAR entries with matching directory prefix

#### 2.2 File Metadata

```c
/* Syscall 10: stat */
/* Syscall 11: fstat */
/* Syscall 12: lstat (can alias to stat for now) */

struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
};

int sys_stat(const char *path, struct stat *buf);
int sys_fstat(int fd, struct stat *buf);
```

#### 2.3 Working Directory

```c
/* Syscall 13: getcwd */
/* Syscall 14: chdir */

char *sys_getcwd(char *buf, size_t size);
int sys_chdir(const char *path);
```

Implementation:
- Store `cwd` string in process structure
- `chdir()` validates path exists (via tarfs), updates cwd
- `getcwd()` copies cwd to user buffer

#### 2.4 File Descriptor Duplication

```c
/* Syscall 15: dup */
/* Syscall 16: dup2 */

int sys_dup(int oldfd);
int sys_dup2(int oldfd, int newfd);
```

Implementation:
- `dup()` finds lowest available fd, copies file_descriptor entry
- `dup2()` closes newfd if open, copies oldfd to newfd slot
- Increment vnode refcount

---

### Phase 3: Process Management

This is the biggest change - moving from single-process to multi-process.

#### 3.1 Process Table

```c
#define MAX_PROCESSES 16

enum process_state {
    PROC_UNUSED,
    PROC_RUNNING,
    PROC_READY,
    PROC_BLOCKED,
    PROC_ZOMBIE
};

struct process {
    int pid;
    int ppid;
    enum process_state state;
    uint64_t pml4;
    uint64_t kernel_stack;
    uint64_t user_rsp;
    uint64_t user_rip;
    struct fd_table fds;
    char cwd[256];
    int exit_code;
    /* ... saved registers ... */
};

static struct process procs[MAX_PROCESSES];
static struct process *current;
```

#### 3.2 fork() Implementation

```c
/* Syscall 17: fork */
int sys_fork(void);
```

Steps:
1. Find empty slot in process table
2. Allocate new PML4, copy parent's address space (or use COW)
3. Copy fd_table (increment vnode refcounts)
4. Copy saved registers, set child's RAX to 0
5. Set child state to READY
6. Return child PID to parent

#### 3.3 execve() Implementation

```c
/* Syscall 18: execve */
int sys_execve(const char *path, char *const argv[], char *const envp[]);
```

Steps:
1. Find executable in tarfs
2. Clear user address space (keep kernel mappings)
3. Load ELF into fresh pages
4. Set up user stack with argv, envp
5. Close O_CLOEXEC file descriptors
6. Set RIP to entry point, RSP to stack
7. Never returns (on success)

#### 3.4 waitpid() Implementation

```c
/* Syscall 19: waitpid */
pid_t sys_waitpid(pid_t pid, int *status, int options);
```

Steps:
1. Find matching zombie child (or any child if pid == -1)
2. If found: extract exit_code, free child resources, return
3. If not found and WNOHANG: return 0
4. Otherwise: block until child exits

#### 3.5 Basic Scheduler

```c
void schedule(void) {
    struct process *next = NULL;

    /* Round-robin: find next READY process */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (current->pid + 1 + i) % MAX_PROCESSES;
        if (procs[idx].state == PROC_READY) {
            next = &procs[idx];
            break;
        }
    }

    if (next && next != current) {
        context_switch(current, next);
    }
}
```

---

### Phase 4: Signals (Minimal)

SASH uses signals for Ctrl+C handling. Minimal implementation:

```c
/* Syscall 20: signal */
typedef void (*sighandler_t)(int);
sighandler_t sys_signal(int signum, sighandler_t handler);

/* Syscall 21: kill */
int sys_kill(pid_t pid, int sig);
```

Minimal signals to support:
- `SIGINT` (2) - Interrupt (Ctrl+C)
- `SIGTERM` (15) - Termination request
- `SIGCHLD` (17) - Child status changed

Implementation:
- Store signal handlers per-process
- On Ctrl+C, send SIGINT to foreground process
- Check pending signals before returning to userspace

---

### Phase 5: Build and Test

#### 5.1 SASH Configuration

SASH has compile-time options to disable features. Disable what we can't support:

```c
/* In sash.h or config.h */
#define HAVE_LINUX_MOUNT 0      /* No mount syscall */
#define HAVE_LINUX_PIVOT 0      /* No pivot_root */
#define HAVE_GZIP 0             /* No gzip support */
#undef HAVE_EXT2_MOUNT          /* No ext2 */
```

#### 5.2 Build Process

```bash
# Cross-compile sash with our libc
x86_64-elf-gcc -nostdinc -nostdlib \
    -I/path/to/seedos/libc/include \
    -L/path/to/seedos/libc \
    -o sash sash.c cmds.c cmd_*.c utils.c \
    -lc
```

#### 5.3 Integration

```
initrd/
├── bin/
│   ├── sash          # The shell
│   └── sh -> sash    # Symlink for scripts
└── etc/
    └── profile       # Shell startup script
```

---

## Syscall Summary

### New Syscalls Needed (Priority Order)

| # | Name | Priority | Difficulty | Notes |
|---|------|----------|------------|-------|
| 9 | getdents | High | Medium | Required for ls, directory listing |
| 10 | stat | High | Easy | File metadata |
| 11 | fstat | High | Easy | Same as stat on fd |
| 12 | lstat | Medium | Easy | Alias to stat initially |
| 13 | getcwd | High | Easy | Process cwd string |
| 14 | chdir | High | Easy | Update process cwd |
| 15 | dup | High | Easy | fd table manipulation |
| 16 | dup2 | High | Easy | fd table manipulation |
| 17 | fork | Critical | Hard | Process creation |
| 18 | execve | Critical | Hard | Program execution |
| 19 | waitpid | Critical | Medium | Child process wait |
| 20 | signal | Medium | Medium | Signal handlers |
| 21 | kill | Medium | Easy | Send signal |
| 22 | pipe | Low | Medium | For pipelines |
| 23 | ioctl | Low | Medium | Terminal control |

### Stubs (Return Error)

These can return `-1` with `errno = ENOSYS` or `EROFS`:

- chmod, chown, chgrp
- mkdir, rmdir, unlink, rename, link, symlink
- mount, umount
- utime

---

## Implementation Timeline

### Sprint 1: Libc Core
- [ ] Create libc directory structure
- [ ] Implement string.h functions
- [ ] Implement ctype.h functions
- [ ] Implement printf/sprintf (variadic)
- [ ] Implement malloc (bump allocator using sbrk)
- [ ] Implement syscall wrappers

### Sprint 2: Directory Support
- [ ] Implement sys_getdents
- [ ] Implement sys_stat / sys_fstat
- [ ] Implement opendir/readdir/closedir in libc
- [ ] Implement sys_getcwd / sys_chdir
- [ ] Test with standalone ls program

### Sprint 3: FD Operations
- [ ] Implement sys_dup / sys_dup2
- [ ] Implement basic stdio (FILE, fopen, fread, fgets)
- [ ] Test with cat program

### Sprint 4: Process Management
- [ ] Refactor to process table
- [ ] Implement basic scheduler
- [ ] Implement sys_fork
- [ ] Implement sys_execve
- [ ] Implement sys_waitpid
- [ ] Test with simple fork-exec test program

### Sprint 5: SASH Integration
- [ ] Compile SASH with stubs
- [ ] Fix compilation errors
- [ ] Test basic command execution
- [ ] Implement signal handling
- [ ] Test Ctrl+C interrupt

### Sprint 6: Polish
- [ ] Fix bugs found during testing
- [ ] Improve error messages
- [ ] Add more built-in commands
- [ ] Documentation

---

## Testing Strategy

### Unit Tests

Test each component independently:

```c
/* test_libc.c */
void test_printf(void) {
    char buf[100];
    sprintf(buf, "Hello %s, you are %d", "World", 42);
    assert(strcmp(buf, "Hello World, you are 42") == 0);
}

void test_malloc(void) {
    void *p1 = malloc(100);
    void *p2 = malloc(200);
    assert(p1 != NULL);
    assert(p2 != NULL);
    assert(p2 > p1);  /* Bump allocator */
}
```

### Integration Tests

```bash
# After sash is running:

# Test echo
echo hello world

# Test cd/pwd
cd /bin
pwd

# Test ls
ls
ls -l

# Test cat
cat /etc/motd

# Test command execution
/bin/hello
```

---

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| fork() complexity | High | Implement vfork() first (no address space copy) |
| COW paging complex | High | Full copy initially, optimize later |
| printf() complexity | Medium | Start with basic %s, %d, %x only |
| Signal delivery complex | Medium | Check signals only at syscall return |
| Too many stubs | Medium | Prioritize based on actual SASH usage |

---

## Alternative: Simplified First

If full SASH is too ambitious, consider these alternatives:

### Alternative A: SASH Lite

Compile SASH with only:
- Built-in commands (no fork/exec needed)
- echo, pwd, cd, ls, cat, help

This requires only:
- getdents, stat, getcwd, chdir
- No process management

### Alternative B: Custom Shell First

Write a minimal custom shell:
1. Parse input line
2. Match against built-in commands
3. Execute directly (no fork)

Then add fork/exec later.

### Alternative C: Single Command Mode

Run each command as a separate process from the kernel shell:
1. Type command in kernel shell
2. Kernel forks, execs SASH with `-c "command"`
3. SASH runs command with built-ins
4. Returns to kernel shell

---

## Built-in Only Mode (Recommended First Step)

SASH's key advantage is that built-in commands execute **directly in the shell process** without fork/exec. This means we can run a useful shell with far fewer syscalls.

### How SASH Command Execution Works

```
User types: "ls -l /bin"
            │
            ▼
    ┌───────────────────┐
    │ Parse command     │
    │ argc=3, argv[0]="ls"
    └─────────┬─────────┘
              │
              ▼
    ┌───────────────────┐
    │ Lookup in         │
    │ commandEntryTable │◄─── 40+ built-in commands
    └─────────┬─────────┘
              │
         Found?
        /      \
       Yes      No
       │         │
       ▼         ▼
  ┌─────────┐  ┌──────────────┐
  │ Call    │  │ fork() +     │
  │ do_ls() │  │ execvp()     │◄─── External command
  │ directly│  │ + waitpid()  │
  └─────────┘  └──────────────┘
       │
       ▼
  Returns to shell prompt (no fork needed!)
```

**Key insight:** If we only use built-in commands, we never hit the fork/exec path.

### Disabling External Command Execution

To force built-in-only mode, modify `sash.c`:

```c
/* In runCmd() function, replace external command handling: */

/* Original code: */
if (!command->func) {
    /* Run external command via fork/exec */
    pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        /* ... */
    }
    waitpid(pid, &status, 0);
}

/* Replace with: */
if (!command->func) {
    fprintf(stderr, "%s: external commands disabled\n", argv[0]);
    return;
}
```

Alternatively, create a `config.h`:

```c
/* config.h - SeedOS SASH configuration */
#ifndef SASH_CONFIG_H
#define SASH_CONFIG_H

/* Disable features requiring missing syscalls */
#define SASH_NO_EXTERNAL_COMMANDS  1  /* No fork/exec */
#define SASH_NO_PIPES              1  /* No pipe() */
#define SASH_NO_REDIRECTS          1  /* No dup2() for now */
#define SASH_NO_JOB_CONTROL        1  /* No signals */
#define SASH_NO_WILDCARDS          1  /* Simpler initially */

/* Disable optional features */
#undef HAVE_GZIP                      /* No zlib */
#undef HAVE_EXT2                      /* No ext2 attrs */
#undef HAVE_LINUX_MOUNT               /* No mount() */
#undef HAVE_LINUX_CHROOT              /* No chroot() */
#undef HAVE_LINUX_LOSETUP             /* No loopback */
#undef HAVE_LINUX_PIVOT               /* No pivot_root() */

#endif
```

### Built-in Commands: Full Analysis

#### Tier 1: Work with Current SeedOS Syscalls

These commands need only: `write`, `read`, `open`, `close`, `lseek`, `exit`

| Command | What it Does | Syscalls Used | SeedOS Ready? |
|---------|--------------|---------------|---------------|
| `echo` | Print arguments | write | **YES** |
| `exit` | Exit shell | exit | **YES** |
| `help` | List commands | write | **YES** |
| `true` | Return success | exit(0) | **YES** |
| `false` | Return failure | exit(1) | **YES** |
| `source` | Read commands from file | open, read, close | **YES** |

#### Tier 2: Need stat/fstat (Easy to Add)

| Command | What it Does | Additional Syscalls | Gap |
|---------|--------------|---------------------|-----|
| `cat` | Display file | stat (for error check) | stat |
| `more` | Page through file | stat, ioctl (termsize) | stat, ioctl |
| `cmp` | Compare files | stat | stat |
| `sum` | Checksum file | stat | stat |
| `file` | Identify file type | stat, open, read | stat |
| `head` | First N lines | stat | stat |
| `dd` | Copy with conversion | stat, fstat | stat, fstat |

#### Tier 3: Need Directory Syscalls

| Command | What it Does | Additional Syscalls | Gap |
|---------|--------------|---------------------|-----|
| `ls` | List directory | opendir, readdir, stat, lstat | getdents, stat |
| `pwd` | Print working dir | getcwd | getcwd |
| `cd` | Change directory | chdir, stat | chdir, stat |
| `find` | Search for files | opendir, readdir, stat | getdents, stat |
| `where` | Find command in PATH | stat, access | stat |

#### Tier 4: Need Write Syscalls (Read-Only FS = Stub)

| Command | What it Does | Additional Syscalls | Stub? |
|---------|--------------|---------------------|-------|
| `cp` | Copy file | open, read, write, creat, chmod | Yes - EROFS |
| `mv` | Move/rename | rename, unlink | Yes - EROFS |
| `rm` | Remove file | unlink | Yes - EROFS |
| `mkdir` | Create directory | mkdir | Yes - EROFS |
| `rmdir` | Remove directory | rmdir | Yes - EROFS |
| `ln` | Create link | link, symlink | Yes - EROFS |
| `touch` | Update timestamp | utime, creat | Yes - EROFS |
| `chmod` | Change permissions | chmod | Yes - EROFS |
| `chown` | Change ownership | chown | Yes - EROFS |
| `chgrp` | Change group | chown | Yes - EROFS |
| `mknod` | Create device | mknod | Yes - EROFS |

#### Tier 5: Need Process/Signal Syscalls

| Command | What it Does | Additional Syscalls | Gap |
|---------|--------------|---------------------|-----|
| `exec` | Replace shell | execve | execve |
| `kill` | Send signal | kill | kill, signals |
| `umask` | Set file mask | umask | umask |
| `setenv` | Set env var | (libc only) | getenv/setenv |
| `printenv` | Print env vars | (libc only) | getenv |

#### Tier 6: System-Level (Can Stub)

| Command | What it Does | Stub Response |
|---------|--------------|---------------|
| `mount` | Mount filesystem | "not supported" |
| `umount` | Unmount filesystem | "not supported" |
| `sync` | Sync filesystems | no-op (return success) |
| `losetup` | Loop device setup | "not supported" |
| `pivot_root` | Change root fs | "not supported" |

---

### Gap Analysis: SeedOS vs Built-in Only SASH

#### Current SeedOS Syscalls (9)

```
✓ SYS_EXIT   (0)  - Works
✓ SYS_WRITE  (1)  - Works
✓ SYS_READ   (2)  - Works
✓ SYS_GETPID (3)  - Works (not used by SASH directly)
✓ SYS_UPTIME (4)  - Works (not used by SASH)
✓ SYS_SBRK   (5)  - Works (for malloc)
✓ SYS_OPEN   (6)  - Works
✓ SYS_CLOSE  (7)  - Works
✓ SYS_LSEEK  (8)  - Works
```

#### Required for Minimal Built-in Shell (6 new syscalls)

| Syscall | Number | Why Needed | Difficulty |
|---------|--------|------------|------------|
| `stat` | 9 | ls, cd validation, many commands | Easy |
| `fstat` | 10 | dd, file operations | Easy |
| `getdents` | 11 | ls, find, directory listing | Medium |
| `getcwd` | 12 | pwd command | Easy |
| `chdir` | 13 | cd command | Easy |
| `isatty` | 14 | Line buffering detection | Easy (stub=1) |

**Total: 15 syscalls to run basic SASH with ls, cd, pwd, cat, echo**

#### Required Libc Functions

**Must Implement:**

| Function | Used By | Implementation Notes |
|----------|---------|---------------------|
| `malloc` | Everywhere | Bump allocator using sbrk |
| `free` | Everywhere | No-op initially (leak memory) |
| `realloc` | ls, utils | malloc + memcpy + free |
| `strdup` | Command parsing | malloc + strcpy |
| `printf` | All output | Variadic, format %s %d %ld %x %c %% |
| `fprintf` | Error messages | printf to FILE* |
| `sprintf` | String building | printf to buffer |
| `fputs` | Output | write() wrapper |
| `fputc` | Character output | write() wrapper |
| `perror` | Error messages | fprintf + strerror |
| `atoi` | Argument parsing | Simple implementation |
| `strtol` | Number parsing | With base support |
| `qsort` | ls sorting | Standard quicksort |
| `getenv` | cd (HOME), PATH | Simple env array lookup |
| `isatty` | Output buffering | Stub: return 1 |

**String Functions (Already have most in kernel string.c):**

```c
strlen, strcpy, strncpy, strcat, strncat
strcmp, strncmp, strchr, strrchr, strstr
memcpy, memset, memcmp, memmove
```

**File I/O (stdio wrappers):**

```c
/* Minimal FILE implementation */
typedef struct {
    int fd;
    int flags;
    char buf[BUFSIZ];
    int bufpos;
    int buflen;
} FILE;

extern FILE *stdin, *stdout, *stderr;

/* Needed functions */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
size_t fread(void *ptr, size_t size, size_t n, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t n, FILE *fp);
char *fgets(char *s, int n, FILE *fp);
int fgetc(FILE *fp);
int fflush(FILE *fp);
int fileno(FILE *fp);
```

**Directory Functions:**

```c
/* Thin wrappers around getdents syscall */
typedef struct {
    int fd;
    struct dirent current;
    /* ... buffer for getdents ... */
} DIR;

DIR *opendir(const char *name);        /* open + allocate DIR */
struct dirent *readdir(DIR *dirp);     /* getdents wrapper */
int closedir(DIR *dirp);               /* close + free */
```

**Can Stub (return error/dummy):**

| Function | Stub Return | Reason |
|----------|-------------|--------|
| `getpwuid` | NULL | No user database |
| `getgrgid` | NULL | No group database |
| `chmod` | -1, EROFS | Read-only fs |
| `chown` | -1, EROFS | Read-only fs |
| `utime` | -1, EROFS | Read-only fs |
| `link` | -1, EROFS | Read-only fs |
| `unlink` | -1, EROFS | Read-only fs |
| `mkdir` | -1, EROFS | Read-only fs |

---

### Compilation Steps for Built-in Only SASH

#### Step 1: Create SeedOS libc

```
seedos/
└── src/
    └── libc/
        ├── Makefile
        ├── crt0.S           # _start -> main
        ├── include/
        │   ├── stdio.h
        │   ├── stdlib.h
        │   ├── string.h
        │   ├── unistd.h
        │   ├── fcntl.h
        │   ├── dirent.h
        │   ├── sys/stat.h
        │   ├── sys/types.h
        │   ├── errno.h
        │   ├── ctype.h
        │   └── time.h
        ├── stdio.c          # printf, FILE ops
        ├── stdlib.c         # malloc, atoi, qsort
        ├── string.c         # string functions
        ├── dirent.c         # opendir/readdir
        ├── syscalls.c       # INT 0x80 wrappers
        ├── stubs.c          # stub functions
        └── errno.c          # errno variable
```

#### Step 2: Build libc

```makefile
# src/libc/Makefile
CC = x86_64-elf-gcc
AR = x86_64-elf-ar
CFLAGS = -ffreestanding -nostdinc -nostdlib -I./include \
         -mno-red-zone -mcmodel=large -fno-pie

OBJS = crt0.o stdio.o stdlib.o string.o dirent.o \
       syscalls.o stubs.o errno.o

libc.a: $(OBJS)
	$(AR) rcs $@ $^
```

#### Step 3: Configure SASH

Create `seedos_config.h`:

```c
/* SASH configuration for SeedOS */

/* Disable external commands (no fork/exec) */
#define SASH_BUILTIN_ONLY 1

/* Disable features needing unavailable syscalls */
#define SASH_NO_PIPES 1
#define SASH_NO_REDIRECTS 1
#define SASH_NO_WILDCARDS 1  /* Needs opendir for glob */

/* Disable optional features */
#undef HAVE_GZIP
#undef HAVE_EXT2
#undef HAVE_LINUX_MOUNT
#undef HAVE_LINUX_CHROOT
#undef HAVE_LINUX_LOSETUP
#undef HAVE_LINUX_PIVOT
```

#### Step 4: Patch SASH

```diff
--- a/sash.c
+++ b/sash.c
@@ -1,5 +1,9 @@
 #include "sash.h"

+#ifdef SASH_BUILTIN_ONLY
+#include "seedos_config.h"
+#endif
+
 /* In runCmd(), disable external command path: */

 static void runCmd(int argc, char **argv) {
     const CommandEntry *entry;

     entry = findCommand(argv[0]);

     if (entry && entry->func) {
         /* Built-in command - execute directly */
         entry->func(argc, argv);
         return;
     }

+#ifdef SASH_BUILTIN_ONLY
+    fprintf(stderr, "%s: command not found (external commands disabled)\n",
+            argv[0]);
+    return;
+#else
     /* External command - fork/exec */
     pid = fork();
     /* ... original code ... */
+#endif
 }
```

#### Step 5: Build SASH

```bash
# Cross-compile SASH for SeedOS
cd sash/

x86_64-elf-gcc \
    -ffreestanding -nostdinc -nostdlib \
    -I../seedos/src/libc/include \
    -include seedos_config.h \
    -mno-red-zone -mcmodel=large -fno-pie \
    -c sash.c cmds.c cmd_ls.c utils.c -o sash.o

x86_64-elf-gcc \
    -nostdlib -static \
    -T ../seedos/src/userspace/user.ld \
    sash.o ../seedos/src/libc/libc.a \
    -o sash

# Add to initrd
cp sash ../seedos/build/initrd/bin/
```

#### Step 6: Test in SeedOS

```
SeedOS> run sash
sash% help
Built-in commands:
    alias cat cd cmp cp dd echo exec exit false find
    grep head help kill ln ls mkdir more mv mknod
    printenv pwd rm rmdir setenv source sum sync
    touch true umask unalias

sash% echo Hello from SASH!
Hello from SASH!

sash% pwd
/

sash% cd /bin
sash% pwd
/bin

sash% ls
alpha    count    crash    filetest hello
info     input    loop     sash     stars

sash% cat /etc/motd
Welcome to SeedOS!
A minimal educational operating system.

sash% exit
SeedOS>
```

---

### Implementation Checklist for Built-in Only Mode

#### Kernel Changes (6 new syscalls)

- [ ] **sys_stat** (9) - Get file metadata from path
  ```c
  int sys_stat(const char *path, struct stat *buf);
  // Use tarfs to find file, populate struct stat
  ```

- [ ] **sys_fstat** (10) - Get file metadata from fd
  ```c
  int sys_fstat(int fd, struct stat *buf);
  // Get vnode from fd, extract metadata
  ```

- [ ] **sys_getdents** (11) - Read directory entries
  ```c
  int sys_getdents(int fd, struct dirent *buf, size_t count);
  // Iterate tarfs entries matching directory prefix
  ```

- [ ] **sys_getcwd** (12) - Get current working directory
  ```c
  int sys_getcwd(char *buf, size_t size);
  // Copy process->cwd to user buffer
  ```

- [ ] **sys_chdir** (13) - Change working directory
  ```c
  int sys_chdir(const char *path);
  // Validate path exists, update process->cwd
  ```

- [ ] **sys_isatty** (14) - Check if fd is a terminal
  ```c
  int sys_isatty(int fd);
  // Return 1 for fd 0,1,2; 0 otherwise
  ```

#### Libc Implementation

- [ ] **crt0.S** - C runtime entry point
- [ ] **stdio.c** - printf, fprintf, sprintf, FILE operations
- [ ] **stdlib.c** - malloc (bump), free (no-op), atoi, strtol, qsort
- [ ] **string.c** - All string functions
- [ ] **dirent.c** - opendir, readdir, closedir
- [ ] **syscalls.c** - INT 0x80 wrappers for all syscalls
- [ ] **errno.c** - errno variable and strerror
- [ ] **stubs.c** - getpwuid, getgrgid, chmod, etc. (return errors)
- [ ] **ctype.c** - isalpha, isdigit, isspace, etc.

#### SASH Patches

- [ ] Create seedos_config.h
- [ ] Patch sash.c to disable fork/exec path
- [ ] Disable wildcard expansion (or implement)
- [ ] Test each built-in command

---

## Success Criteria

### Milestone 1: Directory Listing
- [ ] ls command shows files
- [ ] cd command works
- [ ] pwd shows current directory

### Milestone 2: Built-in Commands
- [ ] echo, cat, cp (read-only), head, tail work
- [ ] help shows available commands

### Milestone 3: Command Execution
- [ ] Can fork and exec external programs
- [ ] waitpid correctly reaps children
- [ ] Exit codes work

### Milestone 4: Full Shell
- [ ] Interactive prompt
- [ ] Ctrl+C interrupts commands
- [ ] Basic job control
- [ ] Scripting works

---

## References

- [SASH Source](https://github.com/multishell/sash)
- [Linux Syscall Table](https://syscalls.mebeim.net/)
- [musl libc](https://musl.libc.org/) - Reference for libc implementation
- [xv6 Operating System](https://github.com/mit-pdos/xv6-public) - Simple Unix implementation
