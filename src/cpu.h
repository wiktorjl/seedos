// CPU primitives

#ifndef CPU_H
#define CPU_H

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
