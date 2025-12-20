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

/* Save context from interrupt frame to process */
void sched_save_context(struct process *p, struct interrupt_frame *frame);

/* Load context from process to interrupt frame */
void sched_load_context(struct process *p, struct interrupt_frame *frame);

#endif /* SCHED_H */