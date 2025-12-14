/*
 * env.c - Environment variable support
 *
 * Simple environment implementation using a static array.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Maximum number of environment variables */
#define MAX_ENV_VARS 64
#define MAX_ENV_SIZE 256

/* Environment storage */
static char *environ_storage[MAX_ENV_VARS + 1];
static char env_strings[MAX_ENV_VARS][MAX_ENV_SIZE];
static int env_count = 0;
static int env_initialized = 0;

/* Global environ pointer (for external access) */
char **environ = environ_storage;

/* Initialize with some default variables */
static void init_env(void) {
    if (env_initialized) return;
    env_initialized = 1;

    /* Set some reasonable defaults */
    putenv("PATH=/bin");
    putenv("HOME=/");
    putenv("SHELL=/bin/sash");
    putenv("TERM=vt100");
    putenv("LINES=25");
    putenv("COLS=80");
}

char *getenv(const char *name) {
    if (!env_initialized) init_env();
    if (!name) return NULL;

    size_t namelen = strlen(name);

    for (int i = 0; i < env_count; i++) {
        if (environ_storage[i] &&
            strncmp(environ_storage[i], name, namelen) == 0 &&
            environ_storage[i][namelen] == '=') {
            return environ_storage[i] + namelen + 1;
        }
    }

    return NULL;
}

int putenv(char *string) {
    if (!env_initialized) init_env();
    if (!string) return -1;

    /* Find the '=' */
    char *eq = strchr(string, '=');
    if (!eq) {
        /* No '=' means unset */
        return unsetenv(string);
    }

    size_t namelen = eq - string;

    /* Check if variable already exists */
    for (int i = 0; i < env_count; i++) {
        if (environ_storage[i] &&
            strncmp(environ_storage[i], string, namelen) == 0 &&
            environ_storage[i][namelen] == '=') {
            /* Replace existing */
            strncpy(env_strings[i], string, MAX_ENV_SIZE - 1);
            env_strings[i][MAX_ENV_SIZE - 1] = '\0';
            environ_storage[i] = env_strings[i];
            return 0;
        }
    }

    /* Add new variable */
    if (env_count >= MAX_ENV_VARS) {
        errno = ENOMEM;
        return -1;
    }

    strncpy(env_strings[env_count], string, MAX_ENV_SIZE - 1);
    env_strings[env_count][MAX_ENV_SIZE - 1] = '\0';
    environ_storage[env_count] = env_strings[env_count];
    env_count++;
    environ_storage[env_count] = NULL;

    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!env_initialized) init_env();
    if (!name || !value) {
        errno = EINVAL;
        return -1;
    }

    /* Check if already exists */
    if (!overwrite && getenv(name) != NULL) {
        return 0;
    }

    /* Build "name=value" string */
    size_t namelen = strlen(name);
    size_t valuelen = strlen(value);

    if (namelen + valuelen + 2 > MAX_ENV_SIZE) {
        errno = ENOMEM;
        return -1;
    }

    /* Find or create slot */
    int slot = -1;
    for (int i = 0; i < env_count; i++) {
        if (environ_storage[i] &&
            strncmp(environ_storage[i], name, namelen) == 0 &&
            environ_storage[i][namelen] == '=') {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        if (env_count >= MAX_ENV_VARS) {
            errno = ENOMEM;
            return -1;
        }
        slot = env_count++;
    }

    /* Build the string */
    strcpy(env_strings[slot], name);
    env_strings[slot][namelen] = '=';
    strcpy(env_strings[slot] + namelen + 1, value);
    environ_storage[slot] = env_strings[slot];
    environ_storage[env_count] = NULL;

    return 0;
}

int unsetenv(const char *name) {
    if (!env_initialized) init_env();
    if (!name) {
        errno = EINVAL;
        return -1;
    }

    size_t namelen = strlen(name);

    for (int i = 0; i < env_count; i++) {
        if (environ_storage[i] &&
            strncmp(environ_storage[i], name, namelen) == 0 &&
            environ_storage[i][namelen] == '=') {
            /* Remove by shifting */
            for (int j = i; j < env_count - 1; j++) {
                environ_storage[j] = environ_storage[j + 1];
                strcpy(env_strings[j], env_strings[j + 1]);
                environ_storage[j] = env_strings[j];
            }
            env_count--;
            environ_storage[env_count] = NULL;
            return 0;
        }
    }

    return 0;  /* Not found is not an error */
}
