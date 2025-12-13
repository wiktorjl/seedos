# Seed OS Roadmap: Path to Lua in Userspace

This roadmap consolidates recommendations from Claude Opus 4.5, Qwen, GPT-5.1-Codex-Max, and Gemini, prioritized for the goal of porting Lua to userspace. Bug fixes that could destabilize larger development efforts are prioritized first.

---

## Critical Path to Lua

Lua requires:
1. **Working `malloc`/`free`** - needs complete `sys_sbrk`
2. **Standard C library** - printf, sprintf, string functions, memory functions
3. **File I/O** - at minimum stdin for REPL (`sys_read`)
4. **Stable kernel** - no memory leaks or page faults during development

---

## Phase 1: Bug Fixes (BLOCKING - Do First)

These bugs will cause crashes, memory corruption, or instability during larger feature development.

### 1.1 Integer Overflow in User Pointer Validation

**Severity:** HIGH - Security vulnerability, potential kernel crash

**Location:** `src/syscall.c:87`

**Current Code:**
```c
if (buffer >= 0x0000800000000000ULL || buffer + count >= 0x0000800000000000ULL) {
```

**The Bug:** The expression `buffer + count` can overflow. If a malicious or buggy userspace program passes:
- `buffer = 0xFFFFFFFFFFFFFF00`
- `count = 0x200`

Then `buffer + count = 0x100` (wraps around), which is less than `0x0000800000000000ULL`, so the check passes! The kernel then tries to read from kernel memory at `0xFFFFFFFFFFFFFF00`, causing either a page fault or leaking kernel data.

**Fix:**
```c
// Check for overflow BEFORE adding
if (buffer >= 0x0000800000000000ULL ||
    count > 0x0000800000000000ULL - buffer) {
    // Invalid pointer
}
```

Or use a helper function:
```c
bool vmm_validate_user_range(const void *ptr, size_t len) {
    uint64_t addr = (uint64_t)ptr;
    // Check pointer is in user space
    if (addr >= USER_SPACE_TOP) return false;
    // Check end doesn't overflow
    if (len > USER_SPACE_TOP - addr) return false;
    return true;
}
```

**Files:** `src/syscall.c`, `src/vmm.c`, `src/vmm.h`, `src/memory.h` (add `USER_SPACE_TOP` constant)

---

### 1.2 Writable Data in .text Section (Page Fault Bug)

**Severity:** HIGH - Causes page faults in userspace programs

**Location:** `src/userspace/count.s:34-37`

**The Bug:** In `count.s`, the `digit:` label is placed in the default `.text` section (no `.section` directive). The code writes to it:

```assembly
# Line 10:
mov %al, digit(%rip)  # WRITE to digit buffer

# Lines 34-37 (in .text section by default):
digit:
    .byte 0
newline:
    .ascii "\n"
```

When the ELF loader loads this program:
1. `.text` segment has `PF_R | PF_X` (readable, executable) but NOT `PF_W`
2. ELF loader maps it with `PTE_PRESENT | PTE_USER` (no `PTE_WRITABLE`)
3. When the program executes `mov %al, digit(%rip)`, it writes to a read-only page
4. CPU raises page fault (#PF)

**Why it might work today:** The current `process_create()` pre-allocates a code page at `USER_CODE_BASE` with `PTE_WRITABLE` before loading the ELF. If the ELF is small enough to fit in this pre-mapped page, writes succeed. But this is fragile - larger programs or different memory layouts will crash.

**The Same Bug Exists In:**
- `hello.s` - `message:` in .text (read-only, happens to work)
- `heap.s` - strings in .text (read-only, happens to work)
- `count.s` - `digit:` written to, **will crash in strict ELF loading**

**Correctly Structured:** `info.s` uses `.section .rodata` and `.section .data` properly.

**Fix:** Update all userspace programs:
```assembly
.section .rodata
message:
    .ascii "Hello\n"

.section .data
buffer:
    .byte 0
```

**Files:** `src/userspace/count.s`, `src/userspace/hello.s`, `src/userspace/heap.s`, `src/userspace/alpha.s`, `src/userspace/stars.s`, `src/userspace/loop.s`

---

### 1.3 Memory Leak in process_destroy()

**Severity:** MEDIUM - System runs out of memory after running many programs

**Location:** `src/process.c:167-189`

**The Bug:** When a process exits, `process_destroy()` only frees three pages:
```c
pmm_free(p->stack_page);  // 1 page
pmm_free(p->code_page);   // 1 page
pmm_free(p->pml4);        // 1 page (just the PML4 itself!)
```

**What's NOT freed:**
1. **Page table hierarchy** - PML4 points to up to 512 PDPTs, each PDPT points to up to 512 PDs, each PD points to up to 512 PTs. Each of these is a 4KB physical page. After running 100 small programs, you've leaked ~300+ pages minimum.

2. **ELF-loaded pages** - `elf_load()` allocates pages for each PT_LOAD segment. These are never freed.

3. **Heap pages** - `process_sbrk()` allocates pages but doesn't track them. On process exit, they leak.

**Impact:** Run the shell, execute programs repeatedly, eventually `pmm_alloc()` returns 0 and the system stops working.

**Fix Strategy:**
1. **Quick fix (page table walk):** On destroy, walk PML4 -> PDPT -> PD -> PT, freeing all user-space page tables and the physical pages they point to.
2. **Better fix:** Track all allocated pages in a list per-process, free on exit.

**Files:** `src/process.c`, `src/vmm.c` (add `vmm_free_user_address_space()`)

---

## Phase 2: Syscall Hardening

### 2.1 Centralized User Pointer Validation

**Why:** Every syscall that takes a user pointer needs the same validation. Without it, adding new syscalls risks kernel crashes.

**Implementation:**
```c
// src/vmm.h
#define USER_SPACE_TOP 0x0000800000000000ULL

bool vmm_validate_user_range(const void *ptr, size_t len);

// src/vmm.c
bool vmm_validate_user_range(const void *ptr, size_t len) {
    uint64_t addr = (uint64_t)ptr;
    if (addr >= USER_SPACE_TOP) return false;
    if (len > USER_SPACE_TOP - addr) return false;
    // Optional: walk page tables to verify pages are mapped
    return true;
}
```

**Apply to:** `sys_write`, future `sys_read`, `sys_sbrk` (validate increment doesn't cause wrap)

**Files:** `src/vmm.c`, `src/vmm.h`, `src/memory.h`, `src/syscall.c`

---

### 2.2 Implement sys_read for Keyboard Input

**Why:** Lua REPL needs to read user input. Without `sys_read(0, buf, n)`, no interactive programs.

**Current State:** `SYS_READ = 2` is defined in `syscall.h` but not implemented.

**Implementation:**
1. Add circular buffer to `keyboard.c` that accumulates keypresses
2. Implement `keyboard_read(char *buf, size_t count)` - returns available bytes or blocks
3. Wire `sys_read` in `syscall.c`:
   - If `fd == 0` (stdin): validate buffer, call `keyboard_read`
   - Return bytes read or -1 on error

**Blocking Behavior Options:**
- **Simple (spin-wait):** Loop until bytes available. Wastes CPU but simple.
- **Better (yield):** Mark process BLOCKED, wake when keyboard has data.

**Files:** `src/keyboard.c`, `src/keyboard.h`, `src/syscall.c`

---

## Phase 3: Complete Heap Management

### 3.1 Full sys_sbrk Implementation

**Current State:** Growing works, shrinking doesn't, no tracking.

**Required for Lua:** `malloc()` needs reliable `sbrk()`. `free()` may call `sbrk(-n)` to return memory.

**Enhancements:**
1. **Support shrinking:** Unmap and free pages when `new_brk < old_brk`
2. **Track heap pages:** Store list of allocated pages in process struct for cleanup
3. **Add heap limit:** Prevent unlimited growth (e.g., `heap_max = USER_STACK_BASE - gap`)

```c
// In struct process:
uint64_t heap_start;    // = USER_HEAP_BASE
uint64_t heap_current;  // = brk
uint64_t heap_max;      // Limit to prevent collision with stack

// Track pages for cleanup (simple approach):
uint64_t heap_pages[MAX_HEAP_PAGES];
int heap_page_count;
```

**Files:** `src/process.h`, `src/process.c`, `src/syscall.c`

---

## Phase 4: Userspace C Library

**This is the critical enabler for Lua.**

### 4.1 Syscall Wrappers

Create thin wrappers so C code can call syscalls without inline assembly.

**Files to Create:** `src/userspace/libc/syscall.S`

```assembly
.global _syscall_write
_syscall_write:
    mov $1, %rax        # SYS_WRITE
    # rdi, rsi, rdx already have args from C ABI
    int $0x80
    ret

.global _syscall_exit
_syscall_exit:
    mov $0, %rax        # SYS_EXIT
    int $0x80
    # no ret - exit doesn't return

.global _syscall_sbrk
_syscall_sbrk:
    mov $5, %rax        # SYS_SBRK
    int $0x80
    ret

.global _syscall_read
_syscall_read:
    mov $2, %rax        # SYS_READ
    int $0x80
    ret
```

### 4.2 Memory Functions

**Required for Lua:** `malloc`, `free`, `realloc`, `memcpy`, `memset`, `memmove`

**malloc/free Strategy:**
- Simple bump allocator initially (malloc only grows, free is no-op)
- Or: simple free-list allocator

```c
// src/userspace/libc/stdlib.c
static char *heap_ptr = NULL;

void *malloc(size_t size) {
    if (heap_ptr == NULL) {
        heap_ptr = sbrk(0);  // Get current brk
    }
    void *ptr = sbrk(size);
    if (ptr == (void*)-1) return NULL;
    return ptr;
}

void free(void *ptr) {
    // No-op for bump allocator
    // Or: add to free list
}
```

### 4.3 String Functions

**Required for Lua:** `strlen`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `memcmp`, `strchr`, `strstr`

```c
// src/userspace/libc/string.c
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}
// ... etc
```

### 4.4 I/O Functions

**Required for Lua:** `printf`, `sprintf`, `snprintf`, `puts`, `putchar`, `getchar`, `fgets`

```c
// src/userspace/libc/stdio.c
int putchar(int c) {
    char ch = c;
    _syscall_write(1, &ch, 1);
    return c;
}

int puts(const char *s) {
    _syscall_write(1, s, strlen(s));
    _syscall_write(1, "\n", 1);
    return 0;
}

// printf via simple format parser
int printf(const char *fmt, ...);
```

### 4.5 Startup Code

```assembly
// src/userspace/libc/crt0.S
.global _start
_start:
    // Clear frame pointer for stack traces
    xor %rbp, %rbp

    // Call main(argc, argv) - for now just main()
    call main

    // Exit with return value from main
    mov %eax, %edi
    mov $0, %rax      # SYS_EXIT
    int $0x80
```

### 4.6 Build System Integration

Update Makefile:
- Compile libc sources to `libc.a`
- Link userspace C programs with libc.a and crt0.o
- Use `user.ld` linker script

**Files to Create:**
- `src/userspace/libc/syscall.S`
- `src/userspace/libc/stdlib.c` (malloc, free)
- `src/userspace/libc/string.c`
- `src/userspace/libc/stdio.c`
- `src/userspace/libc/crt0.S`
- `src/userspace/libc/unistd.h`, `stdlib.h`, `string.h`, `stdio.h`

---

## Phase 5: Port Lua

With libc in place:

1. **Get Lua source** - Use Lua 5.4 or LuaJIT
2. **Configure for freestanding** - Disable filesystem, use our libc
3. **Implement stubs** - Lua needs: malloc, free, realloc, printf, sprintf, strlen, memcpy, memset, setjmp/longjmp
4. **Build as ELF** - Link with userspace libc
5. **Test** - Run simple Lua scripts: `print("Hello")`, arithmetic, functions

**Lua-specific requirements:**
- `setjmp`/`longjmp` for error handling (can be stubbed initially)
- Floating point (x87/SSE should work)
- `time()` - can stub or implement via uptime syscall

---

## Phase 6: Nice-to-Have Improvements

### 6.1 Multi-Process Support

Not required for Lua but useful:
- Process table with MAX_PROCESSES slots
- Per-process state (READY, RUNNING, BLOCKED)
- Basic scheduler (round-robin)

### 6.2 Timer Preemption

- Hook PIT to call scheduler
- Time-slice processes

### 6.3 Kernel Panic Handler

Better debugging:
- Print register dump on crash
- Print CR2 on page faults
- Stack trace via frame pointers

### 6.4 VFS Layer

For loading Lua scripts from "files":
- Ramdisk with embedded files
- Simple tar parser
- `open()`, `read()`, `close()` syscalls

---

## Recommended Execution Order

| Order | Task | Effort | Blocks |
|-------|------|--------|--------|
| 1 | Fix integer overflow in pointer validation | Small | Security |
| 2 | Fix writable data in .text (count.s etc) | Small | Stability |
| 3 | Fix memory leak in process_destroy | Medium | Long-term stability |
| 4 | Centralize pointer validation helper | Small | Future syscalls |
| 5 | Implement sys_read | Medium | Lua REPL |
| 6 | Complete sbrk (shrink support) | Medium | malloc/free |
| 7 | Create userspace libc (syscall wrappers) | Medium | Lua |
| 8 | Create userspace libc (malloc/string/stdio) | Large | Lua |
| 9 | Port Lua | Large | Goal |
| 10 | Process scheduler | Large | Not required |

---

## Summary

**Must fix before major development:**
1. Integer overflow in user pointer check (security hole)
2. Writable data in .text (latent page fault)
3. Memory leak in process_destroy (will exhaust memory)

**Must implement for Lua:**
4. sys_read for keyboard input
5. Complete sbrk with shrink support
6. Userspace C library (malloc, printf, string functions)

**Nice to have:**
7. Multi-process support
8. Better debugging (panic handler)
9. VFS for loading scripts

---

## Cross-Reference: Source Document Agreement

| Feature | Opus | Qwen | GPT51 | Gemini |
|---------|------|------|-------|--------|
| User pointer validation | #2 | - | #1 | #8 |
| Fix .text data issue | #1 | - | #3 | - |
| Memory leak fix | - | - | - | #2 |
| sys_sbrk complete | #4 | #3 | #4 | #5 |
| sys_read | #5 | #3 | - | #5 |
| Userspace libc | #3 | #6 | #2 | #4 |
| Process scheduler | #6-8 | #1-2 | #6-7 | #1 |
| ELF loader | #9 | - | #8 | - |
| Panic handler | #10 | #8 | - | - |
