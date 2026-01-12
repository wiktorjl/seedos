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

static inline long syscall4(long n, long a1, long a2, long a3, long a4)
{
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
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

static inline long getppid(void)
{
    return syscall0(SYS_getppid);
}

static inline long brk(void *addr)
{
    return syscall1(SYS_brk, (long)addr);
}

/* syscall6 for mmap */
__attribute__((always_inline))
static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

/* mmap flags */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20

__attribute__((always_inline))
static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long offset)
{
    return (void *)syscall6(SYS_mmap, (long)addr, len, prot, flags, fd, offset);
}

static inline int munmap(void *addr, size_t len)
{
    return syscall2(SYS_munmap, (long)addr, len);
}

/* utsname structure */
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

static inline int uname(struct utsname *buf)
{
    return syscall1(SYS_uname, (long)buf);
}

/* wait4 options */
#define WNOHANG     0x00000001  /* Don't block if no child exited */
#define WUNTRACED   0x00000002  /* Report stopped children */

/* Wait status decoding macros */
#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)     ((s) & 0x7f)

static inline long wait4(long pid, int *wstatus, int options, void *rusage)
{
    return syscall4(SYS_wait4, pid, (long)wstatus, options, (long)rusage);
}

/* Simpler waitpid wrapper (common case) */
static inline long waitpid(long pid, int *wstatus, int options)
{
    return wait4(pid, wstatus, options, (void *)0);
}

#endif /* _SYSCALL_H */
