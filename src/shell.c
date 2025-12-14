/*
 * shell.c - Simple Kernel Shell
 *
 * This file implements a basic command-line shell for interacting with
 * the kernel. It's useful for testing and debugging kernel subsystems.
 *
 * Implementation Notes:
 *
 *   - Commands are buffered character-by-character as they're typed
 *   - Enter key triggers command parsing and execution
 *   - Commands are matched using simple string comparison
 *   - No command history or advanced editing (keeps it simple)
 *
 * Adding a New Command:
 *
 *   1. Write a cmd_xxx() function to handle the command
 *   2. Add it to the help text in cmd_help()
 *   3. Add an else-if branch in execute_command()
 */

#include "shell.h"
#include "pit.h"
#include "pmm.h"
#include "console.h"
#include "tar.h"
#include "test_framework.h"
#include "string.h"
#include "programs.h"
#include <stddef.h>

/* =============================================================================
 * Command Buffer
 *
 * Characters are accumulated here until Enter is pressed.
 * =============================================================================
 */
#define COMMAND_BUFFER_SIZE 128

static char command_buffer[COMMAND_BUFFER_SIZE];
static int command_length = 0;

/* Current working directory for ls/cd commands */
#define MAX_PATH_LEN 128
static char current_path[MAX_PATH_LEN] = "";

/* Last program exit code (for 'last_exit' command) */
static int last_exit_code = 0;

/*
 * normalize_path - Clean up a path by removing ./ and leading /
 *
 * Writes normalized path to dest buffer.
 */
static void normalize_path(const char *src, char *dest, size_t dest_size) {
    /* Skip leading ./ */
    if (src[0] == '.' && src[1] == '/') {
        src += 2;
    }
    /* Skip leading / */
    while (*src == '/') {
        src++;
    }

    /* Copy rest */
    size_t i = 0;
    while (*src && i < dest_size - 1) {
        dest[i++] = *src++;
    }
    dest[i] = '\0';
}

/* =============================================================================
 * Special Character Codes
 * =============================================================================
 */
#define CHAR_NEWLINE     '\n'
#define CHAR_RETURN      '\r'
#define CHAR_BACKSPACE   '\b'
#define CHAR_DELETE      127    /* Some terminals send this for backspace */
#define CHAR_SPACE       ' '

/* Printable ASCII range */
#define CHAR_PRINTABLE_MIN 32
#define CHAR_PRINTABLE_MAX 126

/* =============================================================================
 * Command Handler Functions
 *
 * Each command has a corresponding cmd_xxx() function.
 * =============================================================================
 */

/* Memory page size for display calculations */
#define PAGE_SIZE_KB 4
#define KB_PER_MB 1024

/*
 * cmd_help - Display list of available commands.
 */
static void cmd_help(void) {
    puts("\nFilesystem:\n");
    puts("  ls [path]     - List directory contents\n");
    puts("  cd [path]     - Change current directory\n");
    puts("\nPrograms:\n");
    puts("  run <program> - Run a program from /bin\n");
    puts("\nSystem:\n");
    puts("  uptime        - Show system uptime in ms\n");
    puts("  meminfo       - Show memory statistics\n");
    puts("  last_exit     - Show last program exit code\n");
    puts("  alloc         - Allocate a physical page\n");
    puts("  free <addr>   - Free a physical page (hex address)\n");
    puts("  clear         - Clear screen\n");
    puts("  help          - Show this help\n");
    puts("\nTests:\n");
    puts("  test              - List all tests\n");
    puts("  test all          - Run all tests\n");
    puts("  test <component>  - Run tests for component\n");
    puts("  test <c>.<name>   - Run specific test\n");
    puts("\nDebugging:\n");
    puts("  crash         - Trigger a page fault\n");
    puts("  divzero       - Trigger divide by zero\n");
    puts("\n");
}

/* Maximum arguments for a user program */
#define MAX_ARGS 16
#define MAX_ARG_LEN 64

/*
 * cmd_run - Run a user program from the initrd.
 *
 * @arg: Program name and optional arguments (e.g., "hello" or "ctest arg1 arg2")
 *
 * Parses arguments and calls programs_run(). Stores exit code in last_exit_code.
 */
static void cmd_run(const char *arg) {
    /* Skip leading whitespace */
    while (*arg == CHAR_SPACE) arg++;

    if (*arg == '\0') {
        puts("Usage: run <program> [args...]\n");
        puts("Example: run hello\n");
        puts("Example: run bin/ctest arg1 arg2\n");
        return;
    }

    /* Parse into argv array */
    static char arg_storage[MAX_ARGS][MAX_ARG_LEN];
    static char *argv[MAX_ARGS + 1];  /* +1 for NULL terminator */
    int argc = 0;

    const char *p = arg;
    while (*p && argc < MAX_ARGS) {
        /* Skip whitespace */
        while (*p == CHAR_SPACE) p++;
        if (*p == '\0') break;

        /* Copy argument */
        int i = 0;
        while (*p && *p != CHAR_SPACE && i < MAX_ARG_LEN - 1) {
            arg_storage[argc][i++] = *p++;
        }
        arg_storage[argc][i] = '\0';
        argv[argc] = arg_storage[argc];
        argc++;
    }
    argv[argc] = NULL;

    if (argc == 0) {
        puts("Error: missing program name\n");
        return;
    }

    /* Normalize the program path */
    char prog_path[MAX_PATH_LEN];
    normalize_path(argv[0], prog_path, sizeof(prog_path));

    /* Build the cwd path (e.g., "/" or "/bin") */
    char cwd[MAX_PATH_LEN + 1];
    cwd[0] = '/';
    if (current_path[0] != '\0') {
        strncpy(cwd + 1, current_path, MAX_PATH_LEN - 1);
        cwd[MAX_PATH_LEN] = '\0';
    } else {
        cwd[1] = '\0';
    }

    /* Run the program with arguments and current working directory */
    puts("\n");
    int exit_code = programs_run_cwd(prog_path, argc, argv, cwd);
    last_exit_code = exit_code;

    if (exit_code == -1) {
        puts("Error: program not found: ");
        puts(prog_path);
        puts("\n");
    }
}

/*
 * cmd_meminfo - Display physical memory statistics.
 *
 * Shows total, free, and used pages along with MB equivalents.
 */
static void cmd_meminfo(void) {
    uint64_t total_pages = pmm_get_total_pages();
    uint64_t free_pages = pmm_get_free_pages();
    uint64_t used_pages = total_pages - free_pages;

    puts("\nPhysical Memory:\n");

    puts("  Total pages:  ");
    put_dec(total_pages);
    puts(" (");
    put_dec(total_pages * PAGE_SIZE_KB / KB_PER_MB);
    puts(" MB)\n");

    puts("  Free pages:   ");
    put_dec(free_pages);
    puts(" (");
    put_dec(free_pages * PAGE_SIZE_KB / KB_PER_MB);
    puts(" MB)\n");

    puts("  Used pages:   ");
    put_dec(used_pages);
    puts(" (");
    put_dec(used_pages * PAGE_SIZE_KB / KB_PER_MB);
    puts(" MB)\n");

    puts("\n");
}

static void cmd_uptime(void) {
    puts("\nUptime: ");
    put_dec(pit_get_ticks() * 10);
    puts(" ms\n\n");
}

/*
 * cmd_last_exit - Display the last program exit code.
 */
static void cmd_last_exit(void) {
    put_dec(last_exit_code);
    puts("\n");
}

/*
 * cmd_alloc - Allocate a single physical page.
 *
 * Demonstrates the physical memory allocator. The allocated page's
 * address is printed so it can be freed later with "free".
 */
static void cmd_alloc(void) {
    uint64_t page_addr = pmm_alloc();

    if (page_addr == 0) {
        puts("\nError: Out of memory!\n\n");
    } else {
        puts("\nAllocated page at: ");
        put_hex(page_addr);
        puts("\n\n");
    }
}

/*
 * cmd_free - Free a physical page by address.
 *
 * @arg: The hex address string (after "free ").
 */
static void cmd_free(const char *arg) {
    /* Skip leading whitespace */
    while (*arg == CHAR_SPACE) arg++;

    if (*arg == '\0') {
        puts("\nUsage: free <hex_address>\n");
        puts("Example: free 0x5000\n\n");
        return;
    }

    uint64_t addr = parse_hex(arg);

    /* Don't allow freeing page 0 (NULL pointer protection) */
    if (addr == 0) {
        puts("\nError: Cannot free address 0\n\n");
        return;
    }

    /* Address must be page-aligned (multiple of 4KB) */
    if (addr & 0xFFF) {
        puts("\nError: Address must be page-aligned (multiple of 0x1000)\n\n");
        return;
    }

    pmm_free(addr);
    puts("\nFreed page at: ");
    put_hex(addr);
    puts("\n\n");
}

/*
 * cmd_crash - Deliberately cause a page fault.
 *
 * Useful for testing the exception handler. Writes to an unmapped address.
 */
static void cmd_crash(void) {
    puts("\nTriggering page fault...\n");
    volatile char *bad_ptr = (volatile char *)0xDEADBEEF;
    *bad_ptr = 0;  /* This will fault - address is not mapped */
}

/*
 * cmd_divzero - Deliberately cause a divide-by-zero exception.
 *
 * Useful for testing the exception handler.
 */
static void cmd_divzero(void) {
    puts("\nTriggering divide by zero...\n");
    volatile int numerator = 1;
    volatile int divisor = 0;
    volatile int result = numerator / divisor;
    (void)result;  /* Suppress unused warning (we'll never get here) */
}

/*
 * cmd_clear - Clear the screen using ANSI escape sequences.
 */
static void cmd_clear(void) {
    /* \033[2J = clear entire screen, \033[H = move cursor to home */
    puts("\033[2J\033[H");
}

/*
 * cmd_test - Run tests with the new framework.
 *
 * @arg: The test specification (after "test ").
 *
 * Supported formats:
 *   test              - List all tests
 *   test all          - Run all tests
 *   test <component>  - Run all tests for component
 *   test <comp>.<name> - Run specific test
 */
static void cmd_test(const char *arg) {
    /* Skip leading whitespace */
    while (*arg == CHAR_SPACE) arg++;

    /* No argument - list all tests */
    if (*arg == '\0') {
        test_list_all();
        return;
    }

    /* "all" - run all tests */
    if (strcmp(arg, "all") == 0) {
        test_run_all();
        return;
    }

    /* Check for component.name format */
    const char *dot = strchr(arg, '.');
    if (dot != NULL) {
        /* Extract component and name */
        static char component[32];
        static char name[32];

        /* Copy component (before dot) */
        int i = 0;
        const char *p = arg;
        while (p < dot && i < 31) {
            component[i++] = *p++;
        }
        component[i] = '\0';

        /* Copy name (after dot) */
        i = 0;
        p = dot + 1;
        while (*p && *p != ' ' && i < 31) {
            name[i++] = *p++;
        }
        name[i] = '\0';

        test_run_single(component, name);
    } else {
        /* Just component name - run all tests for that component */
        /* Extract just the component (stop at space) */
        static char component[32];
        int i = 0;
        while (*arg && *arg != ' ' && i < 31) {
            component[i++] = *arg++;
        }
        component[i] = '\0';

        test_run_component(component);
    }
}

/* =============================================================================
 * Filesystem Commands (ls, cd, cat)
 * =============================================================================
 */

/*
 * print_tar_entry - Callback for tar_list_dir to print one entry.
 */
static void print_tar_entry(const char *name, size_t size, int is_dir, void *ctx) {
    (void)ctx;

    /* Skip empty names and "./" entries */
    if (strlen(name) == 0 || strcmp(name, "./") == 0) {
        return;
    }

    if (is_dir) {
        puts("    <dir>  ");
        puts(name);
    } else {
        /* Right-align size in 8 chars */
        char size_buf[12];
        int len = 0;
        size_t tmp = size;
        do {
            size_buf[len++] = '0' + (tmp % 10);
            tmp /= 10;
        } while (tmp > 0);

        /* Print padding spaces */
        for (int i = len; i < 8; i++) putc(' ');
        /* Print digits in reverse */
        while (len > 0) putc(size_buf[--len]);

        puts("  ");
        puts(name);
    }
    puts("\n");
}

/*
 * cmd_ls - List directory contents from initrd.
 */
static void cmd_ls(const char *arg) {
    /* Skip leading whitespace */
    while (*arg == CHAR_SPACE) arg++;

    /* Use argument if provided, otherwise use current_path */
    const char *path = (*arg != '\0') ? arg : current_path;

    puts("\nContents of /");
    if (strlen(path) > 0) {
        puts(path);
    }
    puts(":\n");

    int count = tar_list_dir(path, print_tar_entry, NULL);

    if (count == 0) {
        puts("  (empty or not found)\n");
    }
    puts("\n");
}

/*
 * cmd_cd - Change current directory.
 */
static void cmd_cd(const char *arg) {
    /* Skip leading whitespace */
    while (*arg == CHAR_SPACE) arg++;

    char new_path[MAX_PATH_LEN];

    if (*arg == '\0') {
        /* No argument - go to root */
        current_path[0] = '\0';
        puts("\nChanged to /\n\n");
        return;
    }

    if (strcmp(arg, "..") == 0) {
        /* Go up one directory */
        size_t len = strlen(current_path);
        if (len == 0) {
            puts("\nAlready at root\n\n");
            return;
        }

        /* Copy current path to work with */
        strncpy(new_path, current_path, MAX_PATH_LEN - 1);
        new_path[MAX_PATH_LEN - 1] = '\0';
        len = strlen(new_path);

        /* Remove trailing slash if present */
        if (len > 0 && new_path[len-1] == '/') {
            new_path[len-1] = '\0';
            len--;
        }

        /* Find last slash and truncate there */
        char *last_slash = NULL;
        for (char *p = new_path; *p != '\0'; p++) {
            if (*p == '/') {
                last_slash = p;
            }
        }

        if (last_slash != NULL) {
            *last_slash = '\0';
        } else {
            /* No slash found - we're one level deep, go to root */
            new_path[0] = '\0';
        }

        /* Update current_path (going up is always valid) */
        strncpy(current_path, new_path, MAX_PATH_LEN - 1);
        current_path[MAX_PATH_LEN - 1] = '\0';
    } else if (arg[0] == '/') {
        /* Absolute path - build without leading slash */
        strncpy(new_path, arg + 1, MAX_PATH_LEN - 1);
        new_path[MAX_PATH_LEN - 1] = '\0';

        /* Remove trailing slash for consistency */
        size_t len = strlen(new_path);
        if (len > 0 && new_path[len-1] == '/') {
            new_path[len-1] = '\0';
        }

        /* Validate the path exists (empty = root is always valid) */
        if (new_path[0] != '\0') {
            const struct tar_file *dir = tar_find(new_path);
            if (dir == NULL || !dir->is_dir) {
                puts("\nError: not a directory: ");
                puts(arg);
                puts("\n\n");
                return;
            }
        }

        strncpy(current_path, new_path, MAX_PATH_LEN - 1);
        current_path[MAX_PATH_LEN - 1] = '\0';
    } else {
        /* Relative path - build full path first */
        size_t cur_len = strlen(current_path);

        if (cur_len > 0) {
            strncpy(new_path, current_path, MAX_PATH_LEN - 1);
            new_path[MAX_PATH_LEN - 1] = '\0';

            /* Add separator if needed */
            if (new_path[cur_len-1] != '/') {
                if (cur_len < MAX_PATH_LEN - 1) {
                    new_path[cur_len++] = '/';
                    new_path[cur_len] = '\0';
                }
            }
        } else {
            new_path[0] = '\0';
            cur_len = 0;
        }

        /* Append new path component */
        size_t i = 0;
        while (arg[i] != '\0' && cur_len + i < MAX_PATH_LEN - 1) {
            new_path[cur_len + i] = arg[i];
            i++;
        }
        new_path[cur_len + i] = '\0';

        /* Remove trailing slash for consistency */
        size_t len = strlen(new_path);
        if (len > 0 && new_path[len-1] == '/') {
            new_path[len-1] = '\0';
        }

        /* Validate the path exists */
        const struct tar_file *dir = tar_find(new_path);
        if (dir == NULL || !dir->is_dir) {
            puts("\nError: not a directory: ");
            puts(arg);
            puts("\n\n");
            return;
        }

        strncpy(current_path, new_path, MAX_PATH_LEN - 1);
        current_path[MAX_PATH_LEN - 1] = '\0';
    }

    puts("\nChanged to /");
    if (current_path[0] != '\0') {
        puts(current_path);
    }
    puts("\n\n");
}

/* =============================================================================
 * Command Dispatcher
 * =============================================================================
 */

/*
 * execute_command - Parse and execute the buffered command.
 *
 * Matches the command buffer against known commands and calls
 * the appropriate handler.
 */
static void execute_command(void) {
    /* Null-terminate the command string */
    command_buffer[command_length] = '\0';

    /* Empty command - just show prompt again */
    if (command_length == 0) {
        return;
    }

    /* Match command and dispatch to handler */
    if (strcmp(command_buffer, "help") == 0) {
        cmd_help();
    } else if (strcmp(command_buffer, "meminfo") == 0) {
        cmd_meminfo();
    } else if (strcmp(command_buffer, "alloc") == 0) {
        cmd_alloc();
    } else if (strncmp(command_buffer, "free ", 5) == 0) {
        cmd_free(command_buffer + 5);  /* Skip "free " prefix */
    } else if (strcmp(command_buffer, "free") == 0) {
        cmd_free("");  /* No argument - will show usage */
    } else if (strcmp(command_buffer, "crash") == 0) {
        cmd_crash();
    } else if (strcmp(command_buffer, "divzero") == 0) {
        cmd_divzero();
    } else if (strcmp(command_buffer, "clear") == 0) {
        cmd_clear();
    } else if (strncmp(command_buffer, "test ", 5) == 0) {
        cmd_test(command_buffer + 5);  /* Skip "test " prefix */
    } else if (strcmp(command_buffer, "test") == 0) {
        cmd_test("");  /* No argument - will list tests */
    } else if (strcmp(command_buffer, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(command_buffer, "last_exit") == 0) {
        cmd_last_exit();
    } else if (strncmp(command_buffer, "run ", 4) == 0) {
        cmd_run(command_buffer + 4);  /* Skip "run " prefix */
    } else if (strcmp(command_buffer, "ls") == 0) {
        cmd_ls("");
    } else if (strncmp(command_buffer, "ls ", 3) == 0) {
        cmd_ls(command_buffer + 3);
    } else if (strcmp(command_buffer, "cd") == 0) {
        cmd_cd("");
    } else if (strncmp(command_buffer, "cd ", 3) == 0) {
        cmd_cd(command_buffer + 3);
    } else {
        puts("\nUnknown command: ");
        puts(command_buffer);
        puts("\nType 'help' for available commands.\n\n");
    }

    /* Reset command buffer for next input */
    command_length = 0;
}

/* =============================================================================
 * Shell Public API
 * =============================================================================
 */

/*
 * shell_init - Display welcome banner and initial prompt.
 */
void shell_init(void) {
    puts("\n");
    puts("========================================\n");
    puts("       Seed OS Shell v0.1\n");
    puts("  Type 'help' for available commands\n");
    puts("========================================\n");
    shell_prompt();
}

/*
 * shell_prompt - Display the command prompt with current path.
 */
void shell_prompt(void) {
    puts("seed:");
    puts("/");
    if (strlen(current_path) > 0) {
        puts(current_path);
    }
    puts("> ");
}

/*
 * shell_input - Process one character of keyboard input.
 *
 * Handles:
 *   - Enter: Execute command and show new prompt
 *   - Backspace: Remove last character
 *   - Printable: Add to buffer and echo
 */
void shell_input(char c) {
    if (c == CHAR_NEWLINE || c == CHAR_RETURN) {
        /* Enter pressed - execute the buffered command */
        puts("\n");
        execute_command();
        shell_prompt();
    } else if (c == CHAR_BACKSPACE || c == CHAR_DELETE) {
        /* Backspace - remove last character if buffer not empty */
        if (command_length > 0) {
            command_length--;
            /* Erase character: move back, print space, move back again */
            puts("\b \b");
        }
    } else if (c >= CHAR_PRINTABLE_MIN && c <= CHAR_PRINTABLE_MAX) {
        /* Printable character - add to buffer if room */
        if (command_length < COMMAND_BUFFER_SIZE - 1) {
            command_buffer[command_length++] = c;
            putc(c);  /* Echo character to screen */
        }
        /* If buffer full, silently ignore additional characters */
    }
    /* Other characters (control chars, etc.) are ignored */
}
