/*
 * shell.c - Simple kernel shell
 */

#include "shell.h"
#include "pmm.h"
#include "console.h"
#include <stdint.h>
#include <stddef.h>

/* Command buffer */
#define CMD_BUFFER_SIZE 128
static char cmd_buffer[CMD_BUFFER_SIZE];
static int cmd_pos = 0;

/* Simple string comparison */
static int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* Check if string starts with prefix */
static int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return 1;
}

/* Parse hex number from string */
static uint64_t parse_hex(const char *s) {
    uint64_t result = 0;
    
    /* Skip "0x" prefix if present */
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
            break;
        }
        
        result = (result << 4) | digit;
        s++;
    }
    
    return result;
}

/* Command handlers */
static void cmd_help(void) {
    puts("\nAvailable commands:\n");
    puts("  help          - Show this help\n");
    puts("  meminfo       - Show memory statistics\n");
    puts("  alloc         - Allocate a physical page\n");
    puts("  free <addr>   - Free a physical page (hex address)\n");
    puts("  crash         - Trigger a page fault\n");
    puts("  divzero       - Trigger divide by zero\n");
    puts("  clear         - Clear screen\n");
    puts("\n");
}

static void cmd_meminfo(void) {
    uint64_t total = pmm_get_total_pages();
    uint64_t free = pmm_get_free_pages();
    uint64_t used = total - free;
    
    puts("\nPhysical Memory:\n");
    puts("  Total pages:  ");
    put_dec(total);
    puts(" (");
    put_dec(total * 4 / 1024);
    puts(" MB)\n");
    
    puts("  Free pages:   ");
    put_dec(free);
    puts(" (");
    put_dec(free * 4 / 1024);
    puts(" MB)\n");
    
    puts("  Used pages:   ");
    put_dec(used);
    puts(" (");
    put_dec(used * 4 / 1024);
    puts(" MB)\n");
    puts("\n");
}

static void cmd_alloc(void) {
    uint64_t page = pmm_alloc();
    if (page == 0) {
        puts("\nError: Out of memory!\n\n");
    } else {
        puts("\nAllocated page at: ");
        put_hex(page);
        puts("\n\n");
    }
}

static void cmd_free(const char *arg) {
    /* Skip whitespace */
    while (*arg == ' ') arg++;
    
    if (*arg == '\0') {
        puts("\nUsage: free <hex_address>\n");
        puts("Example: free 0x5000\n\n");
        return;
    }
    
    uint64_t addr = parse_hex(arg);
    
    if (addr == 0) {
        puts("\nError: Cannot free address 0\n\n");
        return;
    }
    
    if (addr & 0xFFF) {
        puts("\nError: Address must be page-aligned (multiple of 0x1000)\n\n");
        return;
    }
    
    pmm_free(addr);
    puts("\nFreed page at: ");
    put_hex(addr);
    puts("\n\n");
}

static void cmd_crash(void) {
    puts("\nTriggering page fault...\n");
    volatile char *bad_ptr = (volatile char *)0xDEADBEEF;
    *bad_ptr = 0;  /* This will fault */
}

static void cmd_divzero(void) {
    puts("\nTriggering divide by zero...\n");
    volatile int x = 1;
    volatile int y = 0;
    volatile int z = x / y;
    (void)z;
}

static void cmd_clear(void) {
    /* ANSI escape sequence to clear screen */
    puts("\033[2J\033[H");
}

/* Process a complete command */
static void process_command(void) {
    /* Null-terminate the command */
    cmd_buffer[cmd_pos] = '\0';
    
    /* Empty command - just show prompt again */
    if (cmd_pos == 0) {
        return;
    }
    
    /* Parse and execute command */
    if (str_equal(cmd_buffer, "help")) {
        cmd_help();
    } else if (str_equal(cmd_buffer, "meminfo")) {
        cmd_meminfo();
    } else if (str_equal(cmd_buffer, "alloc")) {
        cmd_alloc();
    } else if (str_starts_with(cmd_buffer, "free ")) {
        cmd_free(cmd_buffer + 5);
    } else if (str_equal(cmd_buffer, "free")) {
        cmd_free("");
    } else if (str_equal(cmd_buffer, "crash")) {
        cmd_crash();
    } else if (str_equal(cmd_buffer, "divzero")) {
        cmd_divzero();
    } else if (str_equal(cmd_buffer, "clear")) {
        cmd_clear();
    } else {
        puts("\nUnknown command: ");
        puts(cmd_buffer);
        puts("\nType 'help' for available commands.\n\n");
    }
    
    /* Reset buffer */
    cmd_pos = 0;
}

void shell_init(void) {
    puts("\n");
    puts("========================================\n");
    puts("       MyOS Shell v0.1\n");
    puts("  Type 'help' for available commands\n");
    puts("========================================\n");
    shell_prompt();
}

void shell_prompt(void) {
    puts("myos> ");
}

void shell_input(char c) {
    if (c == '\n' || c == '\r') {
        /* Enter pressed - execute command */
        puts("\n");
        process_command();
        shell_prompt();
    } else if (c == '\b' || c == 127) {
        /* Backspace */
        if (cmd_pos > 0) {
            cmd_pos--;
            /* Move cursor back, overwrite with space, move back again */
            puts("\b \b");
        }
    } else if (c >= 32 && c < 127) {
        /* Printable character */
        if (cmd_pos < CMD_BUFFER_SIZE - 1) {
            cmd_buffer[cmd_pos++] = c;
            putc(c);  /* Echo */
        }
    }
}
