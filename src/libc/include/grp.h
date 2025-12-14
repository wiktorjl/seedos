/*
 * grp.h - Group Database (Stubs)
 */

#ifndef _GRP_H
#define _GRP_H

#include <sys/types.h>

struct group {
    char   *gr_name;    /* Group name */
    char   *gr_passwd;  /* Group password */
    gid_t   gr_gid;     /* Group ID */
    char  **gr_mem;     /* Group members */
};

/* Stub - always returns NULL (no group database) */
struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);

#endif /* _GRP_H */
