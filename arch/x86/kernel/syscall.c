// SPDX-License-Identifier: GPL-2.0-only
/*
 * Syscall/Sysret MSR Configuration and Dispatch
 *
 * Sets up the MSRs that control fast syscall/sysret instructions
 * and dispatches syscalls to their handlers.
 */

#include "syscall.h"
#include "syscall_table.h"
#include "process.h"
#include "percpu.h"
#include "gdt.h"
#include "log.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"
#include "memory.h"
#include "heap.h"
#include "ext2.h"
#include "page.h"

/*
 * Local string helper for syscall implementations
 */
static void syscall_strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void syscall_memset(void *dst, uint8_t val, size_t n)
{
    uint8_t *d = dst;
    for (size_t i = 0; i < n; i++)
        d[i] = val;
}

/*
 * canonical_addr - true iff @a is a canonical x86-64 virtual address
 *
 * Bits 63:48 must sign-extend bit 47. Non-canonical addresses passed
 * to wrmsr (FS_BASE / KERNEL_GS_BASE) or to sysretq (RCX / RSP) raise
 * #GP, and that #GP is delivered in kernel mode with user state - the
 * classic CVE-2012-0217 escalation primitive. Always validate before
 * letting user-supplied values reach those instructions.
 */
static inline bool canonical_addr(uint64_t a)
{
    return ((int64_t)(a << 16) >> 16) == (int64_t)a;
}

/*
 * Bits user is allowed to control in RFLAGS on return to CPL=3.
 *
 * Allowed: arithmetic flags (CF/PF/AF/ZF/SF/OF), IF, DF.
 * Forced:  IF=1, reserved bit 1.
 *
 * Cleared (despite sysretq's own mask passing them through):
 *   IOPL  - leaving IOPL=3 would grant unrestricted I/O port access
 *   NT    - nested-task; only relevant to legacy task switching
 *   RF    - resume flag; intersects with our (absent) debug-exception path
 *   VM    - V8086 mode; long mode kernel rejects it but be explicit
 *   AC    - alignment-check; not wired through #AC handling
 *   TF    - trap flag; would single-step into the kernel on next syscall
 *   ID    - cpuid availability bit; not user-meaningful
 *   VIF/VIP - virtual interrupts; not implemented
 */
#define USER_RFLAGS_ALLOWED 0x0000000000000ED5ULL
#define USER_RFLAGS_REQUIRED 0x0000000000000202ULL

static inline uint64_t sanitize_user_rflags(uint64_t rf)
{
    return (rf & USER_RFLAGS_ALLOWED) | USER_RFLAGS_REQUIRED;
}

/*
 * Copy a NUL-terminated string from user space into @dst.
 *
 * Validates that every byte read stays inside the user half. We don't
 * have an exception-based copy_from_user with a fixup table yet, so an
 * unmapped page inside the user half will still page-fault the kernel
 * - this guards only against out-of-range pointers, not unmapped ones.
 *
 * Return: length excluding NUL on success, negative errno otherwise.
 */
static int64_t copy_string_from_user(char *dst, const void *src, size_t max)
{
    uint64_t addr = (uint64_t)src;
    size_t safe_max;
    const char *user = src;

    if (max == 0)
        return -EINVAL;
    if (!vmm_validate_user_range(src, 1))
        return -EFAULT;

    /* The user half is [0, USER_SPACE_TOP); cap the walk there. */
    safe_max = (size_t)(USER_SPACE_TOP - addr);
    if (safe_max > max)
        safe_max = max;

    for (size_t i = 0; i < safe_max; i++) {
        dst[i] = user[i];
        if (dst[i] == '\0')
            return (int64_t)i;
    }

    /* If we hit USER_SPACE_TOP before max, the string ran off user space. */
    if (safe_max < max)
        return -EFAULT;

    return -ENAMETOOLONG;
}

/*
 * arch_prctl codes
 */
#define ARCH_SET_GS     0x1001
#define ARCH_SET_FS     0x1002
#define ARCH_GET_FS     0x1003
#define ARCH_GET_GS     0x1004

/* IA32_FS_BASE - loaded directly into FS_BASE (no swapgs involved). */
#define MSR_FS_BASE     0xC0000100

/*
 * mmap protection flags
 */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

/*
 * mmap mapping flags
 */
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS

/*
 * wait4 options
 */
#define WNOHANG         0x00000001  /* Don't block if no child has exited */
#define WUNTRACED       0x00000002  /* Also report stopped children */

/*
 * Wait status macros - encode exit status
 * Linux encodes: bits 7:0 = signal (0 if exited normally), bits 15:8 = exit code
 */
#define W_EXITCODE(code)    (((code) & 0xff) << 8)

/*
 * utsname structure (Linux-compatible)
 */
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

/*
 * Syscall dispatch table
 */
syscall_fn_t syscall_table[NR_SYSCALLS];

/*
 * Forward declarations for syscall handlers
 */
static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_exit(uint64_t status, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_getpid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_brk(uint64_t brk, uint64_t arg2, uint64_t arg3,
                       uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset);
static int64_t sys_munmap(uint64_t addr, uint64_t len, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_arch_prctl(uint64_t code, uint64_t addr, uint64_t arg3,
                              uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_getppid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_uname(uint64_t buf, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_wait4(uint64_t pid, uint64_t wstatus, uint64_t options,
                         uint64_t rusage, uint64_t arg5, uint64_t arg6);
static int64_t sys_fork(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_open(uint64_t pathname, uint64_t flags, uint64_t mode,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int64_t sys_close(uint64_t fd, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6);

/*
 * Default handler for unimplemented syscalls
 */
static int64_t sys_ni_syscall(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                              uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;
    return -ENOSYS;
}

void syscall_init(void)
{
    uint64_t efer, star;

    /*
     * Enable syscall/sysret in EFER
     *
     * EFER (Extended Feature Enable Register) controls CPU features.
     * We need to set SCE (System Call Extensions) to enable syscall.
     */
    efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    /*
     * Configure STAR (Segment Selector Register)
     *
     * STAR layout:
     *   Bits 63:48 = SYSRET CS/SS base
     *   Bits 47:32 = SYSCALL CS/SS base
     *   Bits 31:0  = Reserved (must be 0)
     *
     * On SYSCALL:
     *   CS = STAR[47:32]     = 0x08 (GDT_KERNEL_CODE)
     *   SS = STAR[47:32] + 8 = 0x10 (GDT_KERNEL_DATA)
     *
     * On SYSRET:
     *   SS = STAR[63:48] + 8  = 0x18 (GDT_USER_DATA)
     *   CS = STAR[63:48] + 16 = 0x20 (GDT_USER_CODE)
     *
     * Note: SYSRET's weird +8/+16 is why User Data must come before
     * User Code in the GDT!
     */
    star = ((uint64_t)GDT_KERNEL_DATA << 48) |  /* SYSRET base: 0x10 */
           ((uint64_t)GDT_KERNEL_CODE << 32);   /* SYSCALL base: 0x08 */
    wrmsr(MSR_STAR, star);

    /*
     * Set LSTAR (Long mode Syscall Target Address)
     *
     * This is where RIP jumps when user executes syscall.
     * Points to our assembly entry stub that handles register
     * saving and stack switching.
     */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /*
     * Set SFMASK (Syscall Flag Mask)
     *
     * RFLAGS bits to CLEAR on syscall entry. We clear:
     *   IF (bit 9)  - Disable interrupts until we set up kernel stack
     *   DF (bit 10) - Clear direction flag (string ops go forward)
     *   TF (bit 8)  - Clear trap flag (no single-stepping in kernel)
     *   AC (bit 18) - Clear alignment check
     *
     * Interrupts are re-enabled after we've saved state and have
     * a valid kernel stack.
     */
    wrmsr(MSR_SFMASK, RFLAGS_IF | RFLAGS_DF | RFLAGS_TF | RFLAGS_AC);

    log_debug("SYSCALL: EFER=0x%llx (SCE enabled)", rdmsr(MSR_EFER));
    log_debug("SYSCALL: STAR=0x%llx (kernel=0x%02x, user_base=0x%02x)",
              rdmsr(MSR_STAR), GDT_KERNEL_CODE, GDT_KERNEL_DATA);
    log_debug("SYSCALL: LSTAR=0x%llx", rdmsr(MSR_LSTAR));
    log_debug("SYSCALL: SFMASK=0x%llx", rdmsr(MSR_SFMASK));

    /* Initialize syscall dispatch table */
    syscall_table_init();
}

/**
 * syscall_table_init - Populate syscall dispatch table
 *
 * Fills the table with default handlers, then registers
 * implemented syscalls.
 */
void syscall_table_init(void)
{
    /* Initialize all entries to return -ENOSYS */
    for (int i = 0; i < NR_SYSCALLS; i++) {
        syscall_table[i] = sys_ni_syscall;
    }

    /* Register implemented syscalls */
    syscall_table[SYS_read]       = sys_read;
    syscall_table[SYS_write]      = sys_write;
    syscall_table[SYS_exit]       = sys_exit;
    syscall_table[SYS_getpid]     = sys_getpid;
    syscall_table[SYS_brk]        = sys_brk;
    syscall_table[SYS_mmap]       = sys_mmap;
    syscall_table[SYS_munmap]     = sys_munmap;
    syscall_table[SYS_arch_prctl] = sys_arch_prctl;
    syscall_table[SYS_getppid]    = sys_getppid;
    syscall_table[SYS_uname]      = sys_uname;
    syscall_table[SYS_wait4]      = sys_wait4;
    syscall_table[SYS_fork]       = sys_fork;
    syscall_table[SYS_open]       = sys_open;
    syscall_table[SYS_close]      = sys_close;

    log_debug("SYSCALL: Table initialized with %d entries", NR_SYSCALLS);
}

/**
 * syscall_dispatch - C syscall handler
 * @frame: Pointer to saved syscall registers on stack
 *
 * Called from syscall_entry.S with pointer to syscall_frame_t.
 * Looks up handler in syscall_table and invokes it.
 * Returns the syscall result (or negative errno on error).
 */
int64_t syscall_dispatch(syscall_frame_t *frame)
{
    uint64_t nr = frame->nr;
    syscall_fn_t handler;
    process_t *proc = process_current();

    /*
     * Snapshot the caller's user-mode RIP/RFLAGS/RSP into the PCB so
     * syscalls like fork() can build a child sysret context without
     * groping the kernel stack themselves.
     *
     * syscall_entry.S pushes rcx (user RIP) and r11 (user RFLAGS)
     * before the syscall_frame_t, so they sit immediately above it
     * on the kernel stack. The user RSP was already stashed in the
     * percpu slot by the entry stub.
     *
     * RFLAGS is sanitized at save time so every downstream consumer
     * (e.g. fork's sysret trampoline) gets the safe value. The syscall
     * return path in syscall_entry.S sanitizes the live R11 separately.
     *
     * These fields are only valid for entries from CPL=3 via syscall;
     * they must not be inspected from IRQ handlers or any other path.
     */
    if (proc) {
        uint64_t *qw = (uint64_t *)frame;
        proc->user_rflags = sanitize_user_rflags(qw[7]);
        proc->user_rip    = qw[8];
        proc->user_rsp    = percpu_get()->user_rsp;
    }

    /* Validate syscall number */
    if (nr >= NR_SYSCALLS) {
        log_debug("SYSCALL: invalid nr=%llu (max=%d)", nr, NR_SYSCALLS - 1);
        return -ENOSYS;
    }

    handler = syscall_table[nr];

    /* Dispatch to handler */
    return handler(frame->arg1, frame->arg2, frame->arg3,
                   frame->arg4, frame->arg5, frame->arg6);
}

/*
 * =============================================================================
 * Syscall Implementations (Stubs)
 *
 * These are minimal implementations to enable basic userspace testing.
 * Full implementations will be added as we build out process management,
 * VFS, etc.
 * =============================================================================
 */

/**
 * sys_read - Read from a file descriptor
 */
static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return -EBADF;
    }

    /* Validate file descriptor */
    if (fd >= PROC_MAX_FDS || proc->fd_table[fd].file == NULL) {
        return -EBADF;
    }

    /* A zero-length read is a no-op; skip pointer validation. */
    if (count == 0)
        return 0;

    /* Pages must be present *and* writable by user (vfs_read writes there). */
    if (!vmm_user_range_writable(proc->pml4_phys, (void *)buf, count))
        return -EFAULT;

    vfs_file_t *file = proc->fd_table[fd].file;
    return vfs_read(file, (void *)buf, count);
}

/**
 * sys_write - Write to a file descriptor
 */
static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return -EBADF;
    }

    /* Validate file descriptor */
    if (fd >= PROC_MAX_FDS || proc->fd_table[fd].file == NULL) {
        return -EBADF;
    }

    if (count == 0)
        return 0;

    /* Pages must be present and user-readable. */
    if (!vmm_user_range_readable(proc->pml4_phys, (void *)buf, count))
        return -EFAULT;

    vfs_file_t *file = proc->fd_table[fd].file;
    return vfs_write(file, (const void *)buf, count);
}

/**
 * sys_exit - Terminate the calling process
 */
static int64_t sys_exit(uint64_t status, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    /*
     * If we have a current process, use proper exit.
     * Otherwise, fall back to halt (kernel context).
     */
    if (process_current()) {
        process_exit((int)status);
        /* Does not return */
    }

    /* No process context - just halt */
    log_info("Exit called in kernel context with status %llu - halting", status);
    for (;;) {
        __asm__ volatile("hlt");
    }

    /* Unreachable */
    return 0;
}

/**
 * sys_getpid - Get process ID
 */
static int64_t sys_getpid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (proc) {
        return (int64_t)proc->pid;
    }

    /* No process context - return 0 (kernel) */
    return 0;
}

/**
 * sys_brk - Adjust program break for heap allocation
 *
 * Linux semantics: brk(0) returns current brk. brk(addr) sets new brk.
 * Returns the current (possibly new) brk address on success.
 * On failure, returns the old brk (Linux behavior).
 */
static int64_t sys_brk(uint64_t brk, uint64_t arg2, uint64_t arg3,
                       uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return -ESRCH;
    }

    /* If brk is 0, just return current brk */
    if (brk == 0) {
        return (int64_t)proc->brk;
    }

    /* Validate: must be above brk_start and page-aligned when expanded */
    if (brk < proc->brk_start) {
        return (int64_t)proc->brk;  /* Return current brk on failure */
    }

    uint64_t old_brk = proc->brk;
    uint64_t new_brk = brk;

    /* Page-align the addresses for mapping purposes */
    uint64_t old_pages = (old_brk + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t new_pages = (new_brk + PAGE_SIZE - 1) / PAGE_SIZE;

    if (new_pages > old_pages) {
        /* Expanding: allocate and map new pages */
        for (uint64_t page = old_pages; page < new_pages; page++) {
            uint64_t vaddr = page * PAGE_SIZE;
            uint64_t phys = pmm_alloc();
            if (phys == PMM_ALLOC_FAILED) {
                /* Out of memory - return old brk */
                return (int64_t)old_brk;
            }

            /* Zero the page for security */
            syscall_memset(phys_to_virt(phys), 0, PAGE_SIZE);

            /* Map with user, writable permissions */
            if (vmm_map_page(proc->pml4_phys, vaddr, phys,
                            PTE_PRESENT | PTE_WRITABLE | PTE_USER) < 0) {
                pmm_free(phys);
                return (int64_t)old_brk;
            }
        }
    } else if (new_pages < old_pages) {
        /* Shrinking: unmap and free pages */
        for (uint64_t page = new_pages; page < old_pages; page++) {
            uint64_t vaddr = page * PAGE_SIZE;
            uint64_t phys = vmm_get_physical(proc->pml4_phys, vaddr);
            if (phys != 0) {
                vmm_unmap_page(proc->pml4_phys, vaddr);
                pmm_free(phys);
            }
        }
    }

    proc->brk = new_brk;
    return (int64_t)new_brk;
}

/**
 * sys_mmap - Map memory region
 *
 * Simplified implementation: only supports anonymous mappings (fd == -1).
 * Returns mapped address on success, or -errno on failure.
 */
static int64_t sys_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset)
{
    (void)offset;  /* Ignored for anonymous mappings */

    process_t *proc = process_current();
    if (!proc) {
        return -ESRCH;
    }

    /* Must have length */
    if (len == 0) {
        return -EINVAL;
    }

    /* Only support anonymous mappings for now */
    if (!(flags & MAP_ANONYMOUS) || (int64_t)fd != -1) {
        log_debug("MMAP: Only anonymous mappings supported (flags=0x%llx, fd=%lld)",
                  flags, (int64_t)fd);
        return -ENOSYS;
    }

    /* Round length up to page boundary */
    uint64_t num_pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Choose address if not specified or not MAP_FIXED */
    uint64_t map_addr;
    if (addr != 0 && (flags & MAP_FIXED)) {
        /* Use requested address (must be page-aligned) */
        if (addr & (PAGE_SIZE - 1)) {
            return -EINVAL;
        }
        map_addr = addr;
    } else {
        /* Allocate from mmap region - simple bump allocator */
        static uint64_t mmap_next = USER_MMAP_START;
        map_addr = mmap_next;
        mmap_next += num_pages * PAGE_SIZE;
    }

    /* Build page flags from protection */
    uint64_t page_flags = PTE_PRESENT | PTE_USER;
    if (prot & PROT_WRITE) {
        page_flags |= PTE_WRITABLE;
    }
    if (!(prot & PROT_EXEC)) {
        page_flags |= PTE_NX;
    }

    /* Allocate and map pages */
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t vaddr = map_addr + i * PAGE_SIZE;
        uint64_t phys = pmm_alloc();
        if (phys == PMM_ALLOC_FAILED) {
            /* Unmap what we already mapped */
            for (uint64_t j = 0; j < i; j++) {
                uint64_t v = map_addr + j * PAGE_SIZE;
                uint64_t p = vmm_get_physical(proc->pml4_phys, v);
                vmm_unmap_page(proc->pml4_phys, v);
                if (p) pmm_free(p);
            }
            return -ENOMEM;
        }

        /* Zero the page */
        syscall_memset(phys_to_virt(phys), 0, PAGE_SIZE);

        if (vmm_map_page(proc->pml4_phys, vaddr, phys, page_flags) < 0) {
            pmm_free(phys);
            /* Unmap previous pages */
            for (uint64_t j = 0; j < i; j++) {
                uint64_t v = map_addr + j * PAGE_SIZE;
                uint64_t p = vmm_get_physical(proc->pml4_phys, v);
                vmm_unmap_page(proc->pml4_phys, v);
                if (p) pmm_free(p);
            }
            return -ENOMEM;
        }
    }

    return (int64_t)map_addr;
}

/**
 * sys_munmap - Unmap memory region
 */
static int64_t sys_munmap(uint64_t addr, uint64_t len, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return -ESRCH;
    }

    /* Address must be page-aligned */
    if (addr & (PAGE_SIZE - 1)) {
        return -EINVAL;
    }

    if (len == 0) {
        return -EINVAL;
    }

    /* Round up to page boundary */
    uint64_t num_pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Unmap and free each page */
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t vaddr = addr + i * PAGE_SIZE;
        uint64_t phys = vmm_get_physical(proc->pml4_phys, vaddr);
        if (phys != 0) {
            vmm_unmap_page(proc->pml4_phys, vaddr);
            pmm_free(phys);
        }
    }

    return 0;
}

/**
 * sys_arch_prctl - Set/get architecture-specific thread state
 *
 * Used for TLS (Thread Local Storage) setup. Sets FS or GS base register.
 */
static int64_t sys_arch_prctl(uint64_t code, uint64_t addr, uint64_t arg3,
                              uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return -ESRCH;
    }

    switch (code) {
    case ARCH_SET_FS:
        if (!canonical_addr(addr))
            return -EINVAL;
        proc->fs_base = addr;
        wrmsr(MSR_FS_BASE, addr);
        return 0;

    case ARCH_GET_FS:
        if (!vmm_validate_user_range((void *)addr, sizeof(uint64_t)))
            return -EFAULT;
        if (!vmm_user_range_writable(proc->pml4_phys, (void *)addr,
                                     sizeof(uint64_t)))
            return -EFAULT;
        *(uint64_t *)addr = proc->fs_base;
        return 0;

    case ARCH_SET_GS:
        if (!canonical_addr(addr))
            return -EINVAL;
        proc->gs_base = addr;
        /*
         * In kernel mode, IA32_KERNEL_GS_BASE holds the value that
         * swapgs will swap into the active GS_BASE on the way back
         * to user mode. Stash @addr there so the next sysret picks
         * it up. The process_switch path restores this on every
         * subsequent context switch to keep it per-process.
         */
        wrmsr(MSR_KERNEL_GS_BASE, addr);
        return 0;

    case ARCH_GET_GS:
        if (!vmm_validate_user_range((void *)addr, sizeof(uint64_t)))
            return -EFAULT;
        if (!vmm_user_range_writable(proc->pml4_phys, (void *)addr,
                                     sizeof(uint64_t)))
            return -EFAULT;
        *(uint64_t *)addr = proc->gs_base;
        return 0;

    default:
        return -EINVAL;
    }
}

/**
 * sys_getppid - Get parent process ID
 */
static int64_t sys_getppid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return 0;
    }

    if (proc->parent) {
        return (int64_t)proc->parent->pid;
    }

    /* Init process (PID 1) has no parent, return 0 */
    return 0;
}

/**
 * sys_uname - Get system information
 */
static int64_t sys_uname(uint64_t buf, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc)
        return -ESRCH;

    if (!vmm_user_range_writable(proc->pml4_phys, (void *)buf,
                                 sizeof(struct utsname)))
        return -EFAULT;

    struct utsname *u = (struct utsname *)buf;

    /* Fill in system information */
    syscall_strncpy(u->sysname, "SeedOS", sizeof(u->sysname));
    syscall_strncpy(u->nodename, "seed", sizeof(u->nodename));
    syscall_strncpy(u->release, "0.1.0", sizeof(u->release));
    syscall_strncpy(u->version, "#1 SMP", sizeof(u->version));
    syscall_strncpy(u->machine, "x86_64", sizeof(u->machine));

    return 0;
}

/*
 * =============================================================================
 * wait4() Implementation
 * =============================================================================
 */

/**
 * find_zombie_child - Find a zombie child matching the pid specification
 * @parent: Parent process to search children of
 * @pid: PID specification:
 *       -1 = any child
 *       >0 = specific child with that PID
 *
 * Return: Zombie child process, or NULL if none found
 */
static process_t *find_zombie_child(process_t *parent, int64_t pid)
{
    process_t *child;

    for (child = parent->children; child != NULL; child = child->sibling) {
        /* Check if this child matches the pid specification */
        if (pid > 0 && (int64_t)child->pid != pid) {
            continue;  /* Looking for specific PID, this isn't it */
        }

        /* Check if this child is a zombie */
        if (child->state == PROC_ZOMBIE) {
            return child;
        }
    }

    return NULL;
}

/**
 * has_matching_children - Check if parent has any children matching pid spec
 * @parent: Parent process
 * @pid: PID specification (-1 = any, >0 = specific)
 *
 * Return: true if matching children exist, false otherwise
 */
static bool has_matching_children(process_t *parent, int64_t pid)
{
    process_t *child;

    for (child = parent->children; child != NULL; child = child->sibling) {
        if (pid == -1) {
            return true;  /* Any child matches */
        }
        if (pid > 0 && (int64_t)child->pid == pid) {
            return true;  /* Found specific child */
        }
    }

    return false;
}

/**
 * sys_wait4 - Wait for child process state change
 * @pid: Which child to wait for:
 *       -1 = any child
 *       >0 = child with this PID
 * @wstatus: Pointer to store wait status (can be NULL)
 * @options: Wait options (WNOHANG, etc.)
 * @rusage: Resource usage (not implemented, ignored)
 *
 * Return: PID of child on success, 0 if WNOHANG with no zombie,
 *         negative errno on error
 *
 * This implements the core of waitpid()/wait4() semantics:
 * - Find a zombie child matching the pid specification
 * - Reap the zombie (free its resources)
 * - Return its exit status to the parent
 */
static int64_t sys_wait4(uint64_t pid, uint64_t wstatus, uint64_t options,
                         uint64_t rusage, uint64_t arg5, uint64_t arg6)
{
    (void)rusage;  /* Resource usage tracking not implemented */
    (void)arg5; (void)arg6;

    process_t *proc = process_current();
    process_t *child;
    int64_t wait_pid = (int64_t)pid;

    if (!proc) {
        return -ESRCH;
    }

    log_debug("WAIT4: PID %llu waiting for pid=%lld, options=0x%llx",
              proc->pid, wait_pid, options);

    /*
     * Validate wstatus pointer if provided.
     * NULL is valid - caller doesn't want status.
     */
    if (wstatus != 0 &&
        !vmm_user_range_writable(proc->pml4_phys, (void *)wstatus,
                                 sizeof(int))) {
        return -EFAULT;
    }

    /*
     * Main wait loop
     */
    for (;;) {
        /* Look for a zombie child matching our criteria */
        child = find_zombie_child(proc, wait_pid);

        if (child) {
            /* Found a zombie - reap it */
            uint64_t child_pid = child->pid;
            int status = W_EXITCODE(child->exit_code);

            log_debug("WAIT4: Reaping zombie PID %llu (exit_code=%d)",
                      child_pid, child->exit_code);

            /* Store status if caller wants it */
            if (wstatus != 0) {
                *(int *)wstatus = status;
            }

            /* Destroy the zombie process (frees all resources) */
            process_destroy(child);

            return (int64_t)child_pid;
        }

        /* No zombie found - check if we have any matching children at all */
        if (!has_matching_children(proc, wait_pid)) {
            log_debug("WAIT4: No matching children for pid=%lld", wait_pid);
            return -ECHILD;
        }

        /* WNOHANG: return immediately if no zombie */
        if (options & WNOHANG) {
            log_debug("WAIT4: WNOHANG set, returning 0");
            return 0;
        }

        /*
         * Block until a child exits.
         * Set up our wait state and go to sleep.
         * process_exit() will wake us when a child becomes a zombie.
         */
        log_debug("WAIT4: PID %llu sleeping, waiting for child", proc->pid);
        proc->wait_pid = (int)wait_pid;
        proc->state = PROC_SLEEPING;

        /*
         * TODO: Integrate with scheduler properly.
         * For now, we busy-wait with hlt to save CPU cycles.
         * This works because process_exit() sets us back to RUNNABLE
         * and we're single-process anyway.
         */
        while (proc->state == PROC_SLEEPING) {
            __asm__ volatile("sti; hlt; cli");
        }

        /* Woke up - loop back to check for zombie again */
        log_debug("WAIT4: PID %llu woke up", proc->pid);
    }
}

/*
 * =============================================================================
 * fork() Implementation
 * =============================================================================
 */

/**
 * fork_child_user_return - First-run trampoline for a freshly forked child.
 *
 * Runs as a kthread entry. The kthread's stack is only live until the
 * sysretq below; afterwards future syscalls from the child land on
 * child->kernel_stack_top via TSS.rsp0 / percpu.kernel_rsp.
 *
 * Refuses to sysret a child whose user_rip/user_rsp is non-canonical
 * (CVE-2012-0217 class): such a sysretq would deliver #GP in kernel
 * mode with user state still live.
 */
static void fork_child_user_return(void *arg)
{
    process_t *child = (process_t *)arg;
    kthread_t *self  = kthread_current();

    /*
     * From here through sysretq we must not be preempted: an IRQ that
     * lands after swapgs but before sysretq would run isr_common, see
     * the kernel CS and skip its own swapgs, then access %gs:... with
     * the user GS base live. Disable interrupts up front; sysretq
     * atomically restores RFLAGS (and thus IF) from R11.
     */
    __asm__ volatile("cli" ::: "memory");

    /* If user state is unsafe, never sysret - kill the child and yield. */
    if (!canonical_addr(child->user_rip) ||
        !canonical_addr(child->user_rsp)) {
        log_error("FORK: child PID %llu has non-canonical user state "
                  "(rip=0x%llx rsp=0x%llx); aborting",
                  child->pid, child->user_rip, child->user_rsp);
        if (self)
            self->state = THREAD_EXITED;
        child->kthread = NULL;
        process_destroy(child);
        __asm__ volatile("sti" ::: "memory");
        for (;;)
            kthread_yield();
    }

    /* Install the child as the running process. */
    process_set_current(child);          /* updates percpu.kernel_rsp too */
    gdt_set_tss_rsp0(child->kernel_stack_top);
    vmm_switch_address_space(child->pml4_phys);

    /* Stage per-process segment bases for the swapgs+sysretq below. */
    wrmsr(MSR_FS_BASE,        canonical_addr(child->fs_base) ? child->fs_base : 0);
    wrmsr(MSR_KERNEL_GS_BASE, canonical_addr(child->gs_base) ? child->gs_base : 0);

    /*
     * Detach ourselves from the kthread list before sysret. The stack
     * we're standing on becomes unreachable - the next kthread_reap()
     * frees it. Clearing child->kthread prevents process_destroy from
     * walking back to a kthread we're about to abandon.
     */
    if (self)
        self->state = THREAD_EXITED;
    child->kthread = NULL;

    /*
     * Return to user mode. Inputs are pinned to r8/r9/r10 so the rest
     * of the GPRs can be zeroed without clobbering anything live.
     *
     * sysretq: RIP=RCX, RFLAGS=(R11 & 0x3C7FD7)|0x2, CS=user, SS=user.
     */
    register uint64_t rip_in    __asm__("r8")  = child->user_rip;
    register uint64_t rflags_in __asm__("r9")  = child->user_rflags;
    register uint64_t rsp_in    __asm__("r10") = child->user_rsp;

    __asm__ volatile (
        "movq %0, %%rcx\n\t"          /* RCX  = user RIP    */
        "movq %1, %%r11\n\t"          /* R11  = user RFLAGS */
        "movq %2, %%rsp\n\t"          /* RSP  = user RSP    */
        "xorl %%eax, %%eax\n\t"       /* rax = 0 (fork returns 0 in child) */
        "xorl %%edx, %%edx\n\t"
        "xorl %%esi, %%esi\n\t"
        "xorl %%edi, %%edi\n\t"
        "xorl %%ebp, %%ebp\n\t"
        "xorl %%ebx, %%ebx\n\t"
        "xorl %%r8d, %%r8d\n\t"
        "xorl %%r9d, %%r9d\n\t"
        "xorl %%r10d, %%r10d\n\t"
        "xorl %%r12d, %%r12d\n\t"
        "xorl %%r13d, %%r13d\n\t"
        "xorl %%r14d, %%r14d\n\t"
        "xorl %%r15d, %%r15d\n\t"
        "swapgs\n\t"
        "sysretq"
        :
        : "r"(rip_in), "r"(rflags_in), "r"(rsp_in)
        : "memory"
    );
    __builtin_unreachable();
}

/**
 * sys_fork - Create a child process
 *
 * Creates a new process that is a copy of the calling process.
 * Uses Copy-on-Write (COW) for memory efficiency.
 *
 * Return: Child PID to parent, 0 to child, negative errno on error
 */
static int64_t sys_fork(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;

    process_t *parent = process_current();
    process_t *child;
    void *kernel_stack;
    int i;

    if (!parent) {
        return -ESRCH;
    }

    log_debug("FORK: Parent PID %llu forking", parent->pid);

    /*
     * Allocate child process control block
     */
    child = kmalloc(sizeof(process_t));
    if (!child) {
        log_error("FORK: Failed to allocate child PCB");
        return -ENOMEM;
    }

    /* Copy parent's PCB as starting point */
    for (size_t j = 0; j < sizeof(process_t); j++) {
        ((uint8_t *)child)[j] = ((uint8_t *)parent)[j];
    }

    /*
     * Allocate child's kernel stack
     */
    kernel_stack = kmalloc(16384);  /* KERNEL_STACK_SIZE */
    if (!kernel_stack) {
        log_error("FORK: Failed to allocate kernel stack");
        kfree(child);
        return -ENOMEM;
    }
    child->kernel_stack_top = (uint64_t)kernel_stack + 16384;

    /*
     * Copy address space with COW semantics
     */
    child->pml4_phys = vmm_copy_address_space_cow(parent->pml4_phys);
    if (child->pml4_phys == 0) {
        log_error("FORK: Failed to copy address space");
        kfree(kernel_stack);
        kfree(child);
        return -ENOMEM;
    }

    /*
     * Set up child identity
     */
    child->pid = process_allocate_pid();
    child->state = PROC_RUNNABLE;
    child->parent = parent;
    child->children = NULL;  /* No children yet */
    child->kthread = NULL;   /* Will be set when scheduled */
    child->exit_code = 0;
    child->wait_pid = -1;

    /*
     * Copy file descriptors - increment reference counts
     */
    for (i = 0; i < PROC_MAX_FDS; i++) {
        if (parent->fd_table[i].file != NULL) {
            vfs_file_ref(parent->fd_table[i].file);
            /* child->fd_table already has the pointer from PCB copy */
        }
    }

    /*
     * Link child into parent's children list
     */
    child->sibling = parent->children;
    parent->children = child;

    /*
     * Add to global process list
     */
    process_add(child);

    /*
     * Create the kthread that will return the child to user mode with
     * rax=0. The child PCB already has user_rip/user_rsp/user_rflags
     * snapshotted by syscall_dispatch() before this handler ran (and
     * memcpy'd into the child above). We record the kthread pointer
     * so process_destroy can tear it down if the child never sysrets.
     */
    uint64_t child_tid = kthread_create("fork-child",
                                        fork_child_user_return, child);
    if (child_tid == 0) {
        log_error("FORK: Failed to create child kthread");
        process_destroy(child);
        return -ENOMEM;
    }
    child->kthread = kthread_get_kthread(child_tid);

    log_info("FORK: Created child PID %llu from parent PID %llu (COW)",
             child->pid, parent->pid);

    return (int64_t)child->pid;
}

/*
 * =============================================================================
 * File operations for ext2 files
 * =============================================================================
 */

/* Private data for ext2 file handles */
typedef struct {
    uint32_t inode_num;     /* Inode number */
    ext2_inode_t *inode;    /* Cached inode */
    uint64_t size;          /* File size */
} ext2_file_private_t;

static ssize_t ext2_file_read(vfs_file_t *file, void *buf, size_t count)
{
    ext2_file_private_t *priv = (ext2_file_private_t *)file->private;
    ssize_t bytes_read;

    if (!priv || !priv->inode) {
        return -EBADF;
    }

    /* Don't read past end of file */
    if (file->offset >= priv->size) {
        return 0;  /* EOF */
    }
    if (file->offset + count > priv->size) {
        count = priv->size - file->offset;
    }

    bytes_read = ext2_read_file(priv->inode, file->offset, buf, count);
    if (bytes_read > 0) {
        file->offset += bytes_read;
    }

    return bytes_read;
}

static ssize_t ext2_file_write(vfs_file_t *file, const void *buf, size_t count)
{
    (void)file; (void)buf; (void)count;
    /* ext2 is read-only */
    return -EROFS;
}

static off_t ext2_file_lseek(vfs_file_t *file, off_t offset, int whence)
{
    ext2_file_private_t *priv = (ext2_file_private_t *)file->private;
    int64_t new_offset;

    if (!priv) {
        return -EBADF;
    }

    switch (whence) {
    case SEEK_SET:
        new_offset = offset;
        break;
    case SEEK_CUR:
        new_offset = (int64_t)file->offset + offset;
        break;
    case SEEK_END:
        new_offset = (int64_t)priv->size + offset;
        break;
    default:
        return -EINVAL;
    }

    if (new_offset < 0) {
        return -EINVAL;
    }

    file->offset = (uint64_t)new_offset;
    return (off_t)file->offset;
}

static int ext2_file_close(vfs_file_t *file)
{
    if (file->private) {
        kfree(file->private);
        file->private = NULL;
    }
    return 0;
}

static file_ops_t ext2_file_ops = {
    .read = ext2_file_read,
    .write = ext2_file_write,
    .lseek = ext2_file_lseek,
    .close = ext2_file_close,
    .ioctl = NULL,
};

/*
 * =============================================================================
 * open() / close() Implementation
 * =============================================================================
 */

/**
 * sys_open - Open a file
 * @pathname: Path to the file
 * @flags: Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
 * @mode: File mode (for O_CREAT, unused currently)
 *
 * Return: File descriptor on success, negative errno on error
 */
static int64_t sys_open(uint64_t pathname, uint64_t flags, uint64_t mode,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)mode; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    char path[PATH_MAX];
    int64_t path_len;
    uint32_t ino;
    ext2_inode_t *inode;
    vfs_file_t *file;
    ext2_file_private_t *priv;
    int fd;

    if (!proc) {
        return -ESRCH;
    }

    /* Copy path from user space into a bounded kernel buffer. */
    path_len = copy_string_from_user(path, (const void *)pathname, sizeof(path));
    if (path_len < 0)
        return path_len;

    log_debug("OPEN: path='%s' flags=0x%llx", path, flags);

    /* Look up the file in ext2 */
    ino = ext2_lookup(path);
    if (ino == 0) {
        log_debug("OPEN: File not found: %s", path);
        return -ENOENT;
    }

    /* Read the inode */
    inode = ext2_read_inode(ino);
    if (!inode) {
        return -EIO;
    }

    /* Check it's a regular file */
    if (!ext2_is_regular_file(inode)) {
        /* TODO: Support directories, etc. */
        return -EISDIR;
    }

    /* Allocate file descriptor */
    fd = process_fd_alloc(proc);
    if (fd < 0) {
        return -EMFILE;
    }

    /* Allocate VFS file structure */
    file = vfs_file_alloc();
    if (!file) {
        process_fd_free(proc, fd);
        return -ENOMEM;
    }

    /* Allocate private data */
    priv = kmalloc(sizeof(ext2_file_private_t));
    if (!priv) {
        vfs_close(file);
        process_fd_free(proc, fd);
        return -ENOMEM;
    }

    priv->inode_num = ino;
    priv->inode = inode;
    priv->size = ext2_get_file_size(inode);

    /* Set up the file structure */
    file->type = VFS_TYPE_REG;
    file->flags = (int)flags;
    file->offset = 0;
    file->ops = &ext2_file_ops;
    file->private = priv;

    /* Install in process fd table */
    proc->fd_table[fd].file = file;
    proc->fd_table[fd].flags = 0;

    log_debug("OPEN: Opened '%s' as fd %d (ino=%u, size=%llu)",
              path, fd, ino, priv->size);

    return fd;
}

/**
 * sys_close - Close a file descriptor
 * @fd: File descriptor to close
 *
 * Return: 0 on success, negative errno on error
 */
static int64_t sys_close(uint64_t fd, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    vfs_file_t *file;

    if (!proc) {
        return -ESRCH;
    }

    /* Validate file descriptor */
    if (fd >= PROC_MAX_FDS) {
        return -EBADF;
    }

    file = proc->fd_table[fd].file;
    if (!file) {
        return -EBADF;
    }

    log_debug("CLOSE: fd=%llu", fd);

    /* Close the file (decrements refcount, frees if zero) */
    vfs_close(file);

    /* Clear the fd table entry */
    proc->fd_table[fd].file = NULL;
    proc->fd_table[fd].flags = 0;

    return 0;
}
