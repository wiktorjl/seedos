// SPDX-License-Identifier: GPL-2.0-only
/*
 * Process Management
 *
 * Implements process creation, destruction, and lifecycle management.
 * Processes wrap kernel threads (kthread) with user address spaces.
 */

#include "process.h"
#include "kthread.h"
#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "gdt.h"
#include "percpu.h"
#include "log.h"
#include "tty_dev.h"
#include <stddef.h>

/*
 * Kernel stack size for each process (16KB)
 */
#define KERNEL_STACK_SIZE   16384

/*
 * Global process state
 */
static process_t *process_list = NULL;      /* Head of global process list */
static process_t *current_proc = NULL;      /* Currently running process */
static uint64_t next_pid = 1;               /* Next PID to allocate */

/*
 * Init process (PID 1) - parent of all orphaned processes
 */
static process_t *init_process = NULL;

/**
 * process_init - Initialize the process subsystem
 */
void process_init(void)
{
    process_list = NULL;
    current_proc = NULL;
    next_pid = 1;
    init_process = NULL;

    log_debug("PROCESS: Subsystem initialized");
}

/**
 * process_allocate_pid - Get the next available PID
 */
uint64_t process_allocate_pid(void)
{
    return next_pid++;
}

/**
 * process_create - Create a new process
 * @name: Process name for debugging
 *
 * Allocates PCB, kernel stack, and empty address space.
 * Process starts in EMBRYO state.
 */
process_t *process_create(const char *name)
{
    process_t *proc;
    void *kernel_stack;

    /* Allocate process control block */
    proc = kmalloc(sizeof(process_t));
    if (!proc) {
        log_error("PROCESS: Failed to allocate PCB");
        return NULL;
    }

    /* Zero initialize */
    for (size_t i = 0; i < sizeof(process_t); i++) {
        ((uint8_t *)proc)[i] = 0;
    }

    /* Set name */
    size_t name_len = 0;
    while (name[name_len] && name_len < sizeof(proc->name) - 1) {
        proc->name[name_len] = name[name_len];
        name_len++;
    }
    proc->name[name_len] = '\0';

    /* Allocate PID */
    proc->pid = process_allocate_pid();

    /* First process becomes init */
    if (proc->pid == 1) {
        init_process = proc;
    }

    /* Allocate kernel stack */
    kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!kernel_stack) {
        log_error("PROCESS: Failed to allocate kernel stack for PID %llu", proc->pid);
        kfree(proc);
        return NULL;
    }
    /* Stack grows down - top is base + size */
    proc->kernel_stack_top = (uint64_t)kernel_stack + KERNEL_STACK_SIZE;

    /* Create user address space */
    proc->pml4_phys = vmm_create_address_space();
    if (proc->pml4_phys == 0) {
        log_error("PROCESS: Failed to create address space for PID %llu", proc->pid);
        kfree(kernel_stack);
        kfree(proc);
        return NULL;
    }

    /* Initialize state */
    proc->state = PROC_EMBRYO;
    proc->parent = current_proc;  /* Current process is parent (NULL for init) */
    proc->exit_code = 0;
    proc->wait_pid = -1;

    /* Initialize file descriptors (all NULL/closed) */
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        proc->fd_table[i].file = NULL;
        proc->fd_table[i].flags = 0;
    }

    /* Set up standard file descriptors */
    proc->fd_table[0].file = tty_open(O_RDONLY);   /* stdin */
    proc->fd_table[1].file = tty_open(O_WRONLY);   /* stdout */
    proc->fd_table[2].file = tty_open(O_WRONLY);   /* stderr */

    if (!proc->fd_table[0].file || !proc->fd_table[1].file || !proc->fd_table[2].file) {
        log_warn("PROCESS: Failed to set up stdio for PID %llu", proc->pid);
    }

    /* Add to parent's children list */
    if (proc->parent) {
        proc->sibling = proc->parent->children;
        proc->parent->children = proc;
    }

    /* Add to global process list */
    proc->next = process_list;
    process_list = proc;

    log_debug("PROCESS: Created '%s' (PID %llu, pml4=0x%llx, kstack=0x%llx)",
              proc->name, proc->pid, proc->pml4_phys, proc->kernel_stack_top);

    return proc;
}

/**
 * process_destroy - Free all process resources
 * @proc: Process to destroy
 *
 * Called after process is reaped. Frees kernel stack,
 * address space, and PCB.
 */
void process_destroy(process_t *proc)
{
    process_t **pp;

    if (!proc) {
        return;
    }

    log_debug("PROCESS: Destroying PID %llu", proc->pid);

    /* Remove from parent's children list */
    if (proc->parent) {
        pp = &proc->parent->children;
        while (*pp) {
            if (*pp == proc) {
                *pp = proc->sibling;
                break;
            }
            pp = &(*pp)->sibling;
        }
    }

    /* Remove from global process list */
    pp = &process_list;
    while (*pp) {
        if (*pp == proc) {
            *pp = proc->next;
            break;
        }
        pp = &(*pp)->next;
    }

    /* Close all open file descriptors */
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        if (proc->fd_table[i].file) {
            vfs_close(proc->fd_table[i].file);
            proc->fd_table[i].file = NULL;
        }
    }

    /* Free user address space */
    if (proc->pml4_phys) {
        vmm_free_user_address_space(proc->pml4_phys);
    }

    /* Free kernel stack */
    if (proc->kernel_stack_top) {
        void *stack_base = (void *)(proc->kernel_stack_top - KERNEL_STACK_SIZE);
        kfree(stack_base);
    }

    /* Free the PCB */
    kfree(proc);
}

/**
 * process_current - Get the currently running process
 */
process_t *process_current(void)
{
    return current_proc;
}

/**
 * process_set_current - Set the current process
 * @proc: Process to set as current
 */
void process_set_current(process_t *proc)
{
    current_proc = proc;

    /* Also update per-CPU data */
    if (proc) {
        percpu_set_kernel_stack(proc->kernel_stack_top);
    }
}

/**
 * process_find - Find process by PID
 * @pid: Process ID to find
 */
process_t *process_find(uint64_t pid)
{
    process_t *proc = process_list;

    while (proc) {
        if (proc->pid == pid) {
            return proc;
        }
        proc = proc->next;
    }

    return NULL;
}

/**
 * process_list_head - Get head of process list
 */
process_t *process_list_head(void)
{
    return process_list;
}

/**
 * process_exit - Exit the current process
 * @status: Exit status code
 *
 * Sets state to ZOMBIE, reparents children, wakes parent.
 * Does not return - switches to another process.
 */
void process_exit(int status)
{
    process_t *proc = current_proc;
    process_t *child;

    if (!proc) {
        log_error("PROCESS: exit called with no current process!");
        return;
    }

    log_info("PROCESS: PID %llu exiting with status %d", proc->pid, status);

    /* Store exit code */
    proc->exit_code = status;

    /* Reparent children to init */
    if (init_process && proc != init_process) {
        while (proc->children) {
            child = proc->children;
            proc->children = child->sibling;

            /* Add to init's children */
            child->parent = init_process;
            child->sibling = init_process->children;
            init_process->children = child;

            log_debug("PROCESS: Reparented PID %llu to init", child->pid);
        }
    }

    /* Set state to ZOMBIE */
    proc->state = PROC_ZOMBIE;

    /* Wake parent if it's waiting */
    if (proc->parent && proc->parent->state == PROC_SLEEPING) {
        /* Check if parent is waiting for this child or any child */
        if (proc->parent->wait_pid == -1 ||
            proc->parent->wait_pid == (int)proc->pid) {
            proc->parent->state = PROC_RUNNABLE;
            log_debug("PROCESS: Woke parent PID %llu", proc->parent->pid);
        }
    }

    /* TODO: Send SIGCHLD to parent */

    /*
     * Schedule next process.
     * For now, we just halt since we don't have multiple processes yet.
     */
    log_info("PROCESS: No other processes - halting");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/**
 * process_switch - Switch to another process
 * @next: Process to switch to
 */
void process_switch(process_t *next)
{
    process_t *prev = current_proc;

    if (!next || next == prev) {
        return;
    }

    /* Update TSS.rsp0 for syscall/interrupt entry */
    gdt_set_tss_rsp0(next->kernel_stack_top);

    /* Update per-CPU kernel stack pointer */
    percpu_set_kernel_stack(next->kernel_stack_top);

    /* Switch address space if different */
    if (!prev || next->pml4_phys != prev->pml4_phys) {
        vmm_switch_address_space(next->pml4_phys);
    }

    /* Update current process */
    current_proc = next;

    /*
     * Context switch happens via the underlying kthread.
     * The caller should handle kthread switching separately.
     */
}

/*
 * File descriptor operations
 */

/**
 * process_fd_alloc - Allocate a file descriptor
 * @proc: Process to allocate in
 */
int process_fd_alloc(process_t *proc)
{
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        if (proc->fd_table[i].file == NULL) {
            return i;
        }
    }
    return -1;  /* Table full */
}

/**
 * process_fd_free - Free a file descriptor
 * @proc: Process to free from
 * @fd: File descriptor to free
 */
void process_fd_free(process_t *proc, int fd)
{
    if (fd >= 0 && fd < PROC_MAX_FDS) {
        proc->fd_table[fd].file = NULL;
        proc->fd_table[fd].flags = 0;
    }
}
