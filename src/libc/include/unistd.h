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

/* File descriptor duplication */
int dup(int oldfd);
int dup2(int oldfd, int newfd);

/* Process control */
pid_t getpid(void);
void _exit(int status) __attribute__((noreturn));

/* Memory management */
void *sbrk(long increment);

/* Working directory */
char *getcwd(char *buf, size_t size);
int chdir(const char *path);

/* Terminal */
int isatty(int fd);

/* File access */
int access(const char *pathname, int mode);
#define F_OK 0  /* Test for existence */
#define X_OK 1  /* Test for execute permission */
#define W_OK 2  /* Test for write permission */
#define R_OK 4  /* Test for read permission */

/* File operations (stubs - filesystem is read-only) */
int unlink(const char *pathname);
int rmdir(const char *pathname);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

/* Process */
unsigned int sleep(unsigned int seconds);

/* System */
void sync(void);

/* User/group IDs (stubs) */
typedef unsigned int uid_t;
typedef unsigned int gid_t;
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

/* Process creation */
pid_t fork(void);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);

/* waitpid options */
#define WNOHANG   1
#define WUNTRACED 2

/* Wait status macros */
#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)    ((s) & 0x7f)

/* Process execution */
int execvp(const char *file, char *const argv[]);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);

/* SeedOS extensions */
unsigned long uptime(void);  /* milliseconds since boot */

/*
 * spawn - Run a program and wait for it to complete.
 *
 * This is like system() - runs a program and waits for completion.
 * Returns the program's exit code, or -1 on error.
 */
int spawn(const char *path, char *const argv[]);

#endif /* _UNISTD_H */
