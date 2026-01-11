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
    syscall_table[SYS_read]   = sys_read;
    syscall_table[SYS_write]  = sys_write;
    syscall_table[SYS_exit]   = sys_exit;
    syscall_table[SYS_getpid] = sys_getpid;

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
