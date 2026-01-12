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
 * arch_prctl codes
 */
#define ARCH_SET_GS     0x1001
#define ARCH_SET_FS     0x1002
#define ARCH_GET_FS     0x1003
#define ARCH_GET_GS     0x1004

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

    /*
     * TODO: Validate user pointer with vmm_validate_user_range()
     * For now, we trust the pointer (dangerous but works for testing)
     */
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

    /*
     * TODO: Validate user pointer with vmm_validate_user_range()
     * For now, we trust the pointer (dangerous but works for testing)
     */
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
        proc->fs_base = addr;
        /* Set FS base immediately - MSR 0xC0000100 is IA32_FS_BASE */
        wrmsr(0xC0000100, addr);
        return 0;

    case ARCH_GET_FS:
        if (!vmm_validate_user_range((void *)addr, sizeof(uint64_t))) {
            return -EFAULT;
        }
        *(uint64_t *)addr = proc->fs_base;
        return 0;

    case ARCH_SET_GS:
        proc->gs_base = addr;
        /* Note: GS base is tricky because kernel uses it. Store in process. */
        return 0;

    case ARCH_GET_GS:
        if (!vmm_validate_user_range((void *)addr, sizeof(uint64_t))) {
            return -EFAULT;
        }
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

    if (!vmm_validate_user_range((void *)buf, sizeof(struct utsname))) {
        return -EFAULT;
    }

    struct utsname *u = (struct utsname *)buf;

    /* Fill in system information */
    syscall_strncpy(u->sysname, "SeedOS", sizeof(u->sysname));
    syscall_strncpy(u->nodename, "seed", sizeof(u->nodename));
    syscall_strncpy(u->release, "0.1.0", sizeof(u->release));
    syscall_strncpy(u->version, "#1 SMP", sizeof(u->version));
    syscall_strncpy(u->machine, "x86_64", sizeof(u->machine));

    return 0;
}
