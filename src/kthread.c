#include "kthread.h"
#include "heap.h"
#include "log.h"
#include "pmm.h"

extern void kthread_switch(uint64_t *old_rsp, uint64_t new_rsp);

kthread_t genesis_kthread;
kthread_t *current_kthread = NULL;

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
    log_debug("Calling yield");
    kthread_yield();            
    log_debug("Calling exit");
    kthread_exit();
}


uint64_t kthread_create(const char *kthread_friendly_name, void (*kthread_entry_point)(void *), void *arg) {
    kthread_t *new_thread = (kthread_t *)kmalloc(sizeof(kthread_t));
    
    if (new_thread == NULL) {
        log_error("KTHREAD: Failed to allocate memory for new kernel thread");
        return 0;
    }

    new_thread->id = 0;
    new_thread->name = kthread_friendly_name;
    new_thread->state = THREAD_READY;
    new_thread->stack_base = kmalloc(KTHREAD_STACK_SIZE);
    
    if (new_thread->stack_base == NULL) {
        log_error("KTHREAD: Failed to allocate stack for new kernel thread");
        kfree(new_thread);
        return 0;
    }
    
    uint64_t stack_top = (uint64_t)new_thread->stack_base + KTHREAD_STACK_SIZE;
    // Align stack pointer to 16 bytes for x86_64 ABI compliance
    stack_top &= ~0xF;
    new_thread->rsp = stack_top;
    new_thread->next = NULL;

    // Now we need to place this thread in the list
    if(current_kthread == NULL) {
        current_kthread = new_thread;
        log_panic("KTHREAD: No current thread, setting new thread as current (this should not happen)");
    } else {
        kthread_t *iter = current_kthread;
        while(iter->next != NULL) {
            iter = iter->next;
        }
        iter->next = new_thread;
        new_thread->id = iter->id + 1;
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
    return new_thread->id;
}

void kthread_yield(void) {
    current_kthread->state = THREAD_READY;
    kthread_schedule();
}

void kthread_schedule(void) {
    // Simple round-robin scheduler
    kthread_t *next_thread = current_kthread->next;
    while(next_thread != NULL && next_thread->state != THREAD_READY) {
        next_thread = next_thread->next;
    }
    if(next_thread == NULL) {
        // Wrap around to the beginning
        next_thread = &genesis_kthread;
        while(next_thread != current_kthread && next_thread->state != THREAD_READY) {
            next_thread = next_thread->next;
        }
    }
    if(next_thread != NULL && next_thread != current_kthread) {
        kthread_t *old_thread = current_kthread;
        current_kthread = next_thread;
        current_kthread->state = THREAD_RUNNING;
        kthread_switch(&old_thread->rsp, current_kthread->rsp);
    }
}