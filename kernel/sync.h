/*
 * sync.h - Synchronization Primitives
 *
 * Provides spinlocks, mutexes, and condition variables for thread
 * synchronization in kernel space.
 *
 * IMPORTANT USAGE NOTES:
 *
 * 1. Spinlock Deadlock Prevention:
 *    If a spinlock may be acquired from BOTH normal code AND interrupt
 *    handlers (ISRs), you MUST use spin_lock_irqsave/spin_unlock_irqrestore.
 *    Otherwise, if an interrupt fires while the lock is held and the ISR
 *    tries to acquire the same lock, the system will deadlock forever.
 *
 * 2. Mutex vs Spinlock:
 *    - Use spinlocks for very short critical sections (< 1 microsecond)
 *    - Use mutexes when the critical section may block or take longer
 *    - Mutexes CANNOT be used in interrupt context (they may sleep)
 *
 * 3. Condition Variable Pattern:
 *    Always use cond_wait() inside a while loop checking your condition:
 *      while (!condition) {
 *          cond_wait(&cv, &mutex);
 *      }
 *    This handles spurious wakeups correctly.
 */

#ifndef SYNC_H
#define SYNC_H

#include "kthread.h"

/* =============================================================================
 * Interrupt Flag Helpers
 *
 * Used by spinlocks to save/restore interrupt state, preventing deadlock
 * when an interrupt handler tries to acquire a lock held by interrupted code.
 *
 * irq_save():    Saves current interrupt state and DISABLES interrupts.
 * irq_restore(): Restores previous interrupt state (may re-enable interrupts).
 * =============================================================================
 */

typedef uint64_t irqflags_t;

static inline irqflags_t irq_save(void) {
    irqflags_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(irqflags_t flags) {
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

/* =============================================================================
 * Spinlock - Busy-wait synchronization
 *
 * Use for very short critical sections (< 1 microsecond).
 * Thread spins (burns CPU) while waiting for lock.
 *
 * spin_lock/unlock: Use when you KNOW interrupts are already disabled
 *                   or the lock is never taken in interrupt context.
 *
 * spin_lock_irqsave/unlock_irqrestore: Use when lock may be taken from
 *                   both process and interrupt context. Saves and restores
 *                   interrupt state to prevent deadlock.
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

/* Interrupt-safe variants - use these when lock may be taken in ISR context */
irqflags_t spin_lock_irqsave(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, irqflags_t flags);

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
