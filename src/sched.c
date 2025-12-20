/*
* sched.c - Round-robin Process Scheduler
*/
#include "sched.h"
#include "vmm.h"
#include "console.h"
#include "process.h"
#include "gdt.h"

/* User segment selectors with RPL=3 */
#define USER_CS  (GDT_USER_CODE | 3)  /* 0x1B */
#define USER_SS  (GDT_USER_DATA | 3)  /* 0x23 */

/*
 * Interrupt-safe critical sections.
 * Save flags, disable interrupts, restore flags when done.
 */
static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    __asm__ volatile("push %0; popfq" :: "r"(flags) : "memory");
}

static struct process *ready_queue[MAX_PROCESSES];
static int ready_count = 0;
static int current_index = -1;

/*
 * kernel_preempt_ok - Flag to allow preemption during kernel I/O wait.
 *
 * When a syscall is waiting for I/O (e.g., keyboard input), it sets this
 * flag to indicate that it's safe to preempt even though we're in kernel
 * mode. This allows background processes to run while the shell waits.
 */
volatile int kernel_preempt_ok = 0;

void sched_init(void) {
    ready_count = 0;
    current_index = -1;
}

void sched_add(struct process *p) {
    uint64_t flags = irq_save();
    if(ready_count >= MAX_PROCESSES) {
        irq_restore(flags);
        return;
    }
    ready_queue[ready_count++] = p;
    p->state = PROC_READY;
    irq_restore(flags);
}

void sched_remove(struct process *p) {
    uint64_t flags = irq_save();
    for(int i = 0; i < ready_count; i++) {
        if(ready_queue[i] == p) {
            /* Shift remaining entries */
            for(int j = i; j < ready_count - 1; j++) {
                ready_queue[j] = ready_queue[j + 1];
            }
            ready_count--;
            if(current_index >= ready_count) {
                current_index = ready_count - 1;
            }
            irq_restore(flags);
            return;
        }
    }
    irq_restore(flags);
}

void sched_save_context(struct process *p, struct interrupt_frame *frame) {
    p->saved_rip = frame->rip;
    p->saved_rsp = frame->rsp;
    p->saved_rflags = frame->rflags;
    p->saved_rax = frame->rax;
    p->saved_rbx = frame->rbx;
    p->saved_rcx = frame->rcx;
    p->saved_rdx = frame->rdx;
    p->saved_rsi = frame->rsi;
    p->saved_rdi = frame->rdi;
    p->saved_rbp = frame->rbp;
    p->saved_r8 = frame->r8;
    p->saved_r9 = frame->r9;
    p->saved_r10 = frame->r10;
    p->saved_r11 = frame->r11;
    p->saved_r12 = frame->r12;
    p->saved_r13 = frame->r13;
    p->saved_r14 = frame->r14;
    p->saved_r15 = frame->r15;
}

void sched_load_context(struct process *p, struct interrupt_frame *frame) {
    frame->rip = p->saved_rip;
    frame->rsp = p->saved_rsp;
    frame->rflags = p->saved_rflags;
    frame->rax = p->saved_rax;
    frame->rbx = p->saved_rbx;
    frame->rcx = p->saved_rcx;
    frame->rdx = p->saved_rdx;
    frame->rsi = p->saved_rsi;
    frame->rdi = p->saved_rdi;
    frame->rbp = p->saved_rbp;
    frame->r8 = p->saved_r8;
    frame->r9 = p->saved_r9;
    frame->r10 = p->saved_r10;
    frame->r11 = p->saved_r11;
    frame->r12 = p->saved_r12;
    frame->r13 = p->saved_r13;
    frame->r14 = p->saved_r14;
    frame->r15 = p->saved_r15;

    /* Set segment selectors for userspace return */
    frame->cs = USER_CS;
    frame->ss = USER_SS;
}

void schedule(struct interrupt_frame *frame) {
    if(ready_count == 0) return;

    struct process *current = process_get_current();
    int was_running = (current && current->state == PROC_RUNNING);

    /* Save current process context if it was actually running */
    if(was_running) {
        sched_save_context(current, frame);
        current->state = PROC_READY;
    }

    /* Round-robin: pick next ready process */
    current_index = (current_index + 1) % ready_count;
    struct process *next = ready_queue[current_index];

    /*
     * Load context if:
     * - Switching to a different process, OR
     * - Current process wasn't running (e.g., starting from kernel idle)
     */
    if(next != current || !was_running) {
        /* Load next process context */
        sched_load_context(next, frame);
        next->state = PROC_RUNNING;
        process_set_current(next);

        /* Switch address space */
        vmm_switch_address_space(next->pml4);
    }
}

void sched_yield(void) {
    /* For now, just busy-wait - will be improved later */
    asm volatile("hlt");
}

void sched_block_on_pid(struct process *p, int pid) {
    if(p == NULL) return;

    uint64_t flags = irq_save();
    /* Remove from ready queue (sched_remove handles its own locking, but
     * we need atomicity for the whole block operation) */
    for(int i = 0; i < ready_count; i++) {
        if(ready_queue[i] == p) {
            for(int j = i; j < ready_count - 1; j++) {
                ready_queue[j] = ready_queue[j + 1];
            }
            ready_count--;
            if(current_index >= ready_count) {
                current_index = ready_count - 1;
            }
            break;
        }
    }
    /* Mark as blocked and record what we're waiting for */
    p->state = PROC_BLOCKED;
    p->wait_pid = pid;
    irq_restore(flags);
}

void sched_wake_waiters(int pid) {
    uint64_t flags = irq_save();
    /* Find any process blocked waiting for this PID */
    struct process *waiter = process_find_blocked_on_pid(pid);

    if(waiter != NULL) {
        /* Wake up the waiter */
        waiter->wait_pid = 0;
        /* Inline sched_add to avoid nested locking */
        if(ready_count < MAX_PROCESSES) {
            ready_queue[ready_count++] = waiter;
            waiter->state = PROC_READY;
        }
    }
    irq_restore(flags);
}

