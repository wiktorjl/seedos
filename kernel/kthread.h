/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel threading
 *
 * Cooperative and preemptive kernel threads with context switching,
 * sleep/wake mechanisms, and preemption control.
 *
 * Thread IDs are monotonic and never reused. When a thread exits and is
 * reaped, its ID remains permanently retired.
 *
 * Threads that exit are marked THREAD_EXITED but their memory is not freed
 * automatically. Call kthread_reap() periodically to reclaim resources.
 *
 * kthread_sleep() has 10ms granularity (APIC timer at 100Hz). Requested
 * sleep times are rounded up to the next 10ms boundary.
 *
 * When using kthread_block() with preempt_disable(), you must call
 * preempt_enable() before kthread_block() or the system will hang.
 */

#ifndef _KTHREAD_H
#define _KTHREAD_H

#include "types.h"

#define KTHREAD_STACK_SIZE 16384

typedef enum {
	THREAD_READY,
	THREAD_RUNNING,
	THREAD_BLOCKED,
	THREAD_EXITED
} thread_state_t;

/**
 * struct kthread - Thread control block
 * @name: human-readable name for debugging
 * @id: unique monotonic ID (never reused)
 * @rsp: saved stack pointer for context switch
 * @stack_base: base of allocated stack
 * @state: current thread state
 * @next: global thread list linkage
 * @wait_next: wait queue linkage
 * @entry: thread entry point function
 * @arg: argument passed to entry function
 * @wake_tick: timer tick when sleep should end
 */
typedef struct kthread {
	const char *name;
	uint64_t id;
	uint64_t rsp;
	void *stack_base;
	thread_state_t state;
	struct kthread *next;
	struct kthread *wait_next;
	void (*entry)(void *);
	void *arg;
	uint64_t wake_tick;
} kthread_t;

/**
 * kthread_init - Initialize the threading subsystem
 *
 * Wraps the current execution context as the genesis thread (ID 0).
 * Must be called before creating any other threads.
 */
void kthread_init(void);

/**
 * kthread_create - Create a new kernel thread
 * @name: human-readable name (pointer must remain valid)
 * @entry: function to execute
 * @arg: argument passed to entry function
 *
 * Return: thread ID (> 0) on success, 0 on failure
 */
uint64_t kthread_create(const char *name, void (*entry)(void *), void *arg);

/**
 * kthread_exit - Terminate the current thread
 *
 * Does not return. Resources freed by kthread_reap().
 */
void kthread_exit(void);

/**
 * kthread_reap - Clean up exited threads
 *
 * Removes THREAD_EXITED threads from list and frees their resources.
 */
void kthread_reap(void);

kthread_t *kthread_current(void);
void kthread_set_current(kthread_t *kthread);
kthread_t *kthread_get_kthread(uint64_t kthread_id);
void kthreads_list(void);

/**
 * kthread_switch - Low-level context switch
 * @old_rsp: pointer to save current stack pointer
 * @new_rsp: stack pointer to switch to
 */
void kthread_switch(uint64_t *old_rsp, uint64_t new_rsp);

/**
 * kthread_yield - Voluntarily give up the CPU
 */
void kthread_yield(void);

/**
 * kthread_schedule - Run the scheduler
 *
 * Finds next READY thread and switches to it.
 * Does nothing if preemption is disabled.
 */
void kthread_schedule(void);

/**
 * kthread_sleep - Sleep for approximately the given milliseconds
 * @ms: minimum sleep duration (rounded up to 10ms granularity)
 */
void kthread_sleep(uint64_t ms);

/**
 * kthread_wake_sleepers - Wake threads whose sleep time expired
 *
 * Called from timer interrupt.
 */
void kthread_wake_sleepers(void);

void preempt_disable(void);
void preempt_enable(void);
int preempt_enabled(void);

/**
 * kthread_block - Block current thread until explicitly unblocked
 *
 * Preemption must be enabled before calling.
 */
void kthread_block(void);

/**
 * kthread_unblock - Wake a blocked thread
 * @thread: thread to unblock
 */
void kthread_unblock(kthread_t *thread);

#endif /* _KTHREAD_H */
