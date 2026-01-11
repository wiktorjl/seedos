# SeedOS Userspace Implementation Plan

## Overview

Add full userspace support with **Linux x86-64 binary compatibility**. Goal: run statically-linked Linux binaries (busybox, coreutils).

### Architectural Decisions
- **Syscall mechanism**: `syscall/sysret` (fast path, modern)
- **Linux ABI**: Full compatibility (matching syscall numbers, stack layout)
- **fork() model**: Copy-on-Write
- **Filesystem**: ext2 (read-only initially) via RAM disk (Limine module)
- **Init process**: Load `/init` from ext2 filesystem

---

## Architectural Tradeoffs

This section documents key design decisions and the reasoning behind them.

### 1. Process vs Thread Model

**Decision:** Process wraps kthread; plan for 1:1 threading via clone() later.

```
process_t
├── kthread_t *kthread     (kernel execution context)
├── pml4_phys              (address space)
├── fd_table[]             (file descriptors)
└── ...
```

**Rationale:**
- Simple initial implementation leveraging existing scheduler
- Clean separation between user process state and kernel execution context
- Extensible: multiple kthreads can later share pml4_phys/fd_table via clone()

**Alternatives considered:**
- Unified structure: Cache-friendly but harder to share scheduler with kernel-only threads
- M:N threading: Complex, rarely used (Linux abandoned this with NPTL)

---

### 2. Syscall Entry Design

**Decision:** Per-process kernel stacks (16KB each) with interrupt-like frame.

**Rationale:**
- **Per-process stacks required for blocking syscalls.** When `read()` sleeps waiting for input, the entire kernel call stack must be preserved. Per-CPU stacks would require copying the stack before sleeping.
- **Interrupt-like frame** (push SS, RSP, RFLAGS, CS, RIP) enables unified handling with hardware interrupts and simplifies signal delivery.

**Cost:** 16KB memory per process, must update TSS.rsp0 on every context switch.

**Alternative considered:**
- Per-CPU stacks with context save: Would require expensive stack copying or continuation-based design. Not worth the complexity.

---

### 3. Copy-on-Write Implementation

**Decision:** Per-page struct array for refcounting; deep page table copy with COW leaves.

**Reference counting:**
```c
// ~0.2% memory overhead (8 bytes per 4KB page)
struct page_info pages[MAX_PHYS_PAGES];
pages[phys >> 12].refcount++;
```

**Page table copy strategy:**
- Deep copy all 4 levels of page tables on fork()
- Only leaf pages (PTEs) share physical memory with COW
- On write fault: copy page if refcount > 1, else just make writable

**Rationale:**
- Per-page struct enables future features (swapping, page cache, LRU)
- Deep copy is simpler than lazy copy at all page table levels
- Most forks immediately exec anyway (discarding the copy)

**Alternative considered:**
- Lazy page table copy: Would require COW at PDPT/PD/PT levels, significantly more complex page fault handling
- Hash table for refcounts: Slower lookup, collision handling

---

### 4. ext2 Design

**Decision:** No caching for RAM disk; direct pointer access; walk path on every open.

**Rationale for no caching:**
- RAM disk is already in memory! A cache would be a redundant copy
- Read-only filesystem doesn't need write buffering
- For future disk drivers, add caching at the block layer

**Rationale for walk-every-time:**
- Simple implementation
- For loading /init and a few binaries, performance is adequate
- Directory entry cache (dcache) can be added later when needed

---

### 5. Summary of Design Choices

| Area | Decision | Rationale |
|------|----------|-----------|
| Process model | Process wraps kthread | Simple, leverages scheduler, extensible to threads |
| Kernel stacks | Per-process (16KB) | Required for blocking syscalls |
| Syscall frame | Interrupt-like (5 pushes) | Unified with interrupt handling, simplifies signals |
| COW refcounting | Per-page struct array | O(1) lookup, extensible for future features |
| Page table copy | Deep copy + COW leaves | Simpler than lazy copy at all levels |
| ext2 caching | None (RAM disk) | Already in memory, no point caching |
| Path lookup | Walk every time | Good enough for initial use case |

---

## Phase 1: Ring 3 Infrastructure

### Task 1.1: Custom GDT with User Segments ✓ COMPLETE
- [x] Create `arch/x86/kernel/gdt.h`
  - GDT entry structure (`gdt_entry_t`)
  - TSS descriptor structure (`gdt_tss_entry_t`)
  - GDTR structure
  - Selector constants: `GDT_KERNEL_CODE` (0x08), `GDT_KERNEL_DATA` (0x10), `GDT_USER_DATA` (0x18), `GDT_USER_CODE` (0x20), `GDT_TSS` (0x28)
  - Note: User Data comes before User Code for SYSRET compatibility
- [x] Create `arch/x86/kernel/gdt.c`
  - GDT table with 7 entries (null, kernel code/data, user code/data, TSS)
  - `gdt_init()` - build GDT, load with lgdt
  - User segments: DPL=3, access byte 0xFA (code) / 0xF2 (data)
- [x] Create `arch/x86/kernel/gdt_asm.S` (renamed from gdt.S to avoid build collision)
  - `gdt_reload` - reload segment registers after lgdt
  - Far return trick to reload CS
- [x] Modify `arch/x86/kernel/idt.c`
  - Replace `GDT_SELECTOR_FROM_LIMINE` with `GDT_KERNEL_CODE`
- [x] Modify `init/main.c`
  - Call `gdt_init()` before `idt_install()`

**GDT Layout:**
```
Index  Offset  Segment
0      0x00    NULL
1      0x08    Kernel Code (DPL=0)
2      0x10    Kernel Data (DPL=0)
3      0x18    User Data (DPL=3)    ← NOTE: Data before Code!
4      0x20    User Code (DPL=3)
5-6    0x28    TSS (16 bytes)
```

**⚠️ CRITICAL: SYSRET Segment Order Requirement**

The `sysretq` instruction has fixed expectations about GDT layout relative to STAR MSR:
- **SS** = STAR[63:48] + 8
- **CS** = STAR[63:48] + 16 (for 64-bit mode)

With STAR[63:48] = 0x10 (pointing to kernel data as base):
- SS = 0x10 + 8 = 0x18 → must be **User Data**
- CS = 0x10 + 16 = 0x20 → must be **User Code**

**This means User Data (0x18) MUST come before User Code (0x20) in the GDT!**

The original layout in this plan had them reversed. The corrected layout above matches Linux and allows SYSRET to work correctly.

**STAR MSR configuration:**
```c
// STAR[47:32] = kernel CS (for SYSCALL entry): 0x08
// STAR[63:48] = user CS/SS base (for SYSRET): 0x10
// SYSRET will load: SS = 0x18|3 = 0x1B, CS = 0x20|3 = 0x23
uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
wrmsr(MSR_STAR, star);
```

**Test:** Boot with new GDT, verify all existing functionality works.

---

### Task 1.2: TSS (Task State Segment) ✓ COMPLETE
- [x] Add TSS structure to `gdt.h` (named `x86_tss_t` to avoid C11 conflict)
  ```c
  typedef struct {
      uint32_t reserved0;
      uint64_t rsp0;       // Ring 0 stack - loaded on Ring 3→0 transition
      uint64_t rsp1;
      uint64_t rsp2;
      uint64_t reserved1;
      uint64_t ist[7];     // Interrupt Stack Table
      uint64_t reserved2;
      uint16_t reserved3;
      uint16_t iopb_offset;
  } __attribute__((packed)) x86_tss_t;
  ```
- [x] Add to `gdt.c`
  - Global `static x86_tss_t tss`
  - Initialize TSS descriptor in GDT (base address split across fields)
  - `gdt_set_tss_rsp0(uint64_t rsp0)` - update rsp0 on context switch
  - Load TSS with `ltr` instruction after GDT load

**Test:** `ltr` executes without #GP fault. ✓ Verified

---

### Task 1.3: Per-CPU Data Structure ✓
- [x] Create `arch/x86/kernel/percpu.h`
  ```c
  typedef struct {
      struct process *current;    // Currently running process
      uint64_t kernel_rsp;        // Kernel stack for syscall entry
      uint64_t user_rsp;          // Saved user RSP during syscall
  } percpu_t;

  void percpu_init(void);
  void percpu_set_kernel_stack(uint64_t rsp);
  percpu_t *percpu_get(void);
  ```
- [x] Create `arch/x86/kernel/percpu.c`
  - Global `percpu_t cpu0_percpu`
  - `percpu_init()` - set `MSR_GS_BASE` (0xC0000101) and `MSR_KERNEL_GS_BASE` (0xC0000102)
  - Read/write MSR helper functions

**Note:** `swapgs` instruction swaps GS_BASE with KERNEL_GS_BASE. Used at syscall entry/exit.

**Test:** `percpu_init()` sets both MSRs correctly. ✓ Verified

---

### Task 1.4: FPU/SSE State Management
- [ ] Add to `kernel/process.h` (deferred until Phase 3)
  ```c
  // FPU/SSE state (512 bytes for fxsave, 16-byte aligned)
  typedef struct {
      uint8_t state[512] __attribute__((aligned(16)));
  } fpu_state_t;

  // Add to process_t:
  fpu_state_t *fpu_state;      // NULL until first FPU use
  bool fpu_used;               // Has this process used FPU?
  ```
- [x] Create `arch/x86/kernel/fpu.h`
  ```c
  void fpu_init(void);                    // Enable SSE in CR0/CR4
  void fpu_save(fpu_state_t *state);      // fxsave
  void fpu_restore(fpu_state_t *state);   // fxrstor
  void fpu_init_state(fpu_state_t *state); // Initialize clean FPU state
  ```
- [x] Create `arch/x86/kernel/fpu.c`
  ```c
  void fpu_init(void) {
      uint64_t cr0, cr4;

      // Clear CR0.EM (emulation), set CR0.MP (monitor coprocessor)
      __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
      cr0 &= ~(1 << 2);  // Clear EM
      cr0 |= (1 << 1);   // Set MP
      __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

      // Set CR4.OSFXSR (enable SSE) and CR4.OSXMMEXCPT (SSE exceptions)
      __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
      cr4 |= (1 << 9);   // OSFXSR
      cr4 |= (1 << 10);  // OSXMMEXCPT
      __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
  }

  void fpu_save(fpu_state_t *state) {
      __asm__ volatile("fxsave %0" : "=m"(*state));
  }

  void fpu_restore(fpu_state_t *state) {
      __asm__ volatile("fxrstor %0" :: "m"(*state));
  }
  ```
- [ ] Modify `kernel/process.c` - context switch (deferred until Phase 3)
  - Save FPU state when switching away from process that used FPU
  - Restore FPU state when switching to process with saved FPU state
- [x] Call `fpu_init()` in `kmain()` after GDT setup

**Test:** CR0/CR4 bits set correctly. ✓ Verified

**Implementation choice: Eager vs Lazy FPU**

| Approach | Pros | Cons |
|----------|------|------|
| **Eager** (always save/restore) | Simple, predictable | Wastes cycles if process doesn't use FPU |
| **Lazy** (save on #NM trap) | Efficient for non-FPU processes | More complex, CR0.TS bit management |

**Recommendation:** Start with eager save/restore. Optimize to lazy later if context switch overhead becomes an issue.

**Test:** Run userspace program with SSE instructions, verify no #UD or corruption.

---

## Phase 2: Syscall Mechanism

### Task 2.1: syscall/sysret MSR Setup
- [ ] Create `arch/x86/kernel/syscall.h`
  ```c
  #define MSR_EFER        0xC0000080
  #define MSR_STAR        0xC0000081
  #define MSR_LSTAR       0xC0000082
  #define MSR_SFMASK      0xC0000084

  #define EFER_SCE        (1 << 0)  // System Call Enable

  void syscall_init(void);
  ```
- [ ] Create `arch/x86/kernel/syscall.c`
  - `syscall_init()`:
    - Enable SCE bit in EFER
    - STAR = segment selectors (kernel CS/SS at bits 47:32, user at 63:48)
    - LSTAR = address of `syscall_entry`
    - SFMASK = 0x200 (clear IF on entry)

**STAR Register Layout:**
```
Bits 63:48 = SYSRET CS/SS base (user segments)
Bits 47:32 = SYSCALL CS/SS base (kernel segments)
Bits 31:0  = Reserved
```

---

### Task 2.2: Syscall Entry/Exit Assembly
- [ ] Create `arch/x86/kernel/syscall_entry.S`

```asm
.global syscall_entry
syscall_entry:
    # On entry: RCX=user RIP, R11=user RFLAGS, RAX=syscall#
    # RDI,RSI,RDX,R10,R8,R9 = arguments (Linux convention)

    swapgs                          # Switch to kernel GS
    movq %rsp, %gs:PERCPU_USER_RSP  # Save user RSP
    movq %gs:PERCPU_KERNEL_RSP, %rsp # Load kernel RSP

    # Build syscall frame
    pushq $(GDT_USER_DATA | 3)      # SS
    pushq %gs:PERCPU_USER_RSP       # RSP
    pushq %r11                      # RFLAGS
    pushq $(GDT_USER_CODE | 3)      # CS
    pushq %rcx                      # RIP

    # Save callee-saved registers
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # Linux ABI: R10 is 4th arg (RCX clobbered by syscall)
    movq %r10, %rcx

    # Call C dispatcher
    call syscall_dispatch

    # Restore callee-saved
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    # Restore user state
    popq %rcx                       # RIP
    addq $8, %rsp                   # Skip CS
    popq %r11                       # RFLAGS
    popq %rsp                       # RSP (skip SS too)

    swapgs
    sysretq
```

**⚠️ SYSRET Vulnerability (CVE-2012-0217):**
Intel CPUs have a bug where `sysretq` with a non-canonical RIP causes #GP to fire in ring 0 but with user-controlled RSP. This is exploitable.

**Mitigation options:**
1. **Validate RIP before sysretq** - Check if RIP is in the "syscall gap" (0x00007ffffffff000 - 0x00007fffffffffff)
2. **Use iretq for unsafe cases** - Slower but safe; Linux uses this path after signal delivery

```asm
    # Before sysretq, validate RIP (in %rcx)
    movq $0x00007fffffffffff, %r10
    cmpq %r10, %rcx
    ja use_iret                    # RIP too high - use safe path
    shlq $16, %rcx
    sarq $16, %rcx                 # Sign-extend to check canonical
    cmpq %rcx, <original_rcx>
    jne use_iret                   # Non-canonical - use safe path
    # Safe to use sysretq
```

For initial implementation, always using `iretq` is acceptable (the syscall_entry already builds an iret frame). Optimize to `sysretq` later with proper validation.

---

### Task 2.3: Syscall Dispatch Table
- [ ] Create `kernel/syscall_table.h`
  ```c
  // Linux x86-64 syscall numbers
  #define SYS_read      0
  #define SYS_write     1
  #define SYS_open      2
  #define SYS_close     3
  #define SYS_fstat     5
  #define SYS_mmap      9
  #define SYS_munmap    11
  #define SYS_brk       12
  #define SYS_getpid    39
  #define SYS_fork      57
  #define SYS_execve    59
  #define SYS_exit      60
  #define SYS_wait4     61
  #define SYS_kill      62
  #define SYS_arch_prctl 158

  #define NR_SYSCALLS   512
  ```
- [ ] Add to `syscall.c`
  - `syscall_fn_t syscall_table[NR_SYSCALLS]`
  - `syscall_dispatch(nr, arg1-6)` - validate nr, call handler
  - Initial stubs: `sys_exit`, `sys_write`, `sys_getpid`

---

## Phase 3: Process Abstraction

### Task 3.1: Process Structure
- [ ] Create `kernel/process.h`
  ```c
  typedef enum {
      PROC_EMBRYO,
      PROC_RUNNABLE,
      PROC_RUNNING,
      PROC_SLEEPING,
      PROC_ZOMBIE,
  } proc_state_t;

  typedef struct process {
      // Identity
      uint64_t pid;
      char name[64];
      proc_state_t state;

      // Memory
      uint64_t pml4_phys;
      uint64_t brk;

      // Kernel context
      kthread_t *kthread;
      uint64_t kernel_stack_top;

      // User context (saved on syscall/interrupt)
      uint64_t user_rip, user_rsp, user_rflags;

      // File descriptors
      fd_entry_t fd_table[256];

      // Process tree
      struct process *parent;
      struct process *children;
      struct process *sibling;
      int exit_code;

      // Signals
      uint64_t pending_signals;
      sigaction_t sigactions[32];

      // List linkage
      struct process *next;
  } process_t;
  ```

---

### Task 3.2: Process Lifecycle
- [ ] Create `kernel/process.c`
  - `process_t *process_create(const char *name)`
    - Allocate PCB
    - Allocate 16KB kernel stack
    - Create address space (`vmm_create_address_space`)
    - Initialize FD table (stdin=0, stdout=1, stderr=2)
    - Allocate PID
  - `void process_destroy(process_t *proc)`
    - Free kernel stack
    - Free address space
    - Free PCB
  - `void process_switch(process_t *next)`
    - Update `TSS.rsp0` to next's kernel stack
    - Update percpu kernel_rsp
    - Switch CR3 if different address space
    - Context switch via kthread

---

### Task 3.3: Exit Syscall
- [ ] Implement `sys_exit(int status)` in `kernel/process.c`
  - Set state to ZOMBIE
  - Store exit code
  - Reparent children to init (PID 1)
  - Send SIGCHLD to parent
  - Wake parent if waiting
  - Schedule next process

---

## Phase 4: VFS Layer

### Task 4.1: VFS Interface
- [ ] Create `fs/vfs.h`
  ```c
  typedef struct vfs_file {
      struct vfs_inode *inode;
      file_ops_t *ops;
      uint64_t offset;
      int flags;
      int refcount;
  } vfs_file_t;

  typedef struct {
      ssize_t (*read)(vfs_file_t*, void*, size_t);
      ssize_t (*write)(vfs_file_t*, const void*, size_t);
      int (*close)(vfs_file_t*);
      off_t (*lseek)(vfs_file_t*, off_t, int);
  } file_ops_t;
  ```
- [ ] Create `fs/vfs.c`
  - `vfs_open()`, `vfs_close()`, `vfs_read()`, `vfs_write()`

---

### Task 4.2: Console Device
- [ ] Create `drivers/tty/tty_dev.c`
  ```c
  static ssize_t tty_read(vfs_file_t *f, void *buf, size_t count);
  static ssize_t tty_write(vfs_file_t *f, const void *buf, size_t count);

  file_ops_t tty_ops = { .read = tty_read, .write = tty_write };

  vfs_file_t stdin_file, stdout_file, stderr_file;
  ```
- [ ] Wire to keyboard input (read) and terminal output (write)

---

### Task 4.3: File Descriptor Syscalls
- [ ] Implement in `kernel/process.c`:
  - `sys_read(fd, buf, count)` - validate fd, copy to user
  - `sys_write(fd, buf, count)` - validate fd, copy from user
  - `sys_close(fd)` - decrement refcount, clear entry

---

## Phase 5: ext2 Filesystem (Read-Only)

### Task 5.1: Limine Module Request
- [ ] Modify `arch/x86/boot/limine.c`
  - Add module request for initrd
  ```c
  __attribute__((used, section(".limine_requests")))
  static volatile struct limine_module_request module_request = {
      .id = LIMINE_MODULE_REQUEST,
      .revision = 0,
  };
  ```
- [ ] Modify `arch/x86/boot/limine.conf`
  ```
  MODULE_PATH=boot:///initrd.ext2
  ```
- [ ] Add getter: `limine_get_module(index)` returns base address and size

---

### Task 5.2: ext2 On-Disk Structures
- [ ] Create `fs/ext2.h`
  ```c
  // Superblock (at byte offset 1024)
  typedef struct {
      uint32_t s_inodes_count;
      uint32_t s_blocks_count;
      uint32_t s_r_blocks_count;
      uint32_t s_free_blocks_count;
      uint32_t s_free_inodes_count;
      uint32_t s_first_data_block;
      uint32_t s_log_block_size;     // block_size = 1024 << s_log_block_size
      uint32_t s_log_frag_size;
      uint32_t s_blocks_per_group;
      uint32_t s_frags_per_group;
      uint32_t s_inodes_per_group;
      uint32_t s_mtime;
      uint32_t s_wtime;
      uint16_t s_mnt_count;
      uint16_t s_max_mnt_count;
      uint16_t s_magic;              // 0xEF53
      uint16_t s_state;
      uint16_t s_errors;
      uint16_t s_minor_rev_level;
      uint32_t s_lastcheck;
      uint32_t s_checkinterval;
      uint32_t s_creator_os;
      uint32_t s_rev_level;
      uint16_t s_def_resuid;
      uint16_t s_def_resgid;
      // Rev 1 fields
      uint32_t s_first_ino;
      uint16_t s_inode_size;         // Usually 128 or 256
      // ... more fields
  } __attribute__((packed)) ext2_superblock_t;

  // Block Group Descriptor
  typedef struct {
      uint32_t bg_block_bitmap;
      uint32_t bg_inode_bitmap;
      uint32_t bg_inode_table;
      uint16_t bg_free_blocks_count;
      uint16_t bg_free_inodes_count;
      uint16_t bg_used_dirs_count;
      uint16_t bg_pad;
      uint8_t  bg_reserved[12];
  } __attribute__((packed)) ext2_group_desc_t;

  // Inode (128 bytes in rev 0, s_inode_size in rev 1)
  typedef struct {
      uint16_t i_mode;
      uint16_t i_uid;
      uint32_t i_size;
      uint32_t i_atime;
      uint32_t i_ctime;
      uint32_t i_mtime;
      uint32_t i_dtime;
      uint16_t i_gid;
      uint16_t i_links_count;
      uint32_t i_blocks;
      uint32_t i_flags;
      uint32_t i_osd1;
      uint32_t i_block[15];   // 0-11: direct, 12: indirect, 13: double, 14: triple
      uint32_t i_generation;
      uint32_t i_file_acl;
      uint32_t i_dir_acl;
      uint32_t i_faddr;
      uint8_t  i_osd2[12];
  } __attribute__((packed)) ext2_inode_t;

  // Directory entry
  typedef struct {
      uint32_t inode;
      uint16_t rec_len;
      uint8_t  name_len;
      uint8_t  file_type;
      char     name[];
  } __attribute__((packed)) ext2_dirent_t;

  #define EXT2_ROOT_INO 2
  ```

---

### Task 5.3: ext2 Implementation
- [ ] Create `fs/ext2.c`
  - `ext2_init(void *ramdisk, size_t size)` - parse superblock, validate magic
  - `ext2_read_inode(uint32_t ino)` - locate and read inode
  - `ext2_read_block(uint32_t block_num, void *buf)` - read a block
  - `ext2_lookup(const char *path)` - traverse path, return inode number
  - `ext2_read_file(ext2_inode_t *inode, uint64_t offset, void *buf, size_t size)`
    - Handle direct blocks (i_block[0-11])
    - Handle indirect block (i_block[12])
    - Handle double indirect (i_block[13])
  - `ext2_file_ops` for VFS integration
- [ ] Modify `Makefile` - add `fs/` to source directories

**Test:** Read `/init` from ext2 image, print first 16 bytes.

---

## Phase 6: ELF Loader

### Task 6.1: ELF64 Structures
- [ ] Create `kernel/elf.h`
  ```c
  typedef struct {
      uint8_t  e_ident[16];     // Magic: 0x7f 'E' 'L' 'F'
      uint16_t e_type;          // ET_EXEC = 2
      uint16_t e_machine;       // EM_X86_64 = 62
      uint32_t e_version;
      uint64_t e_entry;         // Entry point
      uint64_t e_phoff;         // Program header offset
      uint64_t e_shoff;
      uint32_t e_flags;
      uint16_t e_ehsize;
      uint16_t e_phentsize;
      uint16_t e_phnum;         // Number of program headers
      uint16_t e_shentsize;
      uint16_t e_shnum;
      uint16_t e_shstrndx;
  } __attribute__((packed)) Elf64_Ehdr;

  typedef struct {
      uint32_t p_type;          // PT_LOAD = 1
      uint32_t p_flags;         // PF_X=1, PF_W=2, PF_R=4
      uint64_t p_offset;
      uint64_t p_vaddr;
      uint64_t p_paddr;
      uint64_t p_filesz;
      uint64_t p_memsz;         // >= filesz, difference is BSS
      uint64_t p_align;
  } __attribute__((packed)) Elf64_Phdr;

  #define PT_LOAD 1
  #define PT_INTERP 3
  ```

---

### Task 6.2: ELF Loader Implementation
- [ ] Create `kernel/elf.c`
  - `int elf_load(process_t *proc, const void *data, size_t size, uint64_t *entry)`
    1. Validate magic (`\x7fELF`), class (64-bit), machine (x86-64)
    2. For each PT_LOAD segment:
       - Calculate number of pages needed
       - Allocate physical pages via `pmm_alloc()`
       - Map to p_vaddr with correct flags (PTE_USER, PTE_WRITABLE if PF_W)
       - Copy p_filesz bytes from file
       - Zero remaining (p_memsz - p_filesz) for BSS
    3. Set `*entry = e_entry`

---

### Task 6.3: Linux-Compatible Stack Setup
- [ ] Add to `kernel/elf.c`
  ```c
  // Auxiliary vector types
  #define AT_NULL     0
  #define AT_PHDR     3    // Program header address
  #define AT_PHENT    4    // Program header entry size
  #define AT_PHNUM    5    // Number of program headers
  #define AT_PAGESZ   6    // Page size (4096)
  #define AT_ENTRY    9    // Entry point
  #define AT_RANDOM   25   // 16 random bytes

  uint64_t setup_user_stack(process_t *proc, char **argv, char **envp,
                            uint64_t entry, uint64_t phdr, int phnum);
  ```

**Stack Layout (addresses grow down):**
```
USER_STACK_TOP (0x800000000)
    [string data for argv/envp]
    [16 random bytes for AT_RANDOM]
    [padding to 16-byte alignment]
    [AT_NULL, 0]
    [AT_RANDOM, random_addr]
    [AT_ENTRY, entry]
    [AT_PHNUM, phnum]
    [AT_PHENT, 56]
    [AT_PHDR, phdr]
    [AT_PAGESZ, 4096]
    [NULL]                  <- envp terminator
    [envp[n], envp[n-1]...] <- environment pointers
    [NULL]                  <- argv terminator
    [argv[n], argv[n-1]...] <- argument pointers
    [argc]                  <- RSP points here
```

---

### Task 6.4: Enter Userspace
- [ ] Create `arch/x86/kernel/usermode.S`
  ```asm
  .global user_mode_enter
  # void user_mode_enter(uint64_t entry, uint64_t user_rsp)
  user_mode_enter:
      cli

      # Build iret frame
      pushq $(GDT_USER_DATA | 3)   # SS
      pushq %rsi                   # RSP
      pushq $0x202                 # RFLAGS (IF=1)
      pushq $(GDT_USER_CODE | 3)   # CS
      pushq %rdi                   # RIP

      # Clear registers
      xorq %rax, %rax
      xorq %rbx, %rbx
      # ... clear all GPRs

      swapgs
      iretq
  ```

**Test:** Load minimal ELF that calls `exit(42)`, verify kernel receives exit code.

---

## Phase 6.5: Essential Syscalls for Real Programs

These syscalls are required before busybox or other real programs will work.

### Task 6.5.1: brk() - Heap Management
- [ ] Implement `sys_brk(unsigned long addr)` in `kernel/process.c`
  ```c
  // brk() sets the program break (end of data segment)
  // Returns: new break on success, current break on failure
  long sys_brk(unsigned long addr) {
      process_t *proc = current_process();

      // Query current break
      if (addr == 0)
          return proc->brk;

      // Validate: must be above initial brk, below stack
      if (addr < proc->brk_start || addr >= USER_STACK_BOTTOM)
          return proc->brk;  // Return current, don't fail

      uint64_t old_brk = proc->brk;
      uint64_t new_brk = ALIGN_UP(addr, PAGE_SIZE);

      if (new_brk > old_brk) {
          // Expanding: allocate and map new pages
          for (uint64_t va = ALIGN_UP(old_brk, PAGE_SIZE); va < new_brk; va += PAGE_SIZE) {
              uint64_t phys = pmm_alloc();
              if (!phys) return proc->brk;  // OOM
              vmm_map_page(proc->pml4_phys, va, phys,
                          PTE_PRESENT | PTE_WRITABLE | PTE_USER);
              memset(phys_to_virt(phys), 0, PAGE_SIZE);
          }
      } else if (new_brk < old_brk) {
          // Shrinking: unmap and free pages
          for (uint64_t va = new_brk; va < ALIGN_UP(old_brk, PAGE_SIZE); va += PAGE_SIZE) {
              uint64_t phys = vmm_get_physical(proc->pml4_phys, va);
              vmm_unmap_page(proc->pml4_phys, va);
              pmm_free(phys);
          }
      }

      proc->brk = addr;
      return addr;
  }
  ```
- [ ] Add `brk_start` and `brk` fields to `process_t`
- [ ] Initialize `brk_start` and `brk` in ELF loader (end of last PT_LOAD segment)

**Note:** glibc malloc uses mmap for large allocations, but musl and simple programs rely on brk.

---

### Task 6.5.2: mmap()/munmap() - Memory Mapping
- [ ] Create `mm/mmap.h`
  ```c
  #define MAP_SHARED      0x01
  #define MAP_PRIVATE     0x02
  #define MAP_FIXED       0x10
  #define MAP_ANONYMOUS   0x20

  #define PROT_NONE       0x0
  #define PROT_READ       0x1
  #define PROT_WRITE      0x2
  #define PROT_EXEC       0x4

  // Virtual memory area tracking
  typedef struct vma {
      uint64_t start;
      uint64_t end;
      int prot;
      int flags;
      struct vfs_file *file;    // NULL for anonymous
      uint64_t file_offset;
      struct vma *next;
  } vma_t;

  void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
  int sys_munmap(void *addr, size_t len);
  ```
- [ ] Create `mm/mmap.c`
  ```c
  // Simplified initial implementation - anonymous mappings only
  void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
      process_t *proc = current_process();

      // Only support anonymous private mappings initially
      if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE))
          return (void *)-ENOSYS;

      if (fd != -1)
          return (void *)-EBADF;

      len = ALIGN_UP(len, PAGE_SIZE);

      // Find free virtual address range (simple linear search)
      uint64_t va;
      if (flags & MAP_FIXED) {
          va = (uint64_t)addr;
          if (va & 0xFFF) return (void *)-EINVAL;  // Not page-aligned
      } else {
          va = find_free_vma(proc, len);
          if (!va) return (void *)-ENOMEM;
      }

      // Convert prot to PTE flags
      uint64_t pte_flags = PTE_PRESENT | PTE_USER;
      if (prot & PROT_WRITE) pte_flags |= PTE_WRITABLE;
      if (!(prot & PROT_EXEC)) pte_flags |= PTE_NX;

      // Allocate and map pages
      for (uint64_t v = va; v < va + len; v += PAGE_SIZE) {
          uint64_t phys = pmm_alloc();
          if (!phys) {
              // TODO: unmap already-mapped pages
              return (void *)-ENOMEM;
          }
          vmm_map_page(proc->pml4_phys, v, phys, pte_flags);
          memset(phys_to_virt(phys), 0, PAGE_SIZE);
      }

      // Add to VMA list for tracking
      vma_add(proc, va, va + len, prot, flags);

      return (void *)va;
  }

  int sys_munmap(void *addr, size_t len) {
      process_t *proc = current_process();
      uint64_t va = (uint64_t)addr;

      if (va & 0xFFF) return -EINVAL;
      len = ALIGN_UP(len, PAGE_SIZE);

      for (uint64_t v = va; v < va + len; v += PAGE_SIZE) {
          uint64_t phys = vmm_get_physical(proc->pml4_phys, v);
          if (phys) {
              vmm_unmap_page(proc->pml4_phys, v);
              pmm_free(phys);
          }
      }

      vma_remove(proc, va, va + len);
      return 0;
  }
  ```

**Future enhancements:**
- File-backed mappings (requires page cache)
- MAP_SHARED (requires page cache + writeback)
- Demand paging (map as not-present, fault in on access)

---

### Task 6.5.3: arch_prctl() - Thread Local Storage
- [ ] Implement `sys_arch_prctl(int code, unsigned long addr)` in `arch/x86/kernel/syscall.c`
  ```c
  #define ARCH_SET_GS     0x1001
  #define ARCH_SET_FS     0x1002
  #define ARCH_GET_FS     0x1003
  #define ARCH_GET_GS     0x1004

  long sys_arch_prctl(int code, unsigned long addr) {
      process_t *proc = current_process();

      switch (code) {
      case ARCH_SET_FS:
          proc->fs_base = addr;
          // Write to MSR_FS_BASE (0xC0000100)
          wrmsr(0xC0000100, addr);
          return 0;

      case ARCH_SET_GS:
          // Note: kernel uses GS, so user GS goes to MSR_KERNEL_GS_BASE
          // which gets swapped in on return to userspace
          proc->gs_base = addr;
          wrmsr(0xC0000102, addr);  // MSR_KERNEL_GS_BASE
          return 0;

      case ARCH_GET_FS:
          return copy_to_user((void *)addr, &proc->fs_base, sizeof(uint64_t))
                 ? -EFAULT : 0;

      case ARCH_GET_GS:
          return copy_to_user((void *)addr, &proc->gs_base, sizeof(uint64_t))
                 ? -EFAULT : 0;

      default:
          return -EINVAL;
      }
  }
  ```
- [ ] Add `fs_base` and `gs_base` fields to `process_t`
- [ ] Restore FS_BASE on context switch to process

**Why this matters:** C libraries use FS segment for thread-local storage. `errno` is typically accessed as `%fs:0xfffffffc`. Without arch_prctl, any program using errno will crash.

---

### Task 6.5.4: wait4()/waitpid() - Process Reaping
- [ ] Implement `sys_wait4(pid_t pid, int *wstatus, int options, struct rusage *ru)` in `kernel/process.c`
  ```c
  #define WNOHANG    0x00000001
  #define WUNTRACED  0x00000002

  // Wait status macros (for parent to decode)
  #define WEXITSTATUS(s)  (((s) & 0xff00) >> 8)
  #define WIFEXITED(s)    (((s) & 0x7f) == 0)
  #define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
  #define WTERMSIG(s)     ((s) & 0x7f)

  long sys_wait4(pid_t pid, int *wstatus, int options, void *rusage) {
      process_t *proc = current_process();
      process_t *child;

      // rusage not implemented yet
      (void)rusage;

      while (1) {
          // Find matching zombie child
          child = find_zombie_child(proc, pid);
          if (child) {
              pid_t child_pid = child->pid;
              int status = (child->exit_code & 0xff) << 8;

              if (wstatus) {
                  if (copy_to_user(wstatus, &status, sizeof(int)))
                      return -EFAULT;
              }

              // Reap the zombie
              process_destroy(child);
              return child_pid;
          }

          // Check if we have any children at all
          if (!has_children(proc, pid))
              return -ECHILD;

          // WNOHANG: return immediately if no zombie
          if (options & WNOHANG)
              return 0;

          // Sleep until SIGCHLD wakes us
          proc->state = PROC_SLEEPING;
          proc->wait_pid = pid;
          schedule();

          // Check for signals that should interrupt wait
          if (signal_pending(proc))
              return -EINTR;
      }
  }

  // Helper: find zombie child matching pid
  // pid == -1: any child
  // pid > 0: specific child
  // pid == 0: any child in same process group (not implemented)
  process_t *find_zombie_child(process_t *parent, pid_t pid);
  ```
- [ ] Modify `sys_exit()` to wake parent if waiting
- [ ] Implement `sys_waitpid()` as wrapper: `return sys_wait4(pid, wstatus, options, NULL);`

**Test:** Fork, child exits, parent waits and receives exit code.

---

### Task 6.5.5: getpid()/getppid()/getuid()/getgid() - Identity Syscalls
- [ ] Implement trivial identity syscalls in `kernel/process.c`
  ```c
  long sys_getpid(void)  { return current_process()->pid; }
  long sys_getppid(void) {
      process_t *parent = current_process()->parent;
      return parent ? parent->pid : 0;
  }
  long sys_getuid(void)  { return 0; }  // Always root for now
  long sys_geteuid(void) { return 0; }
  long sys_getgid(void)  { return 0; }
  long sys_getegid(void) { return 0; }
  ```

---

### Task 6.5.6: uname() - System Information
- [ ] Implement `sys_uname(struct utsname *buf)` in `kernel/process.c`
  ```c
  struct utsname {
      char sysname[65];
      char nodename[65];
      char release[65];
      char version[65];
      char machine[65];
  };

  long sys_uname(struct utsname *buf) {
      struct utsname info = {
          .sysname = "SeedOS",
          .nodename = "seed",
          .release = "0.1.0",
          .version = "#1 SMP",
          .machine = "x86_64"
      };
      return copy_to_user(buf, &info, sizeof(info)) ? -EFAULT : 0;
  }
  ```

**Test:** After implementing these syscalls, basic dynamically-linked programs should get further (though will still need more syscalls).

---

## Phase 7: fork() with Copy-on-Write

### Task 7.1: Page Reference Counting
- [ ] Create `mm/cow.h`
  ```c
  void cow_init(void);
  void cow_ref_inc(uint64_t phys_addr);
  void cow_ref_dec(uint64_t phys_addr);
  uint32_t cow_ref_get(uint64_t phys_addr);
  ```
- [ ] Create `mm/cow.c`
  - Hash table or array mapping physical page → refcount
  - Initialize all mapped pages with refcount 1

---

### Task 7.2: CoW Page Table Copy
- [ ] Modify `mm/vmm.h`
  - Add `#define PTE_COW (1ULL << 9)` (software-defined bit)
  - Add `uint64_t vmm_copy_address_space_cow(uint64_t src_pml4)`
- [ ] Modify `mm/vmm.c`
  - `vmm_copy_address_space_cow()`:
    1. Create new PML4
    2. Walk all user-space PTEs in source
    3. For each present page:
       - Clear PTE_WRITABLE, set PTE_COW in BOTH source and dest
       - Map same physical page in dest
       - Increment reference count
    4. Flush TLB
    5. Return new PML4

---

### Task 7.3: CoW Page Fault Handler
- [ ] Modify `arch/x86/kernel/idt.c` - page fault handler
  ```c
  void page_fault_handler(interrupt_frame_t *frame) {
      uint64_t fault_addr;
      __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

      uint64_t error = frame->error_code;

      // Write fault on COW page?
      if ((error & PF_WRITE) && is_cow_page(fault_addr)) {
          uint64_t old_phys = vmm_get_physical(current->pml4_phys, fault_addr);

          if (cow_ref_get(old_phys) == 1) {
              // Only owner - just make writable
              vmm_set_flags(fault_addr, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
          } else {
              // Copy page
              uint64_t new_phys = pmm_alloc();
              memcpy(phys_to_virt(new_phys), phys_to_virt(old_phys), 4096);
              vmm_remap(fault_addr, new_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
              cow_ref_dec(old_phys);
          }
          return;
      }

      // Not COW - deliver SIGSEGV or panic
      if (is_user_mode(frame)) {
          signal_send(current, SIGSEGV);
      } else {
          panic("Page fault in kernel");
      }
  }
  ```

---

### Task 7.4: fork() Syscall
- [ ] Implement `sys_fork()` in `kernel/process.c`
  1. Create child process
  2. Copy parent's name + " (child)"
  3. Copy file descriptor table (increment refcounts)
  4. Copy address space with CoW
  5. Copy user register state
  6. Set child's RAX to 0 (return value)
  7. Add child to scheduler
  8. Return child PID to parent

**Test:** Fork test program, verify both parent and child print messages.

---

## Phase 8: execve()

### Task 8.1: execve() Implementation
- [ ] Implement `sys_execve(path, argv, envp)` in `kernel/process.c`
  1. Validate and copy strings from userspace
  2. Look up file in ext2
  3. Read ELF data
  4. Free old user address space (keep kernel mappings)
  5. Create new address space
  6. Load ELF segments
  7. Set up user stack with argc/argv/envp/auxv
  8. Update process name
  9. Reset signal handlers to default
  10. Close FDs marked FD_CLOEXEC
  11. Jump to entry point (never returns)

---

### Task 8.2: String Copying Helpers
- [ ] Add to `kernel/process.c`
  ```c
  char *copy_string_from_user(const char *user_str);
  char **copy_string_array_from_user(char *const user_arr[]);
  void free_string_array(char **arr);
  ```
  - Validate pointers with `vmm_validate_user_range()`
  - Copy strings to kernel heap

**Test:** Fork, then exec `/bin/hello`, verify it runs.

---

## Phase 9: Basic Signals

### Task 9.1: Signal Infrastructure
- [ ] Create `kernel/signal.h`
  ```c
  #define SIGHUP    1
  #define SIGINT    2
  #define SIGKILL   9
  #define SIGSEGV   11
  #define SIGCHLD   17
  #define SIGSTOP   19

  #define SIG_DFL   ((void (*)(int))0)
  #define SIG_IGN   ((void (*)(int))1)

  typedef struct {
      void (*handler)(int);
      uint64_t sa_mask;
      int sa_flags;
  } sigaction_t;

  void signal_init(process_t *proc);
  void signal_send(process_t *proc, int sig);
  void signal_check_pending(void);  // Before return to userspace
  int sys_kill(pid_t pid, int sig);
  int sys_sigaction(int sig, const sigaction_t *act, sigaction_t *oldact);
  ```

---

### Task 9.2: Signal Delivery
- [ ] Create `kernel/signal.c`
  - `signal_send()` - set bit in pending_signals
  - `signal_check_pending()` - called before sysret/iret
    - For SIGKILL: terminate immediately
    - For SIGSEGV with default handler: terminate
    - For custom handler: set up signal frame on user stack

---

### Task 9.3: Signal Frame Setup

**Signal delivery requires:**
1. A signal frame on the user stack containing saved state
2. A trampoline that calls `sys_sigreturn` after the handler returns
3. Careful manipulation of user registers to redirect execution

- [ ] Create `kernel/signal_frame.h`
  ```c
  // Signal frame pushed onto user stack (Linux-compatible layout)
  typedef struct {
      // Return address points to trampoline
      uint64_t pretcode;

      // ucontext for sigreturn
      uint64_t uc_flags;
      uint64_t uc_link;
      // stack_t uc_stack (24 bytes)
      uint64_t ss_sp;
      uint32_t ss_flags;
      uint32_t ss_size_pad;
      uint64_t ss_size;

      // Saved registers (mcontext)
      uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
      uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp;
      uint64_t rip, eflags;
      uint16_t cs, gs, fs, ss;

      uint64_t err;
      uint64_t trapno;
      uint64_t oldmask;
      uint64_t cr2;

      // FPU state (if used)
      fpu_state_t *fpstate;

      // Signal info
      int signo;
  } __attribute__((packed)) signal_frame_t;
  ```

- [ ] Create signal trampoline page
  ```c
  // Option 1: Dedicated page mapped into every process (like Linux vDSO)
  #define SIGRETURN_TRAMPOLINE  0x7FFFFFFFF000ULL  // Just below user stack

  // Trampoline code (15 bytes):
  // mov $15, %rax    ; SYS_rt_sigreturn
  // syscall
  // (unreachable)
  static const uint8_t sigreturn_trampoline[] = {
      0x48, 0xc7, 0xc0, 0x0f, 0x00, 0x00, 0x00,  // mov $15, %rax
      0x0f, 0x05,                                  // syscall
      0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90   // nop padding
  };

  void signal_init_trampoline(uint64_t pml4) {
      uint64_t phys = pmm_alloc();
      memcpy(phys_to_virt(phys), sigreturn_trampoline, sizeof(sigreturn_trampoline));
      vmm_map_page(pml4, SIGRETURN_TRAMPOLINE, phys,
                   PTE_PRESENT | PTE_USER);  // Read+Execute only, no write
  }
  ```

- [ ] Implement `setup_signal_frame()` in `kernel/signal.c`
  ```c
  void setup_signal_frame(process_t *proc, int signo, sigaction_t *sa) {
      // 1. Calculate frame location (grow stack down)
      uint64_t frame_addr = (proc->user_rsp - sizeof(signal_frame_t)) & ~0xF;

      // 2. Validate we have stack space
      if (!vmm_validate_user_range(proc->pml4_phys, frame_addr, sizeof(signal_frame_t)))
          goto force_kill;

      // 3. Build frame
      signal_frame_t frame = {
          .pretcode = SIGRETURN_TRAMPOLINE,
          .rip = proc->user_rip,
          .rsp = proc->user_rsp,
          .rax = proc->user_rax,
          // ... save all registers
          .eflags = proc->user_rflags,
          .signo = signo,
      };

      // 4. Copy frame to user stack
      if (copy_to_user((void *)frame_addr, &frame, sizeof(frame)))
          goto force_kill;

      // 5. Set up handler invocation
      proc->user_rsp = frame_addr;
      proc->user_rip = (uint64_t)sa->handler;
      proc->user_rdi = signo;  // First argument to handler

      // 6. Block signals in sa_mask during handler
      proc->blocked_signals |= sa->sa_mask;

      return;

  force_kill:
      // Can't deliver signal - kill process
      proc->exit_code = 128 + signo;
      proc->state = PROC_ZOMBIE;
      schedule();
  }
  ```

- [ ] Implement `sys_rt_sigreturn()` (syscall 15)
  ```c
  long sys_rt_sigreturn(void) {
      process_t *proc = current_process();

      // Frame is at current RSP (handler returned to trampoline which called us)
      signal_frame_t frame;
      if (copy_from_user(&frame, (void *)proc->user_rsp, sizeof(frame)))
          return -EFAULT;

      // Restore all registers from frame
      proc->user_rip = frame.rip;
      proc->user_rsp = frame.rsp;
      proc->user_rax = frame.rax;
      proc->user_rflags = frame.eflags;
      // ... restore all registers

      // Restore signal mask
      proc->blocked_signals = frame.oldmask;

      // Return to restored context (special: don't return to RAX, return to saved RIP)
      // This requires special handling in syscall return path
      return 0;  // RAX gets restored from frame, not this return value
  }
  ```

**Signal delivery flow:**
```
1. Kernel detects pending signal before returning to userspace
2. Kernel saves current user state to signal_frame on user stack
3. Kernel modifies user RIP → handler, RSP → frame, RDI → signo
4. Return to userspace (iret/sysret)
5. User handler executes
6. Handler returns to pretcode (trampoline)
7. Trampoline calls sys_rt_sigreturn
8. Kernel restores original state from frame
9. Return to original user code location
```

**Test:** Install SIGUSR1 handler, send signal, verify handler runs and original code resumes.

---

## Verification Milestones

### Milestone A: Ring 3 Entry (After Phase 1-2)
Create inline assembly syscall test:
```c
// In kernel, before enabling user mode
uint64_t pid;
__asm__ volatile("mov $39, %%rax; syscall; mov %%rax, %0" : "=r"(pid));
kprintf("PID from syscall: %lu\n", pid);
```

### Milestone B: First User Program (After Phase 6)
```c
// exit_test.c - compile with: gcc -nostdlib -static -o init exit_test.c
void _start(void) {
    __asm__ volatile("mov $60, %%rax; mov $42, %%rdi; syscall" :::);
}
```
Expected: "Process 1 exited with code 42"

### Milestone B.5: Write Syscall Test
Validates user→kernel string copy before attempting hello world:
```c
// write_test.c - compile with: gcc -nostdlib -static -o init write_test.c
void _start(void) {
    const char msg[] = "test\n";
    __asm__ volatile(
        "mov $1, %%rax;"      // SYS_write
        "mov $1, %%rdi;"      // fd = stdout
        "mov %0, %%rsi;"      // buf
        "mov $5, %%rdx;"      // count
        "syscall"
        :: "r"(msg) : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    __asm__ volatile("mov $60, %%rax; mov $0, %%rdi; syscall" :::);
}
```
Expected: "test" printed to console, process exits with code 0.

**Debug tip:** If nothing prints, add kernel debug output in `sys_write()` to verify:
- fd is valid (should be 1)
- buf pointer is in user space
- count is reasonable
- `copy_from_user()` succeeds

### Milestone C: Hello World (After Phase 6)
```c
void _start(void) {
    const char *msg = "Hello from userspace!\n";
    __asm__ volatile(
        "mov $1, %%rax;"
        "mov $1, %%rdi;"
        "mov %0, %%rsi;"
        "mov $22, %%rdx;"
        "syscall"
        :: "r"(msg) : "rax", "rdi", "rsi", "rdx"
    );
    __asm__ volatile("mov $60, %%rax; xor %%rdi, %%rdi; syscall" :::);
}
```

### Milestone D: Fork Test (After Phase 7)
```c
void _start(void) {
    long pid;
    __asm__ volatile("mov $57, %%rax; syscall; mov %%rax, %0" : "=r"(pid));

    if (pid == 0) {
        // Child
        write(1, "Child!\n", 7);
    } else {
        // Parent
        write(1, "Parent!\n", 8);
    }
    __asm__ volatile("mov $60, %%rax; xor %%rdi, %%rdi; syscall" :::);
}
```

### Milestone E: Static Busybox
```bash
# Create ext2 image
dd if=/dev/zero of=initrd.ext2 bs=1M count=16
mke2fs initrd.ext2
mkdir /tmp/mnt && sudo mount initrd.ext2 /tmp/mnt
sudo cp /bin/busybox-static /tmp/mnt/init
sudo umount /tmp/mnt
```

Additional syscalls needed for busybox:
- `brk` (12) - heap allocation
- `mmap` (9) - memory mapping
- `arch_prctl` (158) - TLS setup

---

## File Checklist

### New Files
- [ ] `arch/x86/kernel/gdt.h`
- [ ] `arch/x86/kernel/gdt.c`
- [ ] `arch/x86/kernel/gdt.S`
- [ ] `arch/x86/kernel/percpu.h`
- [ ] `arch/x86/kernel/percpu.c`
- [ ] `arch/x86/kernel/fpu.h`
- [ ] `arch/x86/kernel/fpu.c`
- [ ] `arch/x86/kernel/syscall.h`
- [ ] `arch/x86/kernel/syscall.c`
- [ ] `arch/x86/kernel/syscall_entry.S`
- [ ] `arch/x86/kernel/usermode.S`
- [ ] `kernel/process.h`
- [ ] `kernel/process.c`
- [ ] `kernel/syscall_table.h`
- [ ] `kernel/elf.h`
- [ ] `kernel/elf.c`
- [ ] `kernel/signal.h`
- [ ] `kernel/signal.c`
- [ ] `kernel/signal_frame.h`
- [ ] `fs/vfs.h`
- [ ] `fs/vfs.c`
- [ ] `fs/ext2.h`
- [ ] `fs/ext2.c`
- [ ] `mm/cow.h`
- [ ] `mm/cow.c`
- [ ] `mm/mmap.h`
- [ ] `mm/mmap.c`
- [ ] `drivers/tty/tty_dev.c`

### Modified Files
- [ ] `arch/x86/kernel/idt.c` - GDT selector, page fault handler
- [ ] `arch/x86/kernel/idt.h` - Remove Limine GDT constant
- [ ] `arch/x86/boot/limine.c` - Module request
- [ ] `arch/x86/boot/limine.conf` - Module path
- [ ] `kernel/kthread.c` - Process integration
- [ ] `kernel/kthread.h` - Process back-pointer
- [ ] `mm/vmm.h` - PTE_COW, CoW functions
- [ ] `mm/vmm.c` - CoW implementation
- [ ] `init/main.c` - New init calls
- [ ] `Makefile` - Add fs/ directory
