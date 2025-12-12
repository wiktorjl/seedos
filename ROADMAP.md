# Seed OS Roadmap

This document outlines implementation plans for the next major features.

---

## Option 1: Process Scheduler

Round-robin multitasking to run multiple processes concurrently.

### Prerequisites
- Preemptive timer (PIT IRQ0 forces context switches)

### Implementation Steps

**Phase 1: Process Table**
1. Create `scheduler.h/c` module
2. Define process states: `READY`, `RUNNING`, `BLOCKED`, `TERMINATED`
3. Extend `struct process` with:
   - `pid` - unique process ID
   - `state` - current state
   - `saved_rsp` - stack pointer when preempted
   - `next` - linked list pointer (or use array)
4. Create process table (fixed array of N processes, e.g., 16)
5. Implement `scheduler_add()`, `scheduler_remove()`

**Phase 2: Context Save/Restore**
1. Modify `context_switch.S` to save full register state (not just entry point)
2. Add `struct cpu_state` with all GP registers, RIP, RSP, RFLAGS
3. On timer interrupt in userspace:
   - Save current process state
   - Call scheduler to pick next process
   - Restore next process state
   - IRETQ to new process

**Phase 3: Scheduler Logic**
1. Implement round-robin: cycle through READY processes
2. `scheduler_tick()` - called from PIT handler
3. `scheduler_yield()` - voluntary yield (new syscall)
4. `scheduler_block()` / `scheduler_unblock()` - for future I/O wait

**Phase 4: Integration**
1. Modify `process_run()` to add process to scheduler instead of blocking
2. Add `sys_fork()` or `sys_spawn()` syscall to create new processes
3. Handle process termination (remove from table, free resources)
4. Shell spawns processes instead of running them synchronously

### Files to Create/Modify
- `src/scheduler.h` - new
- `src/scheduler.c` - new
- `src/context_switch.S` - modify for full save/restore
- `src/process.h/c` - extend struct, integrate with scheduler
- `src/idt.c` - timer handler calls scheduler
- `src/syscall.c` - add yield, spawn syscalls

### Challenges
- Stack management: each process needs its own kernel stack
- Synchronization: disable interrupts during scheduler operations
- Testing: hard to debug concurrent execution

---

## Option 2: ELF Loader

Load programs from ELF binaries instead of hardcoded byte arrays.

### Prerequisites
- None (can work with embedded ELF binaries first)
- Filesystem (for loading from disk - optional enhancement)

### Implementation Steps

**Phase 1: ELF Header Parsing**
1. Create `elf.h` with ELF64 structures:
   - `Elf64_Ehdr` - ELF header
   - `Elf64_Phdr` - program header
   - `Elf64_Shdr` - section header (optional)
2. Implement `elf_validate()` - check magic, class (64-bit), type (executable)
3. Implement `elf_get_entry()` - extract entry point address

**Phase 2: Program Header Processing**
1. Implement `elf_load_segments()`:
   - Iterate program headers
   - Find PT_LOAD segments
   - For each segment:
     - Allocate physical pages
     - Map to specified virtual address
     - Copy segment data
     - Handle BSS (zero-fill beyond file size)
2. Handle segment permissions (PTE flags from p_flags)

**Phase 3: Integration**
1. Modify build system to produce ELF binaries (not raw .bin)
2. Embed ELF in kernel (like current user_program.c)
3. Modify `process_load()` to call ELF loader
4. Entry point comes from ELF header, not hardcoded

**Phase 4: Multiple Programs**
1. Build system compiles multiple .s/.c files to separate ELF binaries
2. Embed multiple ELFs with different symbol names
3. Register each in `programs_init()`

### Files to Create/Modify
- `src/elf.h` - new (ELF structures and constants)
- `src/elf.c` - new (parsing and loading)
- `src/process.c` - use ELF loader instead of raw memcpy
- `Makefile` - produce ELF binaries for userspace
- `src/userspace/*.c` - user programs in C (with minimal runtime)

### ELF Header Quick Reference
```c
typedef struct {
    unsigned char e_ident[16];  // Magic: 0x7F 'E' 'L' 'F'
    uint16_t e_type;            // ET_EXEC = 2
    uint16_t e_machine;         // EM_X86_64 = 62
    uint32_t e_version;
    uint64_t e_entry;           // Entry point virtual address
    uint64_t e_phoff;           // Program header table offset
    uint64_t e_shoff;           // Section header table offset
    uint32_t e_flags;
    uint16_t e_ehsize;          // ELF header size
    uint16_t e_phentsize;       // Program header entry size
    uint16_t e_phnum;           // Number of program headers
    // ... more fields
} Elf64_Ehdr;
```

---

## Option 3: Filesystem

Read files from disk, starting with a simple in-memory filesystem.

### Prerequisites
- None for ramdisk
- Disk driver for real filesystem (ATA/AHCI/virtio-blk)

### Implementation Steps

**Phase 1: VFS Layer (Virtual Filesystem)**
1. Create `vfs.h/c` with abstract filesystem interface:
   ```c
   struct file_operations {
       int (*open)(const char *path, int flags);
       int (*close)(int fd);
       ssize_t (*read)(int fd, void *buf, size_t count);
       ssize_t (*write)(int fd, const void *buf, size_t count);
   };
   ```
2. File descriptor table (per-process, or global for now)
3. Implement `vfs_open()`, `vfs_close()`, `vfs_read()`, `vfs_write()`

**Phase 2: Ramdisk Filesystem**
1. Create `ramfs.h/c` - simple in-memory filesystem
2. Structure:
   - Fixed array of file entries (name, data pointer, size)
   - Files embedded at compile time (like current user programs)
3. Implement ramfs operations for VFS
4. Mount ramfs at boot

**Phase 3: Syscall Integration**
1. Add syscalls: `sys_open()`, `sys_close()`, `sys_read()`
2. Modify `sys_write()` to use VFS (fd 1 = stdout special case)
3. User programs can now open/read files

**Phase 4: Real Filesystem (Future)**
1. Implement disk driver (ATA PIO is simplest)
2. Implement FAT16/FAT32 or ext2 reader
3. Parse partition table (MBR or GPT)
4. Mount real filesystem alongside ramfs

### Files to Create/Modify
- `src/vfs.h` - new (VFS interface)
- `src/vfs.c` - new (VFS implementation)
- `src/ramfs.h` - new (ramdisk filesystem)
- `src/ramfs.c` - new
- `src/syscall.c` - add open/close/read
- `src/process.h` - add file descriptor table

### VFS Design
```
                    ┌─────────────┐
    Userspace       │  sys_read() │
                    └──────┬──────┘
                           │
    ─────────────────────────────────────
                           │
    Kernel             ┌───▼───┐
                       │  VFS  │
                       └───┬───┘
                           │
              ┌────────────┼────────────┐
              │            │            │
          ┌───▼───┐   ┌────▼────┐  ┌────▼────┐
          │ ramfs │   │  FAT32  │  │  ext2   │
          └───────┘   └─────────┘  └─────────┘
```

---

## Option 4: More Syscalls

Expand the syscall interface for richer user programs.

### Implementation Steps

**Phase 1: Memory Syscalls**
1. `sys_sbrk(increment)` - grow/shrink heap
   - Track process break (heap end)
   - Allocate pages as needed
   - Map with PTE_USER | PTE_WRITABLE
   - Return old break address

2. `sys_mmap(addr, len, prot, flags)` - simplified version
   - Allocate physical pages
   - Map to requested (or chosen) virtual address
   - Return mapped address

**Phase 2: Process Syscalls**
1. `sys_getpid()` - return current process ID
2. `sys_yield()` - voluntarily give up CPU (needs scheduler)
3. `sys_sleep(ms)` - sleep for milliseconds (needs scheduler + timer)
4. `sys_spawn(name)` - start new process (needs scheduler)
5. `sys_wait(pid)` - wait for process to exit (needs scheduler)

**Phase 3: File Syscalls** (needs VFS)
1. `sys_open(path, flags)` - open file, return fd
2. `sys_close(fd)` - close file descriptor
3. `sys_read(fd, buf, count)` - read from file

**Phase 4: Info Syscalls**
1. `sys_uptime()` - return system uptime in ms
2. `sys_meminfo()` - return free/total memory

### Syscall Number Assignments
```c
#define SYS_EXIT    0   // existing
#define SYS_WRITE   1   // existing
#define SYS_READ    2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_SBRK    5
#define SYS_GETPID  6
#define SYS_YIELD   7
#define SYS_SPAWN   8
#define SYS_WAIT    9
#define SYS_UPTIME  10
#define SYS_MMAP    11
```

### Files to Modify
- `src/syscall.h` - add syscall numbers
- `src/syscall.c` - implement handlers
- `src/process.h` - add heap tracking (brk), pid
- `src/vmm.c` - helper for dynamic page mapping

### User Library
With more syscalls, create `src/userspace/lib.s` or `lib.c`:
```c
// Wrapper functions for userspace
long write(int fd, const void *buf, size_t len);
void exit(int code);
void *sbrk(long increment);
int getpid(void);
```

---

## Recommended Order

1. **Option 4 (Syscalls)** - Low complexity, provides building blocks
2. **Option 2 (ELF Loader)** - Medium complexity, enables real programs
3. **Option 1 (Scheduler)** - Medium-high complexity, requires timer work
4. **Option 3 (Filesystem)** - High complexity, but scheduler helps

Alternative order if you want multitasking sooner:
1. **Preemptive timer** (subset of scheduler)
2. **Option 1 (Scheduler)**
3. **Option 4 (Syscalls)**
4. **Option 2 (ELF Loader)**
5. **Option 3 (Filesystem)**
