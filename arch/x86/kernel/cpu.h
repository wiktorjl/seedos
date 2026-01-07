/*
 * cpu.h - CPU Primitives
 */

#ifndef CPU_H
#define CPU_H

#include "types.h"

/* Stack symbols from boot.S */
extern uint64_t stack_size;
extern char stack_bottom[];
extern char stack_top[];

static inline uint64_t cpu_get_stack_top(void) {
    uint64_t rsp;
    __asm__ volatile("movq %%rsp, %0" : "=r"(rsp));
    return rsp;
}

static inline void cpu_enable_interrupts(void) {
    __asm__ volatile("sti");
}

static inline void cpu_disable_interrupts(void) {
    __asm__ volatile("cli");
}

static inline void cpu_halt(void) {
    __asm__ volatile("hlt");
}

#endif /* CPU_H */
