/*
 * unistd.h - POSIX Standard Symbolic Constants and Types
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

/* Types */
typedef long ssize_t;
typedef int pid_t;
typedef long off_t;

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Seek whence values */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* File I/O */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);

/* Process control */
pid_t getpid(void);
void _exit(int status) __attribute__((noreturn));

/* Memory management */
void *sbrk(long increment);

/* SeedOS extensions */
unsigned long uptime(void);  /* milliseconds since boot */

#endif /* _UNISTD_H */
