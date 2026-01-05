/*
 * sync.h - Synchronization Primitives
 *
 * Provides spinlocks, mutexes, and condition variables for thread
 * synchronization in kernel space.
 */

#ifndef SYNC_H
#define SYNC_H

#include "kthread.h"

/* =============================================================================
 * Spinlock - Busy-wait synchronization
 *
 * Use for very short critical sections (< 1 microsecond).
 * Thread spins (burns CPU) while waiting for lock.
 * =============================================================================
 */

typedef struct {
    volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT { .locked = 0 }

void spin_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
int spin_trylock(spinlock_t *lock);

/* =============================================================================
 * Mutex - Sleep-wait synchronization
 *
 * Use for longer critical sections.
 * Thread sleeps while waiting, doesn't burn CPU.
 * =============================================================================
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

/* =============================================================================
 * Condition Variable - Wait for a condition to become true
 *
 * Used with a mutex. Thread waits until signaled by another thread.
 * Classic use case: producer/consumer queues.
 * =============================================================================
 */

typedef struct {
    kthread_t *wait_head;
    kthread_t *wait_tail;
} cond_t;

#define COND_INIT { .wait_head = NULL, .wait_tail = NULL }

void cond_init(cond_t *c);
void cond_wait(cond_t *c, mutex_t *m);   /* Wait for signal (releases mutex while waiting) */
void cond_signal(cond_t *c);              /* Wake one waiting thread */
void cond_broadcast(cond_t *c);           /* Wake all waiting threads */

#endif /* SYNC_H */
