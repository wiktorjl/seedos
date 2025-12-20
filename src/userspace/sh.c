/*
 * sh.c - SeedOS Userspace Shell
 *
 * A simple shell that runs as a userspace process, enabling proper
 * preemptive multitasking. While the shell waits for input, other
 * processes can run.
 *
 * Features:
 *   - Command history with arrow key navigation
 *   - Line editing with backspace
 *
 * Builtins:
 *   cd <dir>  - Change directory
 *   exit      - Exit the shell
 *
 * Background execution:
 *   command & - Run command in background (non-blocking)
 *
 * Any other command runs as an external program from /bin.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_LINE 256
#define MAX_ARGS 32
#define HISTORY_SIZE 16

/* Command history circular buffer */
static char history[HISTORY_SIZE][MAX_LINE];
static int history_count = 0;    /* Total commands added (may exceed HISTORY_SIZE) */
static int history_start = 0;    /* Index of oldest entry in circular buffer */

/* Add a command to history */
static void history_add(const char *cmd) {
    if(cmd[0] == '\0') return;  /* Don't add empty commands */

    /* Don't add duplicates of the last command */
    if(history_count > 0) {
        int last_idx = (history_start + ((history_count - 1) % HISTORY_SIZE)) % HISTORY_SIZE;
        if(strcmp(history[last_idx], cmd) == 0) {
            return;
        }
    }

    /* Add to circular buffer */
    int idx = (history_start + (history_count % HISTORY_SIZE)) % HISTORY_SIZE;
    if(history_count >= HISTORY_SIZE) {
        /* Buffer full, overwrite oldest */
        history_start = (history_start + 1) % HISTORY_SIZE;
    }
    strncpy(history[idx], cmd, MAX_LINE - 1);
    history[idx][MAX_LINE - 1] = '\0';
    history_count++;
}

/* Get history entry by index (0 = oldest available) */
static const char *history_get(int idx) {
    int available = history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;
    if(idx < 0 || idx >= available) {
        return NULL;
    }
    int actual_idx = (history_start + idx) % HISTORY_SIZE;
    return history[actual_idx];
}

/* Get number of available history entries */
static int history_available(void) {
    return history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;
}

/*
 * readline - Read a line with editing and history support
 *
 * Handles:
 *   - Character input with echo
 *   - Backspace for deletion
 *   - Up/Down arrows for history navigation
 *   - Enter to submit
 *
 * Returns: length of line, or -1 on error
 */
static int readline(char *buf, int size) {
    int pos = 0;              /* Current position in buffer */
    int history_pos = -1;     /* -1 = current line, 0..n = history index from newest */
    char saved_line[MAX_LINE] = "";  /* Save current line when browsing history */
    int saved_pos = 0;

    buf[0] = '\0';

    while(1) {
        int c = getchar();
        if(c == EOF) {
            return -1;
        }

        /* Handle escape sequences (arrow keys) */
        if(c == 27) {  /* ESC */
            int c2 = getchar();
            if(c2 == '[') {
                int c3 = getchar();
                int avail = history_available();

                if(c3 == 'A') {  /* Up arrow - older history */
                    if(avail > 0) {
                        /* Save current line first time we go into history */
                        if(history_pos == -1) {
                            strncpy(saved_line, buf, MAX_LINE - 1);
                            saved_line[MAX_LINE - 1] = '\0';
                            saved_pos = pos;
                        }

                        /* Move to older entry */
                        if(history_pos < avail - 1) {
                            history_pos++;

                            /* Clear current line on screen */
                            while(pos > 0) {
                                printf("\b \b");
                                pos--;
                            }

                            /* Get history entry (convert from "newest first" to actual index) */
                            const char *hist = history_get(avail - 1 - history_pos);
                            if(hist) {
                                strncpy(buf, hist, size - 1);
                                buf[size - 1] = '\0';
                                pos = strlen(buf);
                                printf("%s", buf);
                            }
                        }
                    }
                    continue;
                }else if(c3 == 'B') {  /* Down arrow - newer history */
                    if(history_pos >= 0) {
                        /* Clear current line on screen */
                        while(pos > 0) {
                            printf("\b \b");
                            pos--;
                        }

                        history_pos--;

                        if(history_pos == -1) {
                            /* Back to saved line */
                            strncpy(buf, saved_line, size - 1);
                            buf[size - 1] = '\0';
                            pos = saved_pos;
                        }else {
                            /* Get history entry */
                            const char *hist = history_get(avail - 1 - history_pos);
                            if(hist) {
                                strncpy(buf, hist, size - 1);
                                buf[size - 1] = '\0';
                                pos = strlen(buf);
                            }
                        }
                        printf("%s", buf);
                    }
                    continue;
                }
                /* Ignore other escape sequences (left/right arrows, etc.) */
                continue;
            }
            /* Ignore bare ESC */
            continue;
        }

        /* Handle backspace */
        if(c == '\b' || c == 127) {
            if(pos > 0) {
                pos--;
                buf[pos] = '\0';
                printf("\b \b");  /* Move back, erase, move back */
            }
            continue;
        }

        /* Handle Enter */
        if(c == '\n' || c == '\r') {
            buf[pos] = '\0';
            printf("\n");
            return pos;
        }

        /* Regular character */
        if(pos < size - 1 && c >= 32 && c < 127) {
            buf[pos++] = (char)c;
            buf[pos] = '\0';
            putchar(c);
        }
    }
}

/* Parse a line into argc/argv */
static int parse_line(char *line, char **argv) {
    int argc = 0;
    char *p = line;

    while(*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        while(*p == ' ' || *p == '\t') p++;
        if(*p == '\0' || *p == '\n') break;

        /* Start of argument */
        argv[argc++] = p;

        /* Find end of argument */
        while(*p && *p != ' ' && *p != '\t' && *p != '\n') p++;

        /* Null-terminate argument */
        if(*p) {
            *p = '\0';
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

/* Try to run a program - returns exit code or -1 on error */
static int run_program(int argc, char **argv, int background) {
    char path[256];
    (void)argc;

    /* If path contains '/', use as-is */
    int has_slash = 0;
    for(const char *p = argv[0]; *p; p++) {
        if(*p == '/') {
            has_slash = 1;
            break;
        }
    }

    if(has_slash) {
        strcpy(path, argv[0]);
    }else {
        /* Try /bin/<name> */
        strcpy(path, "/bin/");
        strcat(path, argv[0]);
    }

    if(background) {
        /* Run in background (non-blocking) */
        int pid = spawn_async(path, argv);
        if(pid < 0) {
            printf("sh: %s: not found\n", argv[0]);
            return -1;
        }
        printf("[%d] %s\n", pid, argv[0]);
        return 0;
    }else {
        /* Run the program (blocking) */
        int exit_code = spawn(path, argv);
        if(exit_code < 0) {
            printf("sh: %s: not found\n", argv[0]);
            return -1;
        }
        return exit_code;
    }
}

int main(int argc, char **argv) {
    char line[MAX_LINE];
    char *args[MAX_ARGS];
    char cwd[256];

    (void)argc;
    (void)argv;

    printf("SeedOS Shell\n");
    printf("Type 'exit' to quit\n\n");

    while(1) {
        /* Get current directory for prompt */
        if(getcwd(cwd, sizeof(cwd)) == NULL) {
            strcpy(cwd, "?");
        }

        /* Print prompt */
        printf("%s $ ", cwd);

        /* Read a line with editing and history support */
        if(readline(line, sizeof(line)) < 0) {
            continue;
        }

        /* Skip empty lines */
        if(line[0] == '\0') {
            continue;
        }

        /* Add to history */
        history_add(line);

        /* Parse the line */
        int nargs = parse_line(line, args);
        if(nargs == 0) {
            continue;
        }

        /* Built-in: exit */
        if(strcmp(args[0], "exit") == 0) {
            break;
        }

        /* Built-in: cd */
        if(strcmp(args[0], "cd") == 0) {
            const char *dir = (nargs > 1) ? args[1] : "/";
            if(chdir(dir) != 0) {
                printf("cd: %s: no such directory\n", dir);
            }
            continue;
        }

        /* Built-in: history */
        if(strcmp(args[0], "history") == 0) {
            int avail = history_available();
            for(int i = 0; i < avail; i++) {
                const char *cmd = history_get(i);
                if(cmd) {
                    printf("%3d  %s\n", i + 1, cmd);
                }
            }
            continue;
        }

        /* Check for background execution (&) */
        int background = 0;
        if(nargs > 0 && strcmp(args[nargs - 1], "&") == 0) {
            background = 1;
            args[nargs - 1] = NULL;
            nargs--;
        }

        /* Skip if only & was provided */
        if(nargs == 0) {
            continue;
        }

        /* Run external program */
        run_program(nargs, args, background);
    }

    printf("Goodbye!\n");
    return 0;
}
