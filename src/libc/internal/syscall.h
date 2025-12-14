/*
 * internal/syscall.h - Raw System Call Interface
 *
 * Low-level syscall wrappers using INT 0x80.
 * This is an internal header - user code should use <unistd.h> instead.
 *
 * Syscall convention:
 *   RAX = syscall number
 *   RDI = arg1, RSI = arg2, RDX = arg3
 *   Return value in RAX
 */

#ifndef _LIBC_INTERNAL_SYSCALL_H
#define _LIBC_INTERNAL_SYSCALL_H

/* Syscall numbers (must match kernel's syscall.h) */
#define __NR_exit     0
#define __NR_write    1
#define __NR_read     2
#define __NR_getpid   3
#define __NR_uptime   4
#define __NR_sbrk     5
#define __NR_open     6
#define __NR_close    7
#define __NR_lseek    8
#define __NR_stat     9
#define __NR_fstat   10
#define __NR_getdents 11
#define __NR_getcwd  12
#define __NR_chdir   13
#define __NR_isatty  14

/* Syscall with 0 arguments */
static inline long __syscall0(long n) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "r"(n)
        : "memory"
    );
    return ret;
}

/* Syscall with 1 argument */
static inline long __syscall1(long n, long a1) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "r"(n), "D"(a1)
        : "memory"
    );
    return ret;
}

/* Syscall with 2 arguments */
static inline long __syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "r"(n), "D"(a1), "S"(a2)
        : "memory"
    );
    return ret;
}

/* Syscall with 3 arguments */
static inline long __syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "r"(n), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

#endif /* _LIBC_INTERNAL_SYSCALL_H */
