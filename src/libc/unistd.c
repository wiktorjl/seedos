/*
 * unistd.c - POSIX System Call Wrappers
 */

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "internal/syscall.h"

ssize_t read(int fd, void *buf, size_t count) {
    return __syscall3(__NR_read, fd, (long)buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return __syscall3(__NR_write, fd, (long)buf, count);
}

int close(int fd) {
    return __syscall1(__NR_close, fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    return __syscall3(__NR_lseek, fd, offset, whence);
}

pid_t getpid(void) {
    return __syscall0(__NR_getpid);
}

void _exit(int status) {
    __syscall1(__NR_exit, status);
    __builtin_unreachable();
}

void *sbrk(long increment) {
    return (void *)__syscall1(__NR_sbrk, increment);
}

char *getcwd(char *buf, size_t size) {
    long ret = __syscall2(__NR_getcwd, (long)buf, size);
    return (ret == -1) ? (char *)0 : buf;
}

int chdir(const char *path) {
    return __syscall1(__NR_chdir, (long)path);
}

int isatty(int fd) {
    return __syscall1(__NR_isatty, fd);
}

int dup(int oldfd) {
    return __syscall1(__NR_dup, oldfd);
}

int dup2(int oldfd, int newfd) {
    return __syscall2(__NR_dup2, oldfd, newfd);
}

unsigned long uptime(void) {
    return __syscall0(__NR_uptime);
}

void shutdown(void) {
    __syscall0(__NR_shutdown);
    __builtin_unreachable();
}

void reboot(void) {
    __syscall0(__NR_reboot);
    __builtin_unreachable();
}

int access(const char *pathname, int mode) {
    struct stat st;
    if(stat(pathname, &st) < 0) {
        return -1;  /* errno set by stat */
    }

    /* F_OK - just test existence */
    if(mode == F_OK) {
        return 0;
    }

    /*
     * For R_OK, W_OK, X_OK - we're a single-user system
     * so just check if the file has any of those bits set.
     * In reality, our tarfs is read-only so W_OK should fail.
     */
    if((mode & W_OK)) {
        errno = EROFS;
        return -1;
    }

    /* R_OK and X_OK - check file permissions */
    if((mode & R_OK) && !(st.st_mode & (S_IRUSR | S_IRGRP | S_IROTH))) {
        errno = EACCES;
        return -1;
    }

    if((mode & X_OK) && !(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        errno = EACCES;
        return -1;
    }

    return 0;
}

int spawn(const char *path, char *const argv[]) {
    return __syscall2(__NR_spawn, (long)path, (long)argv);
}

pid_t spawn_async(const char *path, char *const argv[]) {
    return __syscall2(__NR_spawn_async, (long)path, (long)argv);
}

/*
 * fork - Create a child process.
 *
 * SeedOS doesn't support true fork(). Instead, we return 0 to pretend
 * we're the child. When exec*() is called, it uses spawn() to run the
 * program, then exits. This means the shell will exit after running
 * one external command - the user returns to the kernel shell.
 *
 * This is a simplified "vfork-like" semantic.
 */
pid_t fork(void) {
    return 0;  /* Always pretend to be child */
}

/*
 * waitpid - Wait for a child process to change state.
 *
 * Waits for the specified child process to exit and returns its exit code.
 * The options parameter is currently ignored (no WNOHANG support yet).
 */
pid_t waitpid(pid_t pid, int *status, int options) {
    (void)options;
    int ret = __syscall1(__NR_waitpid, pid);
    if(ret < 0) {
        return -1;  /* Error - no such process */
    }
    if(status) {
        /* Encode exit code in status format: WEXITSTATUS extracts (status >> 8) & 0xff */
        *status = (ret & 0xff) << 8;
    }
    return pid;
}

pid_t wait(int *status) {
    return waitpid(-1, status, 0);
}

/*
 * execvp - Execute a program, searching PATH.
 *
 * In SeedOS, we use spawn() which runs the program and waits.
 * This is not true exec (which replaces the current process),
 * but allows SASH to run programs.
 */
int execvp(const char *file, char *const argv[]) {
    /* Try the file directly first */
    int ret = spawn(file, argv);
    if(ret >= 0) {
        /* spawn succeeded, but exec should not return on success */
        /* In SeedOS, spawn waits for completion, so we exit with child's code */
        _exit(ret);
    }

    /* If file doesn't contain a slash, try /bin/ prefix */
    int has_slash = 0;
    for(const char *p = file; *p; p++) {
        if(*p == '/') {
            has_slash = 1;
            break;
        }
    }

    if(!has_slash) {
        char path[256];
        /* Build /bin/file path */
        int i = 0;
        const char *prefix = "/bin/";
        while(*prefix && i < 250) {
            path[i++] = *prefix++;
        }
        const char *f = file;
        while(*f && i < 255) {
            path[i++] = *f++;
        }
        path[i] = '\0';

        ret = spawn(path, argv);
        if(ret >= 0) {
            _exit(ret);
        }
    }

    errno = ENOENT;
    return -1;
}

int execv(const char *path, char *const argv[]) {
    int ret = spawn(path, argv);
    if(ret >= 0) {
        _exit(ret);
    }
    errno = ENOENT;
    return -1;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)envp;  /* SeedOS doesn't support environment yet */
    return execv(path, argv);
}

/* Stubs for functions not yet implemented */
int unlink(const char *pathname) {
    (void)pathname;
    errno = EROFS;
    return -1;
}

int rmdir(const char *pathname) {
    (void)pathname;
    errno = EROFS;
    return -1;
}

int link(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    errno = EROFS;
    return -1;
}

int symlink(const char *target, const char *linkpath) {
    (void)target;
    (void)linkpath;
    errno = EROFS;
    return -1;
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    (void)pathname;
    (void)buf;
    (void)bufsiz;
    errno = EINVAL;
    return -1;
}

unsigned int sleep(unsigned int seconds) {
    (void)seconds;
    /* No sleep syscall yet - just return immediately */
    return 0;
}

void sync(void) {
    /* No-op - filesystem is read-only */
}

uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }
int setuid(uid_t uid) { (void)uid; return 0; }
int setgid(gid_t gid) { (void)gid; return 0; }
