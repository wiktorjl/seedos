/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Process Management
 *
 * A process represents a user program with its own address space,
 * file descriptors, and execution context. The kernel execution
 * context is provided by an underlying kthread.
 *
 * Process lifecycle:
 *   EMBRYO    - Being created, not yet runnable
 *   RUNNABLE  - Ready to be scheduled
 *   RUNNING   - Currently executing
 *   SLEEPING  - Blocked waiting for I/O or event
 *   ZOMBIE    - Exited, waiting for parent to reap
 *
 * Memory layout per process:
 *   - Separate PML4 (page table root) for user address space
 *   - 16KB kernel stack for syscall/interrupt handling
 *   - User stack allocated in user address space
 */

#ifndef _PROCESS_H
#define _PROCESS_H

#include "types.h"
#include "kthread.h"
#include "vfs.h"

/*
 * Maximum number of open file descriptors per process
 */
#define PROC_MAX_FDS    256

/*
 * User address space limits
 */
#define USER_STACK_TOP      0x800000000ULL      /* 32 GB - stack grows down */
#define USER_STACK_SIZE     0x200000ULL         /* 2 MB initial stack */
#define USER_STACK_BOTTOM   (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_HEAP_START     0x10000000ULL       /* 256 MB - heap starts here */
#define USER_MMAP_START     0x100000000ULL      /* 4 GB - mmap region */

/*
 * Process states
 */
typedef enum {
    PROC_EMBRYO,        /* Being created */
    PROC_RUNNABLE,      /* Ready to run */
    PROC_RUNNING,       /* Currently running */
    PROC_SLEEPING,      /* Blocked on I/O or wait */
    PROC_ZOMBIE         /* Exited, waiting for parent to reap */
} proc_state_t;

/*
 * File descriptor entry
 */
typedef struct fd_entry {
    vfs_file_t *file;   /* VFS file handle (NULL if unused) */
    int flags;          /* O_CLOEXEC, etc. */
} fd_entry_t;

/*
 * Process control block
 *
 * Contains all state for a user process.
 */
typedef struct process {
    /*
     * Identity
     */
    uint64_t pid;               /* Process ID (1 = init) */
    char name[64];              /* Process name for debugging */
    proc_state_t state;         /* Current state */

    /*
     * Memory management
     */
    uint64_t pml4_phys;         /* Physical address of page table root */
    uint64_t brk_start;         /* Initial program break (end of data) */
    uint64_t brk;               /* Current program break */

    /*
     * Kernel execution context
     */
    kthread_t *kthread;         /* Underlying kernel thread */
    uint64_t kernel_stack_top;  /* Top of 16KB kernel stack */

    /*
     * User register state (saved on syscall/interrupt entry)
     */
    uint64_t user_rip;          /* User instruction pointer */
    uint64_t user_rsp;          /* User stack pointer */
    uint64_t user_rflags;       /* User flags */

    /*
     * Segment bases (for TLS)
     */
    uint64_t fs_base;           /* FS segment base (TLS) */
    uint64_t gs_base;           /* User GS segment base */

    /*
     * File descriptors
     */
    fd_entry_t fd_table[PROC_MAX_FDS];

    /*
     * Process tree
     */
    struct process *parent;     /* Parent process (NULL for init) */
    struct process *children;   /* First child (linked list) */
    struct process *sibling;    /* Next sibling (same parent) */
    int exit_code;              /* Exit status (valid when ZOMBIE) */

    /*
     * Wait state
     */
    int wait_pid;               /* PID being waited for (-1 = any) */

    /*
     * Signals (minimal for now)
     */
    uint64_t pending_signals;   /* Bitmap of pending signals */
    uint64_t blocked_signals;   /* Bitmap of blocked signals */

    /*
     * List linkage
     */
    struct process *next;       /* Next in global process list */
} process_t;

/*
 * Process management functions
 */

/**
 * process_init - Initialize the process subsystem
 *
 * Sets up global state. Called once during boot.
 */
void process_init(void);

/**
 * process_create - Create a new process
 * @name: Process name for debugging
 *
 * Allocates PCB, kernel stack, and empty address space.
 * Process starts in EMBRYO state.
 *
 * Return: New process, or NULL on failure
 */
process_t *process_create(const char *name);

/**
 * process_destroy - Free all process resources
 * @proc: Process to destroy
 *
 * Called after process is reaped. Frees kernel stack,
 * address space, and PCB.
 */
void process_destroy(process_t *proc);

/**
 * process_current - Get the currently running process
 *
 * Return: Current process, or NULL if in kernel context
 */
process_t *process_current(void);

/**
 * process_set_current - Set the current process
 * @proc: Process to set as current
 */
void process_set_current(process_t *proc);

/**
 * process_find - Find process by PID
 * @pid: Process ID to find
 *
 * Return: Process with given PID, or NULL if not found
 */
process_t *process_find(uint64_t pid);

/**
 * process_switch - Switch to another process
 * @next: Process to switch to
 *
 * Updates TSS.rsp0, per-CPU kernel stack, and CR3 if needed.
 * Performs context switch via underlying kthread.
 */
void process_switch(process_t *next);

/**
 * process_exit - Exit the current process
 * @status: Exit status code
 *
 * Sets state to ZOMBIE, wakes parent if waiting.
 * Does not return - switches to another process.
 */
void process_exit(int status);

/**
 * process_allocate_pid - Get the next available PID
 *
 * Return: New unique PID
 */
uint64_t process_allocate_pid(void);

/*
 * File descriptor operations
 */

/**
 * process_fd_alloc - Allocate a file descriptor
 * @proc: Process to allocate in
 *
 * Return: New fd number (>= 0), or -1 if table full
 */
int process_fd_alloc(process_t *proc);

/**
 * process_fd_free - Free a file descriptor
 * @proc: Process to free from
 * @fd: File descriptor to free
 */
void process_fd_free(process_t *proc, int fd);

/*
 * Global process list iteration
 */
process_t *process_list_head(void);

#endif /* _PROCESS_H */
