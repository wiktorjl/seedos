#ifndef SYSCALL_H
#define SYSCALL_H

#define SYS_EXIT        0
#define SYS_WRITE       1

#include <stdint.h>

struct registers {
    uint64_t rdi, rsi, rdx, rcx, rbx, rax;
};


void syscall_handler(struct registers *regs);


#endif /* SYSCALL_H */