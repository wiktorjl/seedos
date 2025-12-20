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
    p->saved_cs = frame->cs;
    p->saved_ss = frame->ss;
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
    frame->cs = p->saved_cs;
    frame->ss = p->saved_ss;
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
}

/*
 * schedule - Round-robin process scheduler.
 *
 * Called from the timer interrupt handler when a process was interrupted
 * in userspace. Saves the current process context and switches to the
 * next ready process.
 *
 * IMPORTANT: This function must ONLY be called when the interrupted code
 * was in userspace (ring 3). The caller (timer handler in idt.c) must
 * verify this before calling schedule().
 *
 * @frame: Interrupt frame containing saved CPU state
 */
void schedule(struct interrupt_frame *frame) {
    if(ready_count == 0) return;

    struct process *current = process_get_current();
    int was_running = (current && current->state == PROC_RUNNING);

    /* Save current process context */
    if(was_running) {
        sched_save_context(current, frame);
        current->state = PROC_READY;
    }

    /* Round-robin: pick next ready process */
    current_index = (current_index + 1) % ready_count;
    struct process *next = ready_queue[current_index];

    /* Switch to next process if different from current */
    if(next != current || !was_running) {
        /* Check if the process has valid saved context */
        if(!next->context_valid) {
            /* Process has no valid context - skip it */
            next->state = PROC_READY;
            return;
        }

        /* Load next process context */
        sched_load_context(next, frame);
        next->state = PROC_RUNNING;
        process_set_current(next);

        /* Switch address space */
        vmm_switch_address_space(next->pml4);
    }
}

/* Dead code removed:
 * - sched_yield() - was never called
 * - sched_block_on_pid() - was never called
 * - sched_wake_waiters() - was called but always no-op
 *
 * These will be properly implemented when per-process kernel stacks are added.
 */

