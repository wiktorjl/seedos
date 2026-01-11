/* Minimal syscall wrappers for SeedOS userspace programs */

#ifndef _SYSCALL_H
#define _SYSCALL_H

typedef unsigned long size_t;
typedef long ssize_t;

/* Linux x86-64 syscall numbers */
#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_brk         12
#define SYS_getpid      39
#define SYS_fork        57
#define SYS_execve      59
#define SYS_exit        60
#define SYS_wait4       61
#define SYS_uname       63
#define SYS_getppid     110

/* Generic syscall macros */
static inline long syscall0(long n)
{
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long n, long a1)
{
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

/* Convenience wrappers */
static inline void exit(int status)
{
    syscall1(SYS_exit, status);
    __builtin_unreachable();
}

static inline ssize_t write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_write, fd, (long)buf, count);
}

static inline ssize_t read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_read, fd, (long)buf, count);
}

static inline long getpid(void)
{
    return syscall0(SYS_getpid);
}

#endif /* _SYSCALL_H */
