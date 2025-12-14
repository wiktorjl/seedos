/*
 * sys/wait.h - Process Wait (Stubs)
 */

#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

/* Wait options */
#define WNOHANG    1
#define WUNTRACED  2

/* Macros to interpret status */
#define WIFEXITED(status)    (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)  (((status) >> 8) & 0xff)
#define WIFSIGNALED(status)  (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status)     ((status) & 0x7f)
#define WIFSTOPPED(status)   (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)     (((status) >> 8) & 0xff)

/* Stub - not implemented */
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);

#endif /* _SYS_WAIT_H */
