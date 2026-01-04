#ifndef KTHREAD_H
#define KTHREAD_H

#include "types.h"

#define KTHREAD_STACK_SIZE 16384

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_EXITED
} thread_state_t;

typedef struct kthread {
    const char * name;
    uint64_t id;
    uint64_t rsp;
    void *stack_base;
    thread_state_t state;
    struct kthread *next;
    void (*entry)(void *);
    void *arg;
} kthread_t;


void kthread_init(void);

uint64_t kthread_create(const char *kthread_friendly_name, void (*kthread_entry_point)(void *), void *arg);

void kthread_switch(uint64_t *old_rsp, uint64_t new_rsp);

void kthread_yield(void);

void kthread_exit(void);

kthread_t *kthread_current(void);

void kthread_schedule(void);

void kthreads_list(void);

kthread_t * kthread_get_kthread(uint64_t kthread_id);

#endif /* KTHREAD_H */