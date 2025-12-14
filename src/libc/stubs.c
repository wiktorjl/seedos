/*
 * stubs.c - Stub implementations for unsupported functions
 *
 * These functions exist to allow programs to compile but return errors
 * since SeedOS has a read-only filesystem and limited process control.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Error strings */
static const char *error_strings[] = {
    [0] = "Success",
    [EPERM] = "Operation not permitted",
    [ENOENT] = "No such file or directory",
    [ESRCH] = "No such process",
    [EINTR] = "Interrupted system call",
    [EIO] = "I/O error",
    [ENXIO] = "No such device or address",
    [E2BIG] = "Argument list too long",
    [ENOEXEC] = "Exec format error",
    [EBADF] = "Bad file descriptor",
    [ECHILD] = "No child processes",
    [EAGAIN] = "Try again",
    [ENOMEM] = "Out of memory",
    [EACCES] = "Permission denied",
    [EFAULT] = "Bad address",
    [EBUSY] = "Device or resource busy",
    [EEXIST] = "File exists",
    [EXDEV] = "Cross-device link",
    [ENODEV] = "No such device",
    [ENOTDIR] = "Not a directory",
    [EISDIR] = "Is a directory",
    [EINVAL] = "Invalid argument",
    [ENFILE] = "File table overflow",
    [EMFILE] = "Too many open files",
    [ENOTTY] = "Not a typewriter",
    [EFBIG] = "File too large",
    [ENOSPC] = "No space left on device",
    [ESPIPE] = "Illegal seek",
    [EROFS] = "Read-only file system",
    [EMLINK] = "Too many links",
    [EPIPE] = "Broken pipe",
    [EDOM] = "Math argument out of domain",
    [ERANGE] = "Math result out of range",
    [ENOSYS] = "Function not implemented",
    [ELOOP] = "Too many symbolic links",
};

#define NUM_ERRORS (sizeof(error_strings) / sizeof(error_strings[0]))

char *strerror(int errnum) {
    static char unknown_buf[32];
    if(errnum >= 0 && (size_t)errnum < NUM_ERRORS && error_strings[errnum]) {
        return (char *)error_strings[errnum];
    }
    snprintf(unknown_buf, sizeof(unknown_buf), "Unknown error %d", errnum);
    return unknown_buf;
}

void perror(const char *s) {
    if(s && *s) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    }else {
        fprintf(stderr, "%s\n", strerror(errno));
    }
}

/* File operations - all return EROFS (read-only filesystem) */

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

int chmod(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    errno = EROFS;
    return -1;
}

int fchmod(int fd, mode_t mode) {
    (void)fd;
    (void)mode;
    errno = EROFS;
    return -1;
}

int mkdir(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    errno = EROFS;
    return -1;
}

int mknod(const char *path, mode_t mode, unsigned int dev) {
    (void)path;
    (void)mode;
    (void)dev;
    errno = EROFS;
    return -1;
}

int chown(const char *path, unsigned int owner, unsigned int group) {
    (void)path;
    (void)owner;
    (void)group;
    errno = EROFS;
    return -1;
}

int fchown(int fd, unsigned int owner, unsigned int group) {
    (void)fd;
    (void)owner;
    (void)group;
    errno = EROFS;
    return -1;
}

int rename(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    errno = EROFS;
    return -1;
}

int remove(const char *pathname) {
    (void)pathname;
    errno = EROFS;
    return -1;
}

/* Current umask value (global) */
static mode_t current_umask = 022;

mode_t umask(mode_t mask) {
    mode_t old = current_umask;
    current_umask = mask & 0777;
    return old;
}

/* Sync - no-op since filesystem is read-only */
void sync(void) {
    /* Nothing to sync */
}

/* Sleep - uses uptime syscall to busy-wait */
unsigned int sleep(unsigned int seconds) {
    unsigned long start = uptime();
    unsigned long target = start + (seconds * 1000);
    while(uptime() < target) {
        /* Busy wait - should use a yield syscall if available */
    }
    return 0;
}

/* User/Group IDs - always return 0 (root) */
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }

int setuid(uid_t uid) {
    (void)uid;
    return 0;  /* Pretend success */
}

int setgid(gid_t gid) {
    (void)gid;
    return 0;  /* Pretend success */
}

/* System - cannot execute commands */
int system(const char *command) {
    (void)command;
    errno = ENOSYS;
    return -1;
}

/* execvp - cannot execute */
int execvp(const char *file, char *const argv[]) {
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

/* execv - cannot execute */
int execv(const char *path, char *const argv[]) {
    (void)path;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

/* execve - cannot execute */
int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)path;
    (void)argv;
    (void)envp;
    errno = ENOSYS;
    return -1;
}

/* creat - cannot create files (read-only filesystem) */
int creat(const char *pathname, int mode) {
    (void)pathname;
    (void)mode;
    errno = EROFS;
    return -1;
}

/* readlink - no symbolic links in tarfs */
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    (void)pathname;
    (void)buf;
    (void)bufsiz;
    errno = EINVAL;  /* Not a symbolic link */
    return -1;
}

/* ============================================================================
 * Signal Handling (stubs - no signals in SeedOS)
 * ============================================================================ */
#include <signal.h>

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    return SIG_DFL;  /* Pretend success */
}

int kill(pid_t pid, int sig) {
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}

/* ============================================================================
 * Password/Group Database (stubs)
 * ============================================================================ */
#include <pwd.h>
#include <grp.h>

static struct passwd fake_passwd = {
    .pw_name = "root",
    .pw_passwd = "*",
    .pw_uid = 0,
    .pw_gid = 0,
    .pw_gecos = "root",
    .pw_dir = "/",
    .pw_shell = "/bin/sash"
};

static struct group fake_group = {
    .gr_name = "root",
    .gr_passwd = "*",
    .gr_gid = 0,
    .gr_mem = (char *[]){NULL}
};

struct passwd *getpwuid(uid_t uid) {
    (void)uid;
    return &fake_passwd;
}

struct passwd *getpwnam(const char *name) {
    (void)name;
    return &fake_passwd;
}

struct group *getgrgid(gid_t gid) {
    (void)gid;
    return &fake_group;
}

struct group *getgrnam(const char *name) {
    (void)name;
    return &fake_group;
}

/* ============================================================================
 * Time Functions (stubs)
 * ============================================================================ */
#include <time.h>

time_t time(time_t *tloc) {
    /* Return uptime as time (not correct but works for basic use) */
    time_t t = (time_t)(uptime() / 1000);  /* Convert ms to seconds */
    if(tloc) *tloc = t;
    return t;
}

static struct tm fake_tm = {
    .tm_sec = 0,
    .tm_min = 0,
    .tm_hour = 12,
    .tm_mday = 1,
    .tm_mon = 0,
    .tm_year = 125,  /* 2025 - 1900 */
    .tm_wday = 0,
    .tm_yday = 0,
    .tm_isdst = 0
};

struct tm *localtime(const time_t *timep) {
    (void)timep;
    return &fake_tm;
}

struct tm *gmtime(const time_t *timep) {
    (void)timep;
    return &fake_tm;
}

static char ctime_buf[26] = "Wed Jan  1 12:00:00 2025\n";

char *ctime(const time_t *timep) {
    (void)timep;
    return ctime_buf;
}

int utime(const char *filename, void *times) {
    (void)filename;
    (void)times;
    errno = EROFS;
    return -1;
}

/* ============================================================================
 * Mounting (stubs - no mount support)
 * ============================================================================ */

int mount(const char *source, const char *target, const char *fstype,
          unsigned long flags, const void *data) {
    (void)source;
    (void)target;
    (void)fstype;
    (void)flags;
    (void)data;
    errno = ENOSYS;
    return -1;
}

int umount(const char *target) {
    (void)target;
    errno = ENOSYS;
    return -1;
}

/* ============================================================================
 * Environment variable (for printenv)
 * ============================================================================ */

/* External declaration - actual storage is in env.c */
extern char **environ;

/* ============================================================================
 * Command stubs for commands we didn't build
 * ============================================================================ */

void do_ar(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "ar: not available\n");
}

void do_dd(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "dd: not available\n");
}

void do_ed(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "ed: not available\n");
}

void do_tar(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "tar: not available\n");
}
