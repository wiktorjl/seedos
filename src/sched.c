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

static struct process *ready_queue[MAX_PROCESSES];
static int ready_count = 0;
static int current_index = -1;

void sched_init(void) {
    ready_count = 0;
    current_index = -1;
}

void sched_add(struct process *p) {
    if (ready_count >= MAX_PROCESSES) return;
    ready_queue[ready_count++] = p;
    p->state = PROC_READY;
}

void sched_remove(struct process *p) {
    for (int i = 0; i < ready_count; i++) {
        if (ready_queue[i] == p) {
            /* Shift remaining entries */
            for (int j = i; j < ready_count - 1; j++) {
                ready_queue[j] = ready_queue[j + 1];
            }
            ready_count--;
            if (current_index >= ready_count) {
                current_index = ready_count - 1;
            }
            return;
        }
    }
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
    if (ready_count == 0) return;

    struct process *current = process_get_current();
    int was_running = (current && current->state == PROC_RUNNING);

    /* Save current process context if it was actually running */
    if (was_running) {
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
    if (next != current || !was_running) {
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

