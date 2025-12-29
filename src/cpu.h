// CPU primitives

#ifndef CPU_H
#define CPU_H

#include "types.h"

/* Stack symbols from boot.S */
extern uint64_t stack_size;
extern char stack_bottom[];
extern char stack_top[];

static inline uint64_t cpu_get_stack_top(void) {
    uint64_t rsp;
    asm volatile("movq %%rsp, %0" : "=r"(rsp));
    return rsp;
}

static inline void cpu_enable_interrupts(void) {
    asm volatile("sti");
}

static inline void cpu_disable_interrupts(void) {
    asm volatile("cli");
}

static inline void cpu_halt(void) {
    asm volatile("hlt");
}

#endif
