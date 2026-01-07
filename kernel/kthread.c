/*
 * kthread.c - Kernel Threading Implementation
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

/* Assembly context switch routine (see kthread_switch.S) */
extern void kthread_switch(uint64_t *old_rsp, uint64_t new_rsp);

/* =============================================================================
 * Global State
 * =============================================================================
 */

/* The genesis thread - wraps the initial kernel execution context */
kthread_t genesis_kthread;

/* Currently executing thread */
kthread_t *current_kthread = NULL;

/*
 * Monotonic thread ID counter.
 * IDs are never reused, ensuring stale references can be detected.
 * Starts at 1 because genesis thread is ID 0.
 */
static volatile uint64_t next_thread_id = 1;

/*
 * Preemption nesting counter.
 * When > 0, the scheduler will not perform context switches.
 * Uses atomic operations for interrupt safety.
 */
static volatile int preempt_count = 0;

/* =============================================================================
 * Preemption Control
 * =============================================================================
 */

void preempt_disable(void) {
    __sync_fetch_and_add(&preempt_count, 1);
}

void preempt_enable(void) {
    __sync_fetch_and_sub(&preempt_count, 1);
}

int preempt_enabled(void) {
    return __sync_fetch_and_add(&preempt_count, 0) == 0;
}

/* =============================================================================
 * Thread Lifecycle
 * =============================================================================
 */

void kthread_exit(void) {
    kthread_t *exiting = current_kthread;
    current_kthread->state = THREAD_EXITED;

    kthread_set_current(&genesis_kthread);
    kthread_switch(&exiting->rsp, genesis_kthread.rsp);

    // while(1) { asm volatile("hlt"); }
}

kthread_t *kthread_current(void) {
    return current_kthread;
}

void kthread_set_current(kthread_t *kthread) {
    current_kthread = kthread;
}

kthread_t * kthread_get_kthread(uint64_t kthread_id) {
    kthread_t *iter = &genesis_kthread;
    while(iter != NULL) {
        if(iter->id == kthread_id) {
            return iter;
        }
        iter = iter->next;
    }
    return NULL;
}

/* Wrap current (initial) execution as thread 0 */
void kthread_init(void) {
    genesis_kthread.id = 0;
    genesis_kthread.name = "genesis-kthread-0";
    genesis_kthread.next = NULL;
    genesis_kthread.rsp = 0; // Will be set when switching
    genesis_kthread.stack_base = NULL;
    genesis_kthread.state = THREAD_RUNNING;
    genesis_kthread.entry = (void*) 0x1337; // Not used
    genesis_kthread.arg = NULL;
    current_kthread = &genesis_kthread;

    log_debug("Wrapped initial execution in a kthread: %s", current_kthread->name);
}

void kthread_trampoline(void) {
    kthread_t *self = kthread_current();
    self->entry(self->arg);    // Call the real entry point
    kthread_exit();
}


uint64_t kthread_create(const char *kthread_friendly_name, void (*kthread_entry_point)(void *), void *arg) {
    preempt_disable();

    kthread_t *new_thread = (kthread_t *)kmalloc(sizeof(kthread_t));

    if (new_thread == NULL) {
        log_error("KTHREAD: Failed to allocate memory for new kernel thread");
        preempt_enable();
        return 0;
    }

    /* Assign unique monotonic ID - atomically fetch and increment */
    new_thread->id = __sync_fetch_and_add(&next_thread_id, 1);
    new_thread->name = kthread_friendly_name;
    new_thread->state = THREAD_READY;
    new_thread->wake_tick = 0;   /* Not sleeping */
    new_thread->wait_next = NULL; /* Not in any wait queue */
    new_thread->stack_base = kmalloc(KTHREAD_STACK_SIZE);
    
    if (new_thread->stack_base == NULL) {
        log_error("KTHREAD: Failed to allocate stack for new kernel thread");
        kfree(new_thread);
        preempt_enable();
        return 0;
    }
    
    uint64_t stack_top = (uint64_t)new_thread->stack_base + KTHREAD_STACK_SIZE;
    // Align stack pointer to 16 bytes for x86_64 ABI compliance
    stack_top &= ~0xF;
    new_thread->rsp = stack_top;
    new_thread->next = NULL;

    /* Add to thread list (append at end) */
    if(current_kthread == NULL) {
        current_kthread = new_thread;
        log_panic("KTHREAD: No current thread, setting new thread as current (this should not happen)");
    } else {
        kthread_t *iter = &genesis_kthread;
        while(iter->next != NULL) {
            iter = iter->next;
        }
        iter->next = new_thread;
    }

    new_thread->entry = kthread_entry_point;
    new_thread->arg = arg;

    // We need to set up the initial stack frame for the new thread
    // so that when we switch to it, it starts executing kthread_trampoline.
    uint64_t *stack_ptr = (uint64_t *)new_thread->rsp;
    *(--stack_ptr) = (uint64_t)kthread_trampoline; // Return address
    
    // We also need to adjust the stack for the saved registers
    for(int i = 0; i < 4; i++) {
        *(--stack_ptr) = 0; // R15, R14, R13, R12
    }
    for(int i = 0; i < 2; i++) {
        *(--stack_ptr) = 0; // RBP, RBX
    }
    new_thread->rsp = (uint64_t)stack_ptr;

    log_debug("KTHREAD: Created new kernel thread: %s (ID: %llu)", new_thread->name, new_thread->id);
    log_debug("KTHREAD: stack_base=%p, stack_top=%p, rsp=%p",
              new_thread->stack_base,
              (void*)((uint64_t)new_thread->stack_base + KTHREAD_STACK_SIZE),
              (void*)new_thread->rsp);

    preempt_enable();
    return new_thread->id;
}

/* =============================================================================
 * Scheduling
 * =============================================================================
 */

void kthread_yield(void) {
    current_kthread->state = THREAD_READY;
    kthread_schedule();
}

void kthread_schedule(void) {
    /* Don't switch if preemption is disabled */
    if (preempt_count > 0) {
        return;
    }

    // 1. First, wake any sleeping threads
    uint64_t now = apic_get_ticks();
    kthread_t *t = &genesis_kthread;
    while (t != NULL) {
        if (t->state == THREAD_BLOCKED && t->wake_tick != 0 && t->wake_tick <= now) {
            t->state = THREAD_READY;
            t->wake_tick = 0;
        }
        t = t->next;
    }

    // 2. Find next READY thread (after current)
    kthread_t *next_thread = current_kthread->next;
    while (next_thread != NULL && next_thread->state != THREAD_READY) {
        next_thread = next_thread->next;
    }

    // 3. Wrap around if needed
    if (next_thread == NULL) {
        next_thread = &genesis_kthread;
            while (next_thread != current_kthread &&
                next_thread->state != THREAD_READY) {
                    next_thread = next_thread->next;
                }
    }

    // 4. Switch if we found a different READY thread
    if (next_thread != NULL && next_thread != current_kthread) {
        kthread_t *old_thread = current_kthread;

        // If old thread was RUNNING (interrupted), mark it READY so it can be scheduled again.
        // Don't change if it's BLOCKED (sleeping) - it needs to stay blocked.
        if (old_thread->state == THREAD_RUNNING) {
            old_thread->state = THREAD_READY;
        }

        current_kthread = next_thread;
        current_kthread->state = THREAD_RUNNING;
        kthread_switch(&old_thread->rsp, current_kthread->rsp);
    } else if (current_kthread->state == THREAD_BLOCKED) {
        /*
         * Current thread is blocked but no READY thread to switch to.
         * We need to wait for an interrupt (timer tick) that might wake
         * a sleeping thread. Enable interrupts and halt until one arrives.
         */
        while (current_kthread->state == THREAD_BLOCKED) {
            /* Enable interrupts and halt atomically */
            __asm__ volatile ("sti; hlt");

            /* Timer interrupt happened - check for newly ready threads */
            uint64_t now = apic_get_ticks();
            kthread_t *t = &genesis_kthread;
            while (t != NULL) {
                if (t->state == THREAD_BLOCKED && t->wake_tick != 0 && t->wake_tick <= now) {
                    t->state = THREAD_READY;
                    t->wake_tick = 0;
                }
                t = t->next;
            }

            /* If current thread is still blocked, look for someone else to run */
            if (current_kthread->state == THREAD_BLOCKED) {
                /* Look for any READY thread */
                t = &genesis_kthread;
                while (t != NULL) {
                    if (t->state == THREAD_READY) {
                        kthread_t *old_thread = current_kthread;
                        current_kthread = t;
                        current_kthread->state = THREAD_RUNNING;
                        kthread_switch(&old_thread->rsp, current_kthread->rsp);
                        return;  /* When we resume, we're done */
                    }
                    t = t->next;
                }
            }
            /* If current thread was unblocked by signal/broadcast, loop will exit */
        }

        /* Thread was unblocked while waiting - mark it running */
        current_kthread->state = THREAD_RUNNING;
    }
}

/* =============================================================================
 * Sleep / Wake
 *
 * Note: Sleep granularity is 10ms because the APIC timer runs at 100Hz.
 * The ticks calculation rounds UP to ensure minimum sleep duration.
 * =============================================================================
 */

void kthread_sleep(uint64_t ms) {
    uint64_t ticks = (ms + 9) / 10;  // Round up: 10ms per tick
    current_kthread->wake_tick = apic_get_ticks() + ticks;
    current_kthread->state = THREAD_BLOCKED;
    kthread_schedule();
}

void kthread_wake_sleepers(void) {
    uint64_t now = apic_get_ticks();
    kthread_t *t = &genesis_kthread;
    while (t != NULL) {
        if (t->state == THREAD_BLOCKED && t->wake_tick != 0 && t->wake_tick <= now) {
            t->state = THREAD_READY;
            t->wake_tick = 0;
        }
        t = t->next;
    }
}

/* =============================================================================
 * Block / Unblock
 *
 * Used by synchronization primitives (mutex, condvar) to sleep threads
 * until a specific event occurs (lock released, condition signaled).
 *
 * IMPORTANT: Caller must ensure preemption is enabled before calling
 * kthread_block(), otherwise the scheduler cannot switch threads.
 * =============================================================================
 */

void kthread_block(void) {
    current_kthread->state = THREAD_BLOCKED;
    current_kthread->wake_tick = 0;  /* Not timer-based, wait for explicit unblock */
    kthread_schedule();
}

void kthread_unblock(kthread_t *thread) {
    if (thread->state == THREAD_BLOCKED) {
        thread->state = THREAD_READY;
    }
}

/* =============================================================================
 * Thread Cleanup / Reaping
 *
 * Removes EXITED threads from the list and frees their resources.
 * Should be called periodically (e.g., from the genesis thread or scheduler).
 * =============================================================================
 */

void kthread_reap(void) {
    preempt_disable();

    kthread_t *prev = &genesis_kthread;
    kthread_t *iter = genesis_kthread.next;

    while (iter != NULL) {
        if (iter->state == THREAD_EXITED) {
            /* Remove from list */
            kthread_t *to_free = iter;
            prev->next = iter->next;
            iter = iter->next;

            /* Free resources */
            log_debug("KTHREAD: Reaping thread ID %llu (%s)", to_free->id, to_free->name);
            if (to_free->stack_base != NULL) {
                kfree(to_free->stack_base);
            }
            kfree(to_free);
        } else {
            prev = iter;
            iter = iter->next;
        }
    }

    preempt_enable();
}

/* =============================================================================
 * Thread Listing
 * =============================================================================
 */

static const char *state_to_string(thread_state_t state) {
    switch (state) {
        case THREAD_READY:   return "READY";
        case THREAD_RUNNING: return "RUNNING";
        case THREAD_BLOCKED: return "BLOCKED";
        case THREAD_EXITED:  return "EXITED";
        default:             return "UNKNOWN";
    }
}

void kthreads_list(void) {
    preempt_disable();

    log_info("=== Kernel Threads ===");
    log_info("%-4s %-20s %-10s %s", "ID", "Name", "State", "Stack");

    kthread_t *t = &genesis_kthread;
    int count = 0;
    while (t != NULL) {
        log_info("%-4llu %-20s %-10s %p",
                 t->id,
                 t->name ? t->name : "(null)",
                 state_to_string(t->state),
                 t->stack_base);
        count++;
        t = t->next;
    }

    log_info("Total: %d threads", count);

    preempt_enable();
}
