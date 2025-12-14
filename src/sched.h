#ifndef SCHED_H 
#define SCHED_H

#include "process.h"
#include "idt.h"

/* Initialize the scheduler */
void sched_init(void);

/* Add a process to the ready queue */
void sched_add(struct process *p);

/* Remove a process from the ready queue */
void sched_remove(struct process *p);

/* Pick next process and switch to it (called from timer) */
void schedule(struct interrupt_frame *frame);

/* Yield CPU voluntarily */
void sched_yield(void);

/* Block current process waiting for a child PID */
void sched_block_on_pid(struct process *p, int pid);

/* Wake up any process blocked waiting for the given PID */
void sched_wake_waiters(int pid);

/* Save context from interrupt frame to process */
void sched_save_context(struct process *p, struct interrupt_frame *frame);

/* Load context from process to interrupt frame */
void sched_load_context(struct process *p, struct interrupt_frame *frame);

#endif /* SCHED_H */