// SPDX-License-Identifier: GPL-2.0-only
/*
 * Syscall/Sysret MSR Configuration and Dispatch
 *
 * Sets up the MSRs that control fast syscall/sysret instructions
 * and dispatches syscalls to their handlers.
 */

#include "syscall.h"
#include "syscall_table.h"
#include "percpu.h"
#include "gdt.h"
#include "log.h"
#include "terminal.h"

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
 *
 * Stub: Currently only returns -EBADF (no VFS yet)
 */
static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)buf; (void)count;
    (void)arg4; (void)arg5; (void)arg6;

    log_debug("sys_read(fd=%llu, buf=0x%llx, count=%llu)", fd, buf, count);

    /* TODO: Implement when VFS is ready */
    return -EBADF;
}

/**
 * sys_write - Write to a file descriptor
 *
 * Stub: Writes to terminal for fd 1 (stdout) and 2 (stderr)
 */
static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg4; (void)arg5; (void)arg6;

    /* Only stdout (1) and stderr (2) supported for now */
    if (fd != 1 && fd != 2) {
        return -EBADF;
    }

    /*
     * TODO: Validate user pointer with vmm_validate_user_range()
     * For now, we trust the pointer (dangerous but works for testing)
     */
    const char *user_buf = (const char *)buf;
    terminal_t *term = terminal_get_active();

    /* Write each character to terminal */
    for (uint64_t i = 0; i < count; i++) {
        terminal_putchar(term, user_buf[i]);
    }

    return (int64_t)count;
}

/**
 * sys_exit - Terminate the calling process
 *
 * Stub: Logs and halts (no process management yet)
 */
static int64_t sys_exit(uint64_t status, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    log_info("Process exited with status %llu", status);

    /*
     * TODO: Proper process termination when process management is ready.
     * For now, just halt the CPU.
     */
    log_info("Halting CPU (no process management yet)");
    for (;;) {
        __asm__ volatile("hlt");
    }

    /* Unreachable */
    return 0;
}

/**
 * sys_getpid - Get process ID
 *
 * Stub: Returns 1 (we're always "init" for now)
 */
static int64_t sys_getpid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;

    /* TODO: Return actual PID from current process struct */
    return 1;
}
