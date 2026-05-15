// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel threading
 *
 * Implements kernel threads with cooperative and preemptive scheduling.
 * See kthread.h for API documentation and usage notes.
 */

#include "kthread.h"
#include "heap.h"
#include "log.h"
#include "pmm.h"
#include "apic.h"
#include <stdint.h>

extern void kthread_switch(uint64_t *old_rsp, uint64_t new_rsp);

kthread_t genesis_kthread;
kthread_t *current_kthread;

static volatile uint64_t next_thread_id = 1;
static volatile int preempt_count;

/**
 * preempt_disable - Prevent context switches
 */
void preempt_disable(void)
{
	__sync_fetch_and_add(&preempt_count, 1);
}

/**
 * preempt_enable - Allow context switches
 */
void preempt_enable(void)
{
	__sync_fetch_and_sub(&preempt_count, 1);
}

/**
 * preempt_enabled - Check if preemption is allowed
 *
 * Return: non-zero if preemption enabled, 0 if disabled
 */
int preempt_enabled(void)
{
	return __sync_fetch_and_add(&preempt_count, 0) == 0;
}

/**
 * kthread_exit - Terminate the current thread
 */
void kthread_exit(void)
{
	kthread_t *exiting = current_kthread;

	current_kthread->state = THREAD_EXITED;
	kthread_set_current(&genesis_kthread);
	kthread_switch(&exiting->rsp, genesis_kthread.rsp);
}

/**
 * kthread_current - Get the currently running thread
 *
 * Return: pointer to current thread
 */
kthread_t *kthread_current(void)
{
	return current_kthread;
}

/**
 * kthread_set_current - Set the current thread pointer
 * @kthread: thread to set as current
 */
void kthread_set_current(kthread_t *kthread)
{
	current_kthread = kthread;
}

/**
 * kthread_get_kthread - Find thread by ID
 * @kthread_id: thread ID to find
 *
 * Return: pointer to thread, or NULL if not found
 */
kthread_t *kthread_get_kthread(uint64_t kthread_id)
{
	kthread_t *iter = &genesis_kthread;

	while (iter != NULL) {
		if (iter->id == kthread_id)
			return iter;
		iter = iter->next;
	}
	return NULL;
}

static void kthread_reaper_task(void *arg)
{
	(void)arg;
	for (;;) {
		kthread_sleep(100);
		kthread_reap();
	}
}

/**
 * kthread_init - Initialize the threading subsystem
 */
void kthread_init(void)
{
	genesis_kthread.id = 0;
	genesis_kthread.name = "genesis-kthread-0";
	genesis_kthread.next = NULL;
	genesis_kthread.rsp = 0;
	genesis_kthread.stack_base = NULL;
	genesis_kthread.state = THREAD_RUNNING;
	genesis_kthread.entry = (void *)0x1337;
	genesis_kthread.arg = NULL;
	current_kthread = &genesis_kthread;

	log_debug("Wrapped initial execution in a kthread: %s",
		  current_kthread->name);

	kthread_create("kreaper", kthread_reaper_task, NULL);
}

void kthread_trampoline(void)
{
	kthread_t *self = kthread_current();

	self->entry(self->arg);
	kthread_exit();
}

/**
 * kthread_create - Create a new kernel thread
 * @kthread_friendly_name: human-readable name
 * @kthread_entry_point: function to execute
 * @arg: argument passed to entry function
 *
 * Return: thread ID (> 0) on success, 0 on failure
 */
uint64_t kthread_create(const char *kthread_friendly_name,
			void (*kthread_entry_point)(void *), void *arg)
{
	kthread_t *new_thread;
	uint64_t stack_top;
	uint64_t *stack_ptr;
	kthread_t *iter;

	preempt_disable();

	new_thread = (kthread_t *)kmalloc(sizeof(kthread_t));
	if (new_thread == NULL) {
		log_error("KTHREAD: Failed to allocate memory for new thread");
		preempt_enable();
		return 0;
	}

	new_thread->id = __sync_fetch_and_add(&next_thread_id, 1);
	new_thread->name = kthread_friendly_name;
	new_thread->state = THREAD_READY;
	new_thread->wake_tick = 0;
	new_thread->wait_next = NULL;
	new_thread->stack_base = kmalloc(KTHREAD_STACK_SIZE);

	if (new_thread->stack_base == NULL) {
		log_error("KTHREAD: Failed to allocate stack");
		kfree(new_thread);
		preempt_enable();
		return 0;
	}

	stack_top = (uint64_t)new_thread->stack_base + KTHREAD_STACK_SIZE;
	stack_top &= ~0xF;
	new_thread->rsp = stack_top;
	new_thread->next = NULL;

	if (current_kthread == NULL) {
		current_kthread = new_thread;
		log_panic("KTHREAD: No current thread");
	} else {
		iter = &genesis_kthread;
		while (iter->next != NULL)
			iter = iter->next;
		iter->next = new_thread;
	}

	new_thread->entry = kthread_entry_point;
	new_thread->arg = arg;

	stack_ptr = (uint64_t *)new_thread->rsp;
	*(--stack_ptr) = (uint64_t)kthread_trampoline;

	for (int i = 0; i < 4; i++)
		*(--stack_ptr) = 0;
	for (int i = 0; i < 2; i++)
		*(--stack_ptr) = 0;
	new_thread->rsp = (uint64_t)stack_ptr;

	log_debug("KTHREAD: Created thread: %s (ID: %llu)",
		  new_thread->name, new_thread->id);
	log_debug("KTHREAD: stack_base=%p, rsp=%p",
		  new_thread->stack_base, (void *)new_thread->rsp);

	preempt_enable();
	return new_thread->id;
}

/**
 * kthread_yield - Voluntarily give up the CPU
 */
void kthread_yield(void)
{
	current_kthread->state = THREAD_READY;
	kthread_schedule();
}

/**
 * kthread_schedule - Run the scheduler
 */
void kthread_schedule(void)
{
	kthread_t *t;
	kthread_t *next_thread;
	uint64_t now;

	if (preempt_count > 0)
		return;

	now = apic_get_ticks();
	t = &genesis_kthread;
	while (t != NULL) {
		if (t->state == THREAD_BLOCKED && t->wake_tick != 0 &&
		    t->wake_tick <= now) {
			t->state = THREAD_READY;
			t->wake_tick = 0;
		}
		t = t->next;
	}

	next_thread = current_kthread->next;
	while (next_thread != NULL && next_thread->state != THREAD_READY)
		next_thread = next_thread->next;

	if (next_thread == NULL) {
		next_thread = &genesis_kthread;
		while (next_thread != current_kthread &&
		       next_thread->state != THREAD_READY)
			next_thread = next_thread->next;
	}

	if (next_thread != NULL && next_thread != current_kthread) {
		kthread_t *old_thread = current_kthread;

		if (old_thread->state == THREAD_RUNNING)
			old_thread->state = THREAD_READY;

		current_kthread = next_thread;
		current_kthread->state = THREAD_RUNNING;
		kthread_switch(&old_thread->rsp, current_kthread->rsp);
	} else if (current_kthread->state == THREAD_BLOCKED) {
		while (current_kthread->state == THREAD_BLOCKED) {
			__asm__ volatile("sti; hlt");

			now = apic_get_ticks();
			t = &genesis_kthread;
			while (t != NULL) {
				if (t->state == THREAD_BLOCKED &&
				    t->wake_tick != 0 && t->wake_tick <= now) {
					t->state = THREAD_READY;
					t->wake_tick = 0;
				}
				t = t->next;
			}

			if (current_kthread->state == THREAD_BLOCKED) {
				t = &genesis_kthread;
				while (t != NULL) {
					if (t->state == THREAD_READY) {
						kthread_t *old = current_kthread;
						current_kthread = t;
						current_kthread->state = THREAD_RUNNING;
						kthread_switch(&old->rsp,
							       current_kthread->rsp);
						return;
					}
					t = t->next;
				}
			}
		}
		current_kthread->state = THREAD_RUNNING;
	}
}

/**
 * kthread_sleep - Sleep for approximately the given milliseconds
 * @ms: minimum sleep duration
 */
void kthread_sleep(uint64_t ms)
{
	uint64_t ticks = (ms + 9) / 10;

	current_kthread->wake_tick = apic_get_ticks() + ticks;
	current_kthread->state = THREAD_BLOCKED;
	kthread_schedule();
}

/**
 * kthread_wake_sleepers - Wake threads whose sleep time expired
 */
void kthread_wake_sleepers(void)
{
	uint64_t now = apic_get_ticks();
	kthread_t *t = &genesis_kthread;

	while (t != NULL) {
		if (t->state == THREAD_BLOCKED && t->wake_tick != 0 &&
		    t->wake_tick <= now) {
			t->state = THREAD_READY;
			t->wake_tick = 0;
		}
		t = t->next;
	}
}

/**
 * kthread_block - Block current thread until explicitly unblocked
 */
void kthread_block(void)
{
	current_kthread->state = THREAD_BLOCKED;
	current_kthread->wake_tick = 0;
	kthread_schedule();
}

/**
 * kthread_unblock - Wake a blocked thread
 * @thread: thread to unblock
 */
void kthread_unblock(kthread_t *thread)
{
	if (thread->state == THREAD_BLOCKED)
		thread->state = THREAD_READY;
}

/**
 * kthread_reap - Clean up exited threads
 */
void kthread_reap(void)
{
	kthread_t *prev;
	kthread_t *iter;
	kthread_t *to_free;

	preempt_disable();

	prev = &genesis_kthread;
	iter = genesis_kthread.next;

	while (iter != NULL) {
		if (iter->state == THREAD_EXITED) {
			to_free = iter;
			prev->next = iter->next;
			iter = iter->next;

			log_debug("KTHREAD: Reaping thread ID %llu (%s)",
				  to_free->id, to_free->name);
			if (to_free->stack_base != NULL)
				kfree(to_free->stack_base);
			kfree(to_free);
		} else {
			prev = iter;
			iter = iter->next;
		}
	}

	preempt_enable();
}

static const char *state_to_string(thread_state_t state)
{
	switch (state) {
	case THREAD_READY:
		return "READY";
	case THREAD_RUNNING:
		return "RUNNING";
	case THREAD_BLOCKED:
		return "BLOCKED";
	case THREAD_EXITED:
		return "EXITED";
	default:
		return "UNKNOWN";
	}
}

/**
 * kthreads_list - Print list of all threads
 */
void kthreads_list(void)
{
	kthread_t *t;
	int count = 0;

	preempt_disable();

	log_info("=== Kernel Threads ===");
	log_info("%-4s %-20s %-10s %s", "ID", "Name", "State", "Stack");

	t = &genesis_kthread;
	while (t != NULL) {
		log_info("%-4llu %-20s %-10s %p",
			 t->id, t->name ? t->name : "(null)",
			 state_to_string(t->state), t->stack_base);
		count++;
		t = t->next;
	}

	log_info("Total: %d threads", count);

	preempt_enable();
}
