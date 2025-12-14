/*
 * pwd.h - Password Database (Stubs)
 */

#ifndef _PWD_H
#define _PWD_H

#include <sys/types.h>

struct passwd {
    char   *pw_name;    /* Username */
    char   *pw_passwd;  /* Password */
    uid_t   pw_uid;     /* User ID */
    gid_t   pw_gid;     /* Group ID */
    char   *pw_gecos;   /* Real name */
    char   *pw_dir;     /* Home directory */
    char   *pw_shell;   /* Shell program */
};

/* Stub - always returns NULL (no user database) */
struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

#endif /* _PWD_H */
