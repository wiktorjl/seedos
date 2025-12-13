/*
 * usys.h - Userspace System Call Library
 *
 * Provides C wrappers for all system calls. User programs should include
 * this header instead of writing inline assembly directly.
 *
 * Syscall convention (INT 0x80):
 *   RAX = syscall number
 *   RDI = arg1, RSI = arg2, RDX = arg3
 *   Return value in RAX
 */

#ifndef USYS_H
#define USYS_H

#include <stddef.h>

/* Syscall numbers (must match kernel's syscall.h) */
#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_GETPID 3
#define SYS_UPTIME 4
#define SYS_SBRK   5
#define SYS_OPEN   6
#define SYS_CLOSE  7
#define SYS_LSEEK  8

/*
 * sys_exit - Terminate the current process
 * @code: Exit code returned to the kernel
 *
 * This function never returns.
 */
static inline void sys_exit(int code) {
    __asm__ volatile (
        "mov %0, %%rax\n"
        "int $0x80\n"
        :
        : "i"(SYS_EXIT), "D"(code)
        : "rax"
    );
    __builtin_unreachable();
}

/*
 * sys_write - Write data to a file descriptor
 * @fd:  File descriptor (1 = stdout)
 * @buf: Buffer containing data to write
 * @len: Number of bytes to write
 *
 * Returns: Number of bytes written, or -1 on error
 */
static inline int sys_write(int fd, const char *buf, size_t len) {
    int ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_WRITE), "D"(fd), "S"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

/*
 * sys_read - Read data from a file descriptor
 * @fd:  File descriptor (0 = stdin)
 * @buf: Buffer to store read data
 * @len: Maximum number of bytes to read
 *
 * Returns: Number of bytes read, 0 if no data available, or -1 on error
 */
static inline int sys_read(int fd, char *buf, size_t len) {
    int ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_READ), "D"(fd), "S"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

/*
 * sys_getpid - Get the current process ID
 *
 * Returns: Process ID of the calling process
 */
static inline int sys_getpid(void) {
    int ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_GETPID)
    );
    return ret;
}

/*
 * sys_uptime - Get system uptime
 *
 * Returns: System uptime in milliseconds
 */
static inline unsigned long sys_uptime(void) {
    unsigned long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_UPTIME)
    );
    return ret;
}

/*
 * sys_sbrk - Adjust the program break (heap)
 * @increment: Number of bytes to add (positive) or remove (negative)
 *
 * Returns: Previous program break address, or (void*)-1 on error
 */
static inline void *sys_sbrk(long increment) {
    void *ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_SBRK), "D"(increment)
    );
    return ret;
}

/*
* sys_open - Open a file
* @path: Path to the file
* @flags: Open flags (O_RDONLY = 0)
*
* Returns: File descriptor on success, -1 on error
*/
static inline int sys_open(const char *path, int flags) {
    int ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_OPEN), "D"(path), "S"(flags)
        : "memory"
    );
    return ret;
}

/*
* sys_close - Close a file descriptor
* @fd: File descriptor to close
*
* Returns: 0 on success, -1 on error
*/
static inline int sys_close(int fd) {
    int ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_CLOSE), "D"(fd)
    );
    return ret;
}

/*
* sys_lseek - Reposition file offset
* @fd: File descriptor
* @offset: Offset value
* @whence: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END
*
* Returns: New position on success, -1 on error
*/
static inline long sys_lseek(int fd, long offset, int whence) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n"
        "int $0x80\n"
        : "=a"(ret)
        : "i"(SYS_LSEEK), "D"(fd), "S"(offset), "d"(whence)
    );
    return ret;
}



/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/*
 * strlen - Calculate string length
 */
static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/*
 * print - Print a null-terminated string to stdout
 */
static inline void print(const char *s) {
    sys_write(1, s, strlen(s));
}

/* Buffer for single-character output (in .data section) */
static char _print_char_buf[2] = {'?', 0};

/*
 * print_char - Print a single character to stdout
 */
static inline void print_char(char c) {
    _print_char_buf[0] = c;
    sys_write(1, _print_char_buf, 1);
}

/*
 * print_num - Print a signed decimal number to stdout
 */
static inline void print_num(long n) {
    char buf[24];
    int i = 0;
    int neg = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }

    if (n == 0) {
        print_char('0');
        return;
    }

    /* Build digits in reverse */
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    if (neg) {
        print_char('-');
    }

    /* Print in correct order */
    while (i > 0) {
        print_char(buf[--i]);
    }
}

/*
 * print_hex - Print a number in hexadecimal to stdout
 */
static inline void print_hex(unsigned long n) {
    const char *hex = "0123456789abcdef";
    char buf[17];
    int i = 0;

    if (n == 0) {
        print("0x0");
        return;
    }

    while (n > 0) {
        buf[i++] = hex[n & 0xf];
        n >>= 4;
    }

    print("0x");
    while (i > 0) {
        print_char(buf[--i]);
    }
}

#endif /* USYS_H */
