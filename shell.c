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
#include "pmm.h"
#include "console.h"
#include "tests.h"
#include <stdint.h>
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
 * String Helper Functions
 *
 * Simple string operations. We can't use libc, so we implement our own.
 * =============================================================================
 */

/*
 * strings_equal - Check if two null-terminated strings are identical.
 */
static int strings_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/*
 * string_starts_with - Check if string begins with the given prefix.
 */
static int string_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return 1;
}

/*
 * parse_hex_string - Parse a hexadecimal number from a string.
 *
 * Accepts with or without "0x" prefix.
 * Returns: The parsed value, or 0 if invalid.
 */
static uint64_t parse_hex_string(const char *s) {
    uint64_t result = 0;

    /* Skip optional "0x" or "0X" prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s) {
        char c = *s;
        uint64_t digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            break;  /* Stop at first non-hex character */
        }

        result = (result << 4) | digit;
        s++;
    }

    return result;
}

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
    puts("\nAvailable commands:\n");
    puts("  help          - Show this help\n");
    puts("  meminfo       - Show memory statistics\n");
    puts("  alloc         - Allocate a physical page\n");
    puts("  free <addr>   - Free a physical page (hex address)\n");
    puts("  clear         - Clear screen\n");
    puts("\nTests:\n");
    puts("  test memmap   - Display memory map from bootloader\n");
    puts("  test vmm      - Run VMM test suite\n");
    puts("  test user     - Run userspace program\n");
    puts("\nDebugging:\n");
    puts("  crash         - Trigger a page fault\n");
    puts("  divzero       - Trigger divide by zero\n");
    puts("\n");
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

    uint64_t addr = parse_hex_string(arg);

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
 * cmd_test - Run a test suite by name.
 *
 * @arg: The test name (after "test ").
 */
static void cmd_test(const char *arg) {
    /* Skip leading whitespace */
    while (*arg == CHAR_SPACE) arg++;

    if (*arg == '\0') {
        puts("\nUsage: test <name>\n");
        puts("Available tests: memmap, vmm, user\n\n");
        return;
    }

    if (strings_equal(arg, "memmap")) {
        test_memmap();
    } else if (strings_equal(arg, "vmm")) {
        test_vmm();
    } else if (strings_equal(arg, "user")) {
        test_user();
    } else {
        puts("\nUnknown test: ");
        puts(arg);
        puts("\nAvailable tests: memmap, vmm, user\n\n");
    }
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
    if (strings_equal(command_buffer, "help")) {
        cmd_help();
    } else if (strings_equal(command_buffer, "meminfo")) {
        cmd_meminfo();
    } else if (strings_equal(command_buffer, "alloc")) {
        cmd_alloc();
    } else if (string_starts_with(command_buffer, "free ")) {
        cmd_free(command_buffer + 5);  /* Skip "free " prefix */
    } else if (strings_equal(command_buffer, "free")) {
        cmd_free("");  /* No argument - will show usage */
    } else if (strings_equal(command_buffer, "crash")) {
        cmd_crash();
    } else if (strings_equal(command_buffer, "divzero")) {
        cmd_divzero();
    } else if (strings_equal(command_buffer, "clear")) {
        cmd_clear();
    } else if (string_starts_with(command_buffer, "test ")) {
        cmd_test(command_buffer + 5);  /* Skip "test " prefix */
    } else if (strings_equal(command_buffer, "test")) {
        cmd_test("");  /* No argument - will show usage */
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
 * shell_prompt - Display the command prompt.
 */
void shell_prompt(void) {
    puts("seed> ");
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
