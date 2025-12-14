/*
 * sh.c - SeedOS Userspace Shell
 *
 * A simple shell that runs as a userspace process, enabling proper
 * preemptive multitasking. While the shell waits for input, other
 * processes can run.
 *
 * Builtins:
 *   cd <dir>  - Change directory
 *   exit      - Exit the shell
 *
 * Any other command runs as an external program from /bin.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_LINE 256
#define MAX_ARGS 32

/* Parse a line into argc/argv */
static int parse_line(char *line, char **argv) {
    int argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n') break;

        /* Start of argument */
        argv[argc++] = p;

        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;

        /* Null-terminate argument */
        if (*p) {
            *p = '\0';
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

/* Try to run a program - returns exit code or -1 on error */
static int run_program(int argc, char **argv) {
    char path[256];
    (void)argc;

    /* If path contains '/', use as-is */
    int has_slash = 0;
    for (const char *p = argv[0]; *p; p++) {
        if (*p == '/') {
            has_slash = 1;
            break;
        }
    }

    if (has_slash) {
        strcpy(path, argv[0]);
    } else {
        /* Try /bin/<name> */
        strcpy(path, "/bin/");
        strcat(path, argv[0]);
    }

    /* Run the program (blocking) */
    int exit_code = spawn(path, argv);
    if (exit_code < 0) {
        printf("sh: %s: not found\n", argv[0]);
        return -1;
    }

    return exit_code;
}

int main(int argc, char **argv) {
    char line[MAX_LINE];
    char *args[MAX_ARGS];
    char cwd[256];

    (void)argc;
    (void)argv;

    printf("SeedOS Shell\n");
    printf("Type 'exit' to quit\n\n");

    while (1) {
        /* Get current directory for prompt */
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            strcpy(cwd, "?");
        }

        /* Print prompt */
        printf("%s $ ", cwd);

        /* Read a line */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            continue;
        }

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        /* Skip empty lines */
        if (line[0] == '\0') {
            continue;
        }

        /* Parse the line */
        int nargs = parse_line(line, args);
        if (nargs == 0) {
            continue;
        }

        /* Built-in: exit */
        if (strcmp(args[0], "exit") == 0) {
            break;
        }

        /* Built-in: cd */
        if (strcmp(args[0], "cd") == 0) {
            const char *dir = (nargs > 1) ? args[1] : "/";
            if (chdir(dir) != 0) {
                printf("cd: %s: no such directory\n", dir);
            }
            continue;
        }

        /* Run external program */
        run_program(nargs, args);
    }

    printf("Goodbye!\n");
    return 0;
}
