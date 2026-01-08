/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Synchronization primitives
 *
 * Spinlocks, mutexes, and condition variables for kernel threads.
 *
 * Spinlock deadlock prevention: If a spinlock may be acquired from both
 * normal code and interrupt handlers, use spin_lock_irqsave/unlock_irqrestore.
 *
 * Mutex vs spinlock: Use spinlocks for very short critical sections
 * (< 1us). Use mutexes when the critical section may block or take longer.
 * Mutexes cannot be used in interrupt context.
 *
 * Condition variable pattern: Always use cond_wait() inside a while loop:
 *   while (!condition)
 *       cond_wait(&cv, &mutex);
 */

#ifndef _SYNC_H
#define _SYNC_H

#include "kthread.h"

typedef uint64_t irqflags_t;

/**
 * irq_save - Save interrupt state and disable interrupts
 *
 * Return: saved interrupt flags
 */
static inline irqflags_t irq_save(void)
{
	irqflags_t flags;
	__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
	return flags;
}

/**
 * irq_restore - Restore interrupt state
 * @flags: saved interrupt flags from irq_save()
 */
static inline void irq_restore(irqflags_t flags)
{
	__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

/**
 * struct spinlock - Busy-wait lock
 * @locked: lock state (0 = unlocked, 1 = locked)
 */
typedef struct {
	volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT { .locked = 0 }

void spin_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
int spin_trylock(spinlock_t *lock);
irqflags_t spin_lock_irqsave(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, irqflags_t flags);

/**
 * struct mutex - Sleep-wait lock
 * @locked: lock state
 * @owner: thread holding the lock
 * @wait_head: head of wait queue
 * @wait_tail: tail of wait queue
 */
typedef struct {
	volatile int locked;
	kthread_t *owner;
	kthread_t *wait_head;
	kthread_t *wait_tail;
} mutex_t;

#define MUTEX_INIT { .locked = 0, .owner = NULL, .wait_head = NULL, .wait_tail = NULL }

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
int mutex_trylock(mutex_t *m);

/**
 * struct cond - Condition variable
 * @wait_head: head of wait queue
 * @wait_tail: tail of wait queue
 */
typedef struct {
	kthread_t *wait_head;
	kthread_t *wait_tail;
} cond_t;

#define COND_INIT { .wait_head = NULL, .wait_tail = NULL }

void cond_init(cond_t *c);
void cond_wait(cond_t *c, mutex_t *m);
void cond_signal(cond_t *c);
void cond_broadcast(cond_t *c);

#endif /* _SYNC_H */
