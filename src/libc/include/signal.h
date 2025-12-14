/*
 * signal.h - Signal Handling (Stubs)
 */

#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

/* Signal numbers */
#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE    13
#define SIGALRM    14
#define SIGTERM    15
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN    21
#define SIGTTOU    22

/* Special signal handlers */
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

typedef void (*sighandler_t)(int);

/* Stub functions */
sighandler_t signal(int signum, sighandler_t handler);
int kill(pid_t pid, int sig);

#endif /* _SIGNAL_H */
