# Seed OS

A minimal x86-64 operating system built from scratch with a working userspace shell!

## About This Project

This is a hobby project I started to re-learn fundamental CS concepts in the operating systems domain.

**Goals:**
1. **Readability** - Code should be clear and well-documented
2. **Modifiability** - Easy to extend and experiment with
3. **Educational value** - Useful for anyone learning OS development

**Non-goals:**
- Performance optimization
- Completeness
- Feature richness

**Vision:** Eventually this OS will have multitasking, a networking stack, proper userspace, and a filesystem.

**Development approach:** This project relies heavily on AI-generated code. Operating systems are large projects, and to cover significant ground learning about various subsystems, AI assistance saves time on debugging and boilerplate. There is value in writing everything from scratch and fighting through every bug, but that is not the focus here. This OS is meant to be a playground for ideas, so the priority is getting basic facilities implemented and working.

## Code Style

- K&R brace style (opening brace on same line)
- No space before parens in conditionals: `if(x)` not `if (x)`
- Pointer declarations: `char *p` not `char* p`

## Current State (Multitasking Shell)

### Completed Components

**Boot & Core:**
- Limine bootloader (BIOS/UEFI compatible)
- x86-64 long mode, higher-half kernel at `0xFFFFFFFF80000000`
- HHDM (Higher Half Direct Map) at `0xFFFF800000000000`

**Memory:**
- PMM (Physical Memory Manager) - bitmap-based page allocator
- VMM (Virtual Memory Manager) - 4-level paging, per-process address spaces
- 4KB pages, kernel mapped in upper half of all address spaces

**Interrupts:**
- IDT with 48 handlers (exceptions 0-31, IRQs 32-47) + syscall (128)
- Assembly stubs in isr.S normalize error codes
- PIC remapped to IRQs 32-47

**Hardware:**
- PS/2 keyboard driver with scancode translation and blocking wait
- Serial port (COM1) for debug output
- PIT (Programmable Interval Timer) for scheduling

**Display:**
- Framebuffer graphics via Limine protocol
- 8x16 bitmap font, text console with scrolling
- `console.c` module provides unified output to both serial and framebuffer

**GDT + TSS:**
- Custom GDT with kernel (DPL=0) and user (DPL=3) segments
- TSS for RSP0 (kernel stack on ring transition)
- Segment selectors: 0x08 kernel code, 0x10 kernel data, 0x18 user code, 0x20 user data, 0x28 TSS

**Scheduler:**
- Round-robin preemptive scheduler (`sched.c`)
- Process states: PROC_UNUSED, PROC_READY, PROC_RUNNING, PROC_BLOCKED, PROC_ZOMBIE
- Timer-driven context switching
- Blocking support for waitpid (processes can wait for children)
- Interrupt-safe scheduler operations with irq_save()/irq_restore()

**Virtual Filesystem (VFS):**
- `vfs.c` - Path resolution, file descriptor management
- `tarfs.c` - Tar archive filesystem (files embedded in kernel image)
- `tar.c` - Tar archive parsing
- Vnode abstraction with read/write/close operations
- Per-process file descriptor tables

**Syscalls (22 implemented):**
- `sys_exit(code)` - terminate process
- `sys_write(fd, buf, len)` - write to file/console
- `sys_read(fd, buf, len)` - read from file/keyboard (blocking for stdin)
- `sys_getpid()` - get current process ID
- `sys_uptime()` - get system uptime in milliseconds
- `sys_sbrk(increment)` - grow/query heap
- `sys_open(path, flags)` - open file
- `sys_close(fd)` - close file descriptor
- `sys_lseek(fd, offset, whence)` - seek in file
- `sys_stat(path, buf)` - get file status
- `sys_fstat(fd, buf)` - get file status by fd
- `sys_getdents(fd, buf, count)` - read directory entries
- `sys_getcwd(buf, size)` - get current working directory
- `sys_chdir(path)` - change directory (directories only)
- `sys_isatty(fd)` - check if fd is a terminal
- `sys_dup(fd)` - duplicate file descriptor
- `sys_dup2(oldfd, newfd)` - duplicate to specific fd
- `sys_spawn(path, argv)` - spawn child process (blocking)
- `sys_spawn_async(path, argv)` - spawn child process (returns immediately)
- `sys_waitpid(pid, status, options)` - wait for child process
- `sys_shutdown()` - power off the system
- `sys_reboot()` - reboot the system

**Context Switching:**
- `context_switch_to_user()` - enter userspace, returns when sys_exit called
- `context_return_to_kernel()` - return from userspace to kernel
- Full register save/restore for preemption (RAX-R15, RIP, RSP, RFLAGS)
- Userspace exceptions return gracefully to kernel (no system halt)

**Process Management:**
- `process.c` - process lifecycle: create, load, run, destroy
- Per-process address spaces, file descriptor tables, working directory
- ELF loading with argument passing (argc/argv on stack)
- Clean memory cleanup on process exit

**ELF Loader:**
- `elf.c` - parses and loads ELF64 executables
- Validates ELF magic, class (64-bit), endianness, type (EXEC), architecture (x86-64)
- Loads PT_LOAD segments at their specified virtual addresses
- Properly handles BSS (p_memsz > p_filesz)

**Userspace C Library (libc):**
- `stdio.c` - printf, puts, getchar, fopen, fread, etc.
- `stdlib.c` - malloc, free, atoi, getenv, etc.
- `string.c` - strlen, strcpy, strcmp, memcpy, etc.
- `unistd.c` - read, write, close, chdir, getcwd, etc.
- `dirent.c` - opendir, readdir, closedir
- `ctype.c` - isalpha, isdigit, etc.

**User Programs** (in `src/userspace/`, written in C):
- `init` - init process, spawns shell
- `sh` - interactive shell with command execution
- `ls` - list directory contents
- `cat` - concatenate and display files
- `echo` - print arguments
- `pwd` - print working directory
- `clear` - clear screen
- `info` - display PID and uptime
- `stat` - display file information
- `head` / `tail` - show first/last lines
- `wc` - word/line/char count
- `grep` - pattern matching
- `sort` - sort lines
- `uniq` - filter duplicate lines
- `tr` - translate characters
- `hexdump` - hex dump of files
- `seq` - print number sequences
- `yes` / `true` / `false` - utility programs
- `uptime` - system uptime
- `shutdown` / `reboot` - system control
- `crash` - triggers page fault (for testing)

### File Structure

```
seedos/
├── src/
│   ├── boot.S           # Entry point, stack setup
│   ├── kernel.c         # Main, initialization
│   ├── linker.ld        # Kernel linker script (higher-half)
│   ├── pmm.h/c          # Physical memory manager
│   ├── vmm.h/c          # Virtual memory manager
│   ├── idt.h/c          # Interrupt descriptor table
│   ├── isr.S            # Interrupt service routines (asm)
│   ├── pic.h/c          # Programmable interrupt controller
│   ├── pit.h/c          # Programmable interval timer
│   ├── keyboard.h/c     # PS/2 keyboard driver
│   ├── shell.h/c        # Kernel shell (fallback)
│   ├── fb.h/c           # Framebuffer driver + font
│   ├── console.h/c      # Unified serial+FB output
│   ├── gdt.h/c          # Global descriptor table
│   ├── gdt_load.S       # GDT/TSS loading (asm)
│   ├── syscall.h/c      # System call handler
│   ├── context.h        # Context switching API
│   ├── context_switch.S # Context switching (asm)
│   ├── process.h/c      # Process management
│   ├── sched.h/c        # Scheduler
│   ├── programs.h/c     # User program registry
│   ├── elf.h/c          # ELF64 loader
│   ├── vfs.h/c          # Virtual filesystem
│   ├── tar.h/c          # Tar archive parsing
│   ├── tarfs.h/c        # Tar filesystem
│   ├── serial.h/c       # Serial port driver
│   ├── string.h/c       # String utilities
│   ├── libc/            # Userspace C library
│   │   ├── stdio.c
│   │   ├── stdlib.c
│   │   ├── string.c
│   │   ├── unistd.c
│   │   └── ...
│   └── userspace/
│       ├── user.ld      # User program linker script
│       ├── crt0.s       # C runtime startup
│       ├── sh.c         # Shell
│       ├── ls.c         # List directory
│       ├── cat.c        # Concatenate files
│       └── ...          # Other user programs
├── test/
│   ├── test_framework.h/c
│   ├── test_pmm.c
│   ├── test_vmm.c
│   └── ...
├── docs/
│   ├── KERNEL_CONTEXT_SWITCHING.md  # Blocking I/O requirements
│   └── GAPS.md                       # Known issues and gaps
├── limine.h
├── limine.conf
└── Makefile
```

## Build & Run

```bash
cd ~/code/seedos
make
./build-iso.sh
qemu-system-x86_64 -cdrom seed.iso -serial stdio
```

QEMU window: keyboard input, graphics output
Terminal: serial output (debugging)

## IDE Setup (VS Code)

For code navigation (Go to Definition, Find References, etc.), run:

```bash
make ide-setup
```

This requires `bear` (`sudo apt install bear`) and generates:
- `.clangd` - clangd configuration for freestanding kernel code
- `compile_commands.json` - compilation database for clangd

Then install the **clangd** extension in VS Code.

## Architecture Notes

### Memory Layout
```
0xFFFFFFFFFFFFFFFF ┐
                   │ Kernel Space (shared across all address spaces)
0xFFFFFFFF80000000 │ <- Kernel code/data
                   │
0xFFFF800000000000 │ <- HHDM (physical memory direct map)
                   ┘
    (non-canonical hole)
0x0000800000000000 ┐
                   │ User Space (per-process)
0x7FFFFF000        │ <- User stack
                   │
0x400000           │ <- User code
0x0                ┘
```

### Process States
```
PROC_UNUSED  ──create──>  PROC_READY  ──schedule──>  PROC_RUNNING
                              ^                           │
                              │                           │
                         wake │                           │ block (waitpid, I/O)
                              │                           v
                         PROC_BLOCKED <────────────────────

PROC_RUNNING ──exit──> PROC_ZOMBIE ──reap──> PROC_UNUSED
```

### Key APIs

**Console output:**
- `puts()`, `putc()`, `put_hex()`, `put_dec()` - dual serial+framebuffer

**Memory:**
- `pmm_alloc()` / `pmm_free()` - physical page allocation
- `vmm_create_address_space()` - new PML4 with kernel mapped
- `vmm_map_page(pml4, virt, phys, flags)` - map a page

**Process management:**
- `process_create()` - allocate process with address space
- `process_load_elf()` - load ELF into process
- `process_start()` - add to scheduler
- `process_destroy()` - cleanup all resources

**Scheduler:**
- `sched_add()` / `sched_remove()` - manage ready queue
- `schedule()` - pick next process (called from timer)
- `sched_block_on_pid()` - block waiting for child
- `sched_wake_waiters()` - wake processes waiting for PID

**VFS:**
- `vfs_resolve_path()` - normalize path for filesystem lookup
- `vfs_lookup()` - find file by path
- `vfs_alloc_fd()` / `vfs_free_fd()` - manage file descriptors

**GDT selectors (gdt.h):**
- `GDT_KERNEL_CODE` (0x08), `GDT_KERNEL_DATA` (0x10)
- `GDT_USER_CODE` (0x18), `GDT_USER_DATA` (0x20)
- `GDT_TSS` (0x28)

## Known Limitations

See `docs/GAPS.md` for detailed list. Key items:
- No per-process kernel stacks (limits true blocking I/O)
- Single shared kernel stack for all processes
- Keyboard blocking uses polling with HLT, not true sleep
- No fork() - only spawn()

## Documentation

| Document | Description |
|----------|-------------|
| [KERNEL_CONTEXT_SWITCHING.md](docs/KERNEL_CONTEXT_SWITCHING.md) | Blocking I/O requirements |
| [GAPS.md](docs/GAPS.md) | Known issues and implementation gaps |

## Future Ideas

- Per-process kernel stacks (enables true blocking I/O)
- Pipes and I/O redirection
- Signal handling
- Network stack
- Writable filesystem

## Development Notes

When writing code or documentation, remember to create regular commits for tracking. If changes are big enough, consider branches.
