// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synchronization primitives
 *
 * Spinlocks use GCC atomic builtins for lock-free synchronization.
 * Mutexes and condition variables use wait queues with sleep/wake.
 */

#include "sync.h"
#include "kthread.h"
#include "log.h"

/**
 * spin_init - Initialize a spinlock
 * @lock: spinlock to initialize
 */
void spin_init(spinlock_t *lock)
{
	lock->locked = 0;
}

/**
 * spin_lock - Acquire spinlock
 * @lock: spinlock to acquire
 */
void spin_lock(spinlock_t *lock)
{
	while (__sync_lock_test_and_set(&lock->locked, 1))
		__asm__ volatile("pause");
}

/**
 * spin_unlock - Release spinlock
 * @lock: spinlock to release
 */
void spin_unlock(spinlock_t *lock)
{
	__sync_lock_release(&lock->locked);
}

/**
 * spin_trylock - Try to acquire spinlock without blocking
 * @lock: spinlock to try
 *
 * Return: 1 if acquired, 0 if already held
 */
int spin_trylock(spinlock_t *lock)
{
	return __sync_lock_test_and_set(&lock->locked, 1) == 0;
}

/**
 * spin_lock_irqsave - Acquire spinlock with interrupt disable
 * @lock: spinlock to acquire
 *
 * Return: saved interrupt flags
 */
irqflags_t spin_lock_irqsave(spinlock_t *lock)
{
	irqflags_t flags = irq_save();

	while (__sync_lock_test_and_set(&lock->locked, 1))
		__asm__ volatile("pause");
	return flags;
}

/**
 * spin_unlock_irqrestore - Release spinlock and restore interrupts
 * @lock: spinlock to release
 * @flags: interrupt flags from spin_lock_irqsave()
 */
void spin_unlock_irqrestore(spinlock_t *lock, irqflags_t flags)
{
	__sync_lock_release(&lock->locked);
	irq_restore(flags);
}

static void waitq_add(kthread_t **head, kthread_t **tail, kthread_t *thread)
{
	thread->wait_next = NULL;
	if (*tail == NULL) {
		*head = thread;
		*tail = thread;
	} else {
		(*tail)->wait_next = thread;
		*tail = thread;
	}
}

static kthread_t *waitq_remove(kthread_t **head, kthread_t **tail)
{
	kthread_t *thread;

	if (*head == NULL)
		return NULL;

	thread = *head;
	*head = thread->wait_next;
	if (*head == NULL)
		*tail = NULL;
	thread->wait_next = NULL;
	return thread;
}

/**
 * mutex_init - Initialize a mutex
 * @m: mutex to initialize
 */
void mutex_init(mutex_t *m)
{
	m->locked = 0;
	m->owner = NULL;
	m->wait_head = NULL;
	m->wait_tail = NULL;
}

/**
 * mutex_lock - Acquire mutex, sleeping if necessary
 * @m: mutex to acquire
 */
void mutex_lock(mutex_t *m)
{
	preempt_disable();

	while (!__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
		waitq_add(&m->wait_head, &m->wait_tail, kthread_current());
		preempt_enable();
		kthread_block();
		preempt_disable();
	}

	m->owner = kthread_current();
	preempt_enable();
}

/**
 * mutex_unlock - Release mutex
 * @m: mutex to release
 */
void mutex_unlock(mutex_t *m)
{
	kthread_t *waiter;

	preempt_disable();

	m->locked = 0;
	m->owner = NULL;

	waiter = waitq_remove(&m->wait_head, &m->wait_tail);
	if (waiter != NULL)
		kthread_unblock(waiter);

	preempt_enable();
}

/**
 * mutex_trylock - Try to acquire mutex without blocking
 * @m: mutex to try
 *
 * Return: 1 if acquired, 0 if already held
 */
int mutex_trylock(mutex_t *m)
{
	preempt_disable();

	if (!__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
		preempt_enable();
		return 0;
	}

	m->owner = kthread_current();
	preempt_enable();
	return 1;
}

/**
 * cond_init - Initialize a condition variable
 * @c: condition variable to initialize
 */
void cond_init(cond_t *c)
{
	c->wait_head = NULL;
	c->wait_tail = NULL;
}

/**
 * cond_wait - Wait for condition to be signaled
 * @c: condition variable
 * @m: mutex (must be held, will be released while waiting)
 */
void cond_wait(cond_t *c, mutex_t *m)
{
	kthread_t *mutex_waiter;

	preempt_disable();

	waitq_add(&c->wait_head, &c->wait_tail, kthread_current());

	m->locked = 0;
	m->owner = NULL;

	mutex_waiter = waitq_remove(&m->wait_head, &m->wait_tail);
	if (mutex_waiter != NULL)
		kthread_unblock(mutex_waiter);

	preempt_enable();
	kthread_block();

	preempt_disable();
	while (m->locked) {
		waitq_add(&m->wait_head, &m->wait_tail, kthread_current());
		preempt_enable();
		kthread_block();
		preempt_disable();
	}
	m->locked = 1;
	m->owner = kthread_current();
	preempt_enable();
}

/**
 * cond_signal - Wake one waiting thread
 * @c: condition variable
 */
void cond_signal(cond_t *c)
{
	kthread_t *waiter;

	preempt_disable();

	waiter = waitq_remove(&c->wait_head, &c->wait_tail);
	if (waiter != NULL)
		kthread_unblock(waiter);

	preempt_enable();
}

/**
 * cond_broadcast - Wake all waiting threads
 * @c: condition variable
 */
void cond_broadcast(cond_t *c)
{
	kthread_t *waiter;

	preempt_disable();

	while ((waiter = waitq_remove(&c->wait_head, &c->wait_tail)) != NULL)
		kthread_unblock(waiter);

	preempt_enable();
}
