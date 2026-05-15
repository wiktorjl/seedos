/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux x86-64 Syscall Numbers
 *
 * These match the Linux kernel's syscall numbering for x86-64.
 * See: arch/x86/entry/syscalls/syscall_64.tbl in Linux source.
 *
 * We implement a subset needed for running statically-linked Linux binaries.
 */

#ifndef _SYSCALL_TABLE_H
#define _SYSCALL_TABLE_H

#include "types.h"

/*
 * Syscall numbers - Linux x86-64 ABI
 *
 * Only commonly needed syscalls are listed here.
 * Add more as needed for compatibility.
 */
#define SYS_read            0
#define SYS_write           1
#define SYS_open            2
#define SYS_close           3
#define SYS_stat            4
#define SYS_fstat           5
#define SYS_lstat           6
#define SYS_poll            7
#define SYS_lseek           8
#define SYS_mmap            9
#define SYS_mprotect        10
#define SYS_munmap          11
#define SYS_brk             12
#define SYS_rt_sigaction    13
#define SYS_rt_sigprocmask  14
#define SYS_rt_sigreturn    15
#define SYS_ioctl           16
#define SYS_access          21
#define SYS_pipe            22
#define SYS_dup             32
#define SYS_dup2            33
#define SYS_getpid          39
#define SYS_clone           56
#define SYS_fork            57
#define SYS_vfork           58
#define SYS_execve          59
#define SYS_exit            60
#define SYS_wait4           61
#define SYS_kill            62
#define SYS_uname           63
#define SYS_fcntl           72
#define SYS_getcwd          79
#define SYS_chdir           80
#define SYS_mkdir           83
#define SYS_rmdir           84
#define SYS_unlink          87
#define SYS_readlink        89
#define SYS_getuid          102
#define SYS_getgid          104
#define SYS_geteuid         107
#define SYS_getegid         108
#define SYS_getppid         110
#define SYS_getpgrp         111
#define SYS_setsid          112
#define SYS_setuid          113
#define SYS_setgid          114
#define SYS_gettid          186
#define SYS_arch_prctl      158
#define SYS_exit_group      231
#define SYS_openat          257
#define SYS_mkdirat         258
#define SYS_fstatat         262
#define SYS_unlinkat        263
#define SYS_readlinkat      267
#define SYS_faccessat       269
#define SYS_getrandom       318

/*
 * Maximum syscall number we support.
 * Linux x86-64 has ~450 syscalls as of kernel 6.x.
 * We use 512 for headroom.
 */
#define NR_SYSCALLS         512

/*
 * Error codes (Linux-compatible)
 *
 * Syscalls return negative errno on error.
 * Only commonly used errors listed here.
 */
#define EPERM           1       /* Operation not permitted */
#define ENOENT          2       /* No such file or directory */
#define ESRCH           3       /* No such process */
#define EINTR           4       /* Interrupted system call */
#define EIO             5       /* I/O error */
#define ENXIO           6       /* No such device or address */
#define E2BIG           7       /* Argument list too long */
#define ENOEXEC         8       /* Exec format error */
#define EBADF           9       /* Bad file descriptor */
#define ECHILD          10      /* No child processes */
#define EAGAIN          11      /* Try again (also EWOULDBLOCK) */
#define ENOMEM          12      /* Out of memory */
#define EACCES          13      /* Permission denied */
#define EFAULT          14      /* Bad address */
#define EBUSY           16      /* Device or resource busy */
#define EEXIST          17      /* File exists */
#define ENODEV          19      /* No such device */
#define ENOTDIR         20      /* Not a directory */
#define EISDIR          21      /* Is a directory */
#define EINVAL          22      /* Invalid argument */
#define ENFILE          23      /* File table overflow */
#define EMFILE          24      /* Too many open files */
#define ENOTTY          25      /* Not a typewriter */
#define EFBIG           27      /* File too large */
#define ENOSPC          28      /* No space left on device */
#define ESPIPE          29      /* Illegal seek */
#define EROFS           30      /* Read-only file system */
#define EPIPE           32      /* Broken pipe */
#define ERANGE          34      /* Math result not representable */
#define ENAMETOOLONG    36      /* File name too long */
#define ENOSYS          38      /* Function not implemented */
#define ENOTEMPTY       39      /* Directory not empty */

/*
 * Syscall handler function pointer type
 *
 * All syscall handlers take 6 arguments (maximum for Linux x86-64).
 * Unused arguments are simply ignored by handlers that need fewer.
 */
typedef int64_t (*syscall_fn_t)(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                uint64_t arg4, uint64_t arg5, uint64_t arg6);

/*
 * Syscall table - array of function pointers indexed by syscall number
 */
extern syscall_fn_t syscall_table[NR_SYSCALLS];

/*
 * Initialize syscall table with handlers
 */
void syscall_table_init(void);

#endif /* _SYSCALL_TABLE_H */
