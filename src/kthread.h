/*
 * kthread.h - Kernel Threading
 *
 * Provides cooperative and preemptive kernel threads with:
 *   - Thread creation and destruction
 *   - Context switching (see kthread_switch.S)
 *   - Sleep/wake mechanisms
 *   - Preemption control for critical sections
 *
 * IMPORTANT USAGE NOTES:
 *
 * 1. Thread IDs are Monotonic:
 *    Thread IDs are never reused. When a thread exits and is reaped, its ID
 *    remains permanently retired. This ensures that stale thread ID references
 *    can be detected (kthread_get_kthread returns NULL for reaped threads).
 *
 * 2. Memory Management:
 *    Threads that exit are marked THREAD_EXITED but their memory is NOT freed
 *    automatically. Call kthread_reap() periodically to clean up exited threads
 *    and reclaim their stack memory (16KB per thread).
 *
 * 3. Sleep Granularity:
 *    kthread_sleep() has 10ms granularity (based on APIC timer at 100Hz).
 *    Requested sleep times are rounded UP to the next 10ms boundary.
 *    Example: kthread_sleep(1) sleeps for 10ms, kthread_sleep(15) sleeps 20ms.
 *
 * 4. Preemption and Blocking:
 *    When using preempt_disable()/preempt_enable() with kthread_block():
 *    You MUST call preempt_enable() BEFORE kthread_block(), otherwise the
 *    scheduler cannot switch to another thread and the system will hang.
 */

#ifndef KTHREAD_H
#define KTHREAD_H

#include "types.h"

/* =============================================================================
 * Configuration
 * =============================================================================
 */

#define KTHREAD_STACK_SIZE 16384  /* 16KB stack per thread */

/* =============================================================================
 * Thread State
 * =============================================================================
 */

typedef enum {
    THREAD_READY,     /* Runnable, waiting to be scheduled */
    THREAD_RUNNING,   /* Currently executing on CPU */
    THREAD_BLOCKED,   /* Waiting for event (sleep, mutex, condvar) */
    THREAD_EXITED     /* Terminated, awaiting cleanup by kthread_reap() */
} thread_state_t;

/* =============================================================================
 * Thread Control Block
 * =============================================================================
 */

typedef struct kthread {
    const char *name;           /* Human-readable name for debugging */
    uint64_t id;                /* Unique monotonic ID (never reused) */
    uint64_t rsp;               /* Saved stack pointer for context switch */
    void *stack_base;           /* Base of allocated stack (for freeing) */
    thread_state_t state;       /* Current thread state */
    struct kthread *next;       /* Global thread list linkage */
    struct kthread *wait_next;  /* Wait queue linkage (mutex/condvar) */
    void (*entry)(void *);      /* Thread entry point function */
    void *arg;                  /* Argument passed to entry function */
    uint64_t wake_tick;         /* Timer tick when sleep should end (0 = not sleeping) */
} kthread_t;

/* =============================================================================
 * Initialization
 * =============================================================================
 */

/*
 * kthread_init - Initialize the threading subsystem.
 *
 * Wraps the current execution context as the "genesis" thread (ID 0).
 * Must be called before creating any other threads.
 */
void kthread_init(void);

/* =============================================================================
 * Thread Lifecycle
 * =============================================================================
 */

/*
 * kthread_create - Create a new kernel thread.
 *
 * @name:  Human-readable name for debugging (not copied, pointer must remain valid)
 * @entry: Function to execute in the new thread
 * @arg:   Argument passed to the entry function
 *
 * Returns: Thread ID (> 0) on success, 0 on failure (out of memory).
 *
 * The new thread starts in THREAD_READY state and will be scheduled
 * when the scheduler runs. Thread IDs are monotonically increasing
 * and are never reused.
 */
uint64_t kthread_create(const char *name, void (*entry)(void *), void *arg);

/*
 * kthread_exit - Terminate the current thread.
 *
 * Marks the thread as THREAD_EXITED and switches to the genesis thread.
 * The thread's resources are NOT freed until kthread_reap() is called.
 * This function does not return.
 */
void kthread_exit(void);

/*
 * kthread_reap - Clean up exited threads.
 *
 * Scans the thread list for THREAD_EXITED threads, removes them from
 * the list, and frees their stack and control block memory.
 *
 * Should be called periodically from a thread that can safely block
 * (typically the genesis thread during idle time).
 *
 * Note: Thread IDs are never reused, so stale ID references remain
 * detectable (kthread_get_kthread returns NULL for reaped threads).
 */
void kthread_reap(void);

/* =============================================================================
 * Thread Queries
 * =============================================================================
 */

kthread_t *kthread_current(void);
void kthread_set_current(kthread_t *kthread);
kthread_t *kthread_get_kthread(uint64_t kthread_id);
void kthreads_list(void);

/* =============================================================================
 * Scheduling
 * =============================================================================
 */

/*
 * kthread_switch - Low-level context switch (defined in kthread_switch.S).
 *
 * Saves callee-saved registers to old thread's stack, switches stack
 * pointer, and restores registers from new thread's stack.
 */
void kthread_switch(uint64_t *old_rsp, uint64_t new_rsp);

/*
 * kthread_yield - Voluntarily give up the CPU.
 *
 * Marks current thread as READY and calls the scheduler.
 * Use in cooperative multitasking or long-running loops.
 */
void kthread_yield(void);

/*
 * kthread_schedule - Run the scheduler.
 *
 * Finds the next READY thread and switches to it.
 * Does nothing if preemption is disabled (preempt_count > 0).
 */
void kthread_schedule(void);

/* =============================================================================
 * Sleep / Wake
 * =============================================================================
 */

/*
 * kthread_sleep - Sleep for approximately the given number of milliseconds.
 *
 * @ms: Minimum sleep duration in milliseconds.
 *
 * IMPORTANT: Sleep granularity is 10ms (APIC timer runs at 100Hz).
 * The actual sleep time is rounded UP to the next 10ms boundary.
 * Example: kthread_sleep(1) sleeps ~10ms, kthread_sleep(25) sleeps ~30ms.
 */
void kthread_sleep(uint64_t ms);

/*
 * kthread_wake_sleepers - Wake threads whose sleep time has expired.
 *
 * Called from the timer interrupt to wake BLOCKED threads that have
 * reached their wake_tick time. Does NOT perform a context switch.
 */
void kthread_wake_sleepers(void);

/* =============================================================================
 * Preemption Control
 *
 * Prevents context switches in critical sections. Uses atomic operations
 * internally, so safe to call from interrupt context.
 *
 * IMPORTANT: When using with kthread_block(), you MUST call preempt_enable()
 * BEFORE kthread_block(). The pattern is:
 *
 *   preempt_disable();
 *   // ... modify shared state ...
 *   preempt_enable();    // <-- MUST come before block!
 *   kthread_block();
 *
 * If you call kthread_block() with preemption disabled, the scheduler
 * cannot switch threads and the system will hang.
 * =============================================================================
 */

void preempt_disable(void);
void preempt_enable(void);
int preempt_enabled(void);

/* =============================================================================
 * Block / Unblock (for synchronization primitives)
 * =============================================================================
 */

/*
 * kthread_block - Block the current thread until explicitly unblocked.
 *
 * The thread enters THREAD_BLOCKED state and the scheduler runs.
 * The thread will not be scheduled again until kthread_unblock() is called.
 *
 * WARNING: Preemption must be enabled before calling this function!
 */
void kthread_block(void);

/*
 * kthread_unblock - Wake a blocked thread.
 *
 * @thread: The thread to unblock.
 *
 * Sets the thread's state to THREAD_READY so it can be scheduled.
 * Does nothing if the thread is not in THREAD_BLOCKED state.
 */
void kthread_unblock(kthread_t *thread);

#endif /* KTHREAD_H */