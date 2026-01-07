/*
 * sync.c - Synchronization Primitives Implementation
 */

#include "sync.h"
#include "kthread.h"
#include "log.h"

/* =============================================================================
 * Spinlock Implementation
 *
 * Uses GCC atomic builtins for lock-free synchronization.
 * __sync_lock_test_and_set: atomically sets to 1, returns old value
 * __sync_lock_release: atomically sets to 0
 * =============================================================================
 */

void spin_init(spinlock_t *lock) {
    lock->locked = 0;
}

void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        /* Spin until we acquire the lock */
        /* Hint to CPU that we're in a spin-wait loop */
        __asm__ volatile("pause");
    }
}

void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->locked);
}

int spin_trylock(spinlock_t *lock) {
    return __sync_lock_test_and_set(&lock->locked, 1) == 0;
}

irqflags_t spin_lock_irqsave(spinlock_t *lock) {
    irqflags_t flags = irq_save();
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        /* Spin until we acquire the lock */
        __asm__ volatile("pause");
    }
    return flags;
}

void spin_unlock_irqrestore(spinlock_t *lock, irqflags_t flags) {
    __sync_lock_release(&lock->locked);
    irq_restore(flags);
}

/* =============================================================================
 * Wait Queue Helpers
 *
 * Add/remove threads from wait queues (used by mutex and condvar).
 * =============================================================================
 */

static void waitq_add(kthread_t **head, kthread_t **tail, kthread_t *thread) {
    thread->wait_next = NULL;
    if (*tail == NULL) {
        *head = thread;
        *tail = thread;
    } else {
        (*tail)->wait_next = thread;
        *tail = thread;
    }
}

static kthread_t *waitq_remove(kthread_t **head, kthread_t **tail) {
    if (*head == NULL) {
        return NULL;
    }
    kthread_t *thread = *head;
    *head = thread->wait_next;
    if (*head == NULL) {
        *tail = NULL;
    }
    thread->wait_next = NULL;
    return thread;
}

/* =============================================================================
 * Mutex Implementation
 *
 * If lock is free, acquire it immediately.
 * If lock is held, add to wait queue and sleep.
 * =============================================================================
 */

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner = NULL;
    m->wait_head = NULL;
    m->wait_tail = NULL;
}

void mutex_lock(mutex_t *m) {
    preempt_disable();

    /* Use atomic compare-and-swap to avoid race between check and set */
    while (!__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
        /* Lock is held - add ourselves to wait queue and sleep */
        waitq_add(&m->wait_head, &m->wait_tail, kthread_current());
        preempt_enable();   /* Must enable before blocking! */
        kthread_block();
        preempt_disable();  /* Re-disable to safely retry CAS */
    }

    /* Acquired the lock */
    m->owner = kthread_current();

    preempt_enable();
}

void mutex_unlock(mutex_t *m) {
    preempt_disable();

    m->locked = 0;
    m->owner = NULL;

    /* Wake the first waiter if any */
    kthread_t *waiter = waitq_remove(&m->wait_head, &m->wait_tail);
    if (waiter != NULL) {
        kthread_unblock(waiter);
    }

    preempt_enable();
}

int mutex_trylock(mutex_t *m) {
    preempt_disable();

    /* Atomic compare-and-swap: try to set locked from 0 to 1 */
    if (!__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
        preempt_enable();
        return 0;  /* Failed to acquire */
    }

    m->owner = kthread_current();

    preempt_enable();
    return 1;  /* Acquired */
}

/* =============================================================================
 * Condition Variable Implementation
 *
 * cond_wait: release mutex, sleep, re-acquire mutex when woken
 * cond_signal: wake one waiter
 * cond_broadcast: wake all waiters
 * =============================================================================
 */

void cond_init(cond_t *c) {
    c->wait_head = NULL;
    c->wait_tail = NULL;
}

void cond_wait(cond_t *c, mutex_t *m) {
    preempt_disable();

    /* Add ourselves to condition's wait queue */
    waitq_add(&c->wait_head, &c->wait_tail, kthread_current());

    /* Release the mutex so other threads can make progress */
    m->locked = 0;
    m->owner = NULL;

    /* Wake a mutex waiter if any */
    kthread_t *mutex_waiter = waitq_remove(&m->wait_head, &m->wait_tail);
    if (mutex_waiter != NULL) {
        kthread_unblock(mutex_waiter);
    }

    /* Sleep until signaled */
    preempt_enable();   /* Must enable before blocking! */
    kthread_block();

    /* Re-acquire the mutex before returning */
    preempt_disable();
    while (m->locked) {
        waitq_add(&m->wait_head, &m->wait_tail, kthread_current());
        preempt_enable();   /* Must enable before blocking! */
        kthread_block();
        preempt_disable();
    }
    m->locked = 1;
    m->owner = kthread_current();

    preempt_enable();
}

void cond_signal(cond_t *c) {
    preempt_disable();

    kthread_t *waiter = waitq_remove(&c->wait_head, &c->wait_tail);
    if (waiter != NULL) {
        kthread_unblock(waiter);
    }

    preempt_enable();
}

void cond_broadcast(cond_t *c) {
    preempt_disable();

    /* Wake all waiters */
    kthread_t *waiter;
    while ((waiter = waitq_remove(&c->wait_head, &c->wait_tail)) != NULL) {
        kthread_unblock(waiter);
    }

    preempt_enable();
}
