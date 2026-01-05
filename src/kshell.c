/*
 * kshell.c - Kernel Shell
 *
 * Minimal command-line interface for kernel-space interaction.
 * Features:
 *   - Command parsing with arguments
 *   - Command history navigation (up/down arrows)
 *   - Basic line editing (backspace)
 */

#include "kshell.h"
#include "keyboard.h"
#include "kprintf.h"
#include "terminal.h"
#include "console.h"
#include "pmm.h"
#include "heap.h"
#include "types.h"
#include "kthread.h"
#include "config.h"
#include "matrix.h"
#include "sync.h"
#include "apic.h"

/* =============================================================================
 * Module State
 * =============================================================================
 */

/* Input buffer */
static char input_buffer[KSHELL_MAX_INPUT];
static int input_len;

/* History - circular buffer */
static char history[KSHELL_HISTORY_SIZE][KSHELL_MAX_INPUT];
static int history_count;       /* Total commands stored (up to HISTORY_SIZE) */
static int history_pos;         /* Next write position in circular buffer */
static int history_browse;      /* Current browse index (-1 = not browsing) */
static char saved_input[KSHELL_MAX_INPUT];  /* Saved input when browsing history */

/* =============================================================================
 * Forward Declarations
 * =============================================================================
 */

typedef void (*cmd_handler_t)(int argc, char *argv[]);

typedef struct {
    const char *name;
    const char *help;
    cmd_handler_t handler;
} command_t;

/* Command handlers */
static void cmd_help(int argc, char *argv[]);
static void cmd_clear(int argc, char *argv[]);
static void cmd_echo(int argc, char *argv[]);
static void cmd_version(int argc, char *argv[]);
static void cmd_meminfo(int argc, char *argv[]);
static void cmd_reboot(int argc, char *argv[]);
static void cmd_matrix(int argc, char *argv[]);
static void cmd_spintest(int argc, char *argv[]);
static void cmd_mutextest(int argc, char *argv[]);
static void cmd_condtest(int argc, char *argv[]);

/* Command table */
static const command_t commands[] = {
    {"help",      "Show available commands",      cmd_help},
    {"clear",     "Clear the screen",             cmd_clear},
    {"echo",      "Echo arguments to screen",     cmd_echo},
    {"version",   "Show SeedOS version",          cmd_version},
    {"meminfo",   "Show memory statistics",       cmd_meminfo},
    {"reboot",    "Reboot the system",            cmd_reboot},
    {"matrix",    "Toggle Matrix rain demo",      cmd_matrix},
    {"spintest",  "Test spinlock (busy-wait)",    cmd_spintest},
    {"mutextest", "Test mutex (sleep-wait)",      cmd_mutextest},
    {"condtest",  "Test condition variables",     cmd_condtest},
    {NULL, NULL, NULL}  /* Sentinel */
};

/* =============================================================================
 * String Utilities
 * =============================================================================
 */

static int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, int max_len) {
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* =============================================================================
 * Command Handlers (Stubs)
 * =============================================================================
 */

static void cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    kprintf("Available commands:\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        kprintf("  %-12s %s\n", commands[i].name, commands[i].help);
    }
}

static void cmd_clear(int argc, char *argv[]) {
    (void)argc; (void)argv;
    terminal_clear(terminal_get_active());
}

static void cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        kprintf("%s%s", argv[i], (i < argc - 1) ? " " : "");
    }
    kprintf("\n");
}

static void cmd_version(int argc, char *argv[]) {
    (void)argc; (void)argv;
    kprintf("SeedOS v0.1\n");
}

static void cmd_meminfo(int argc, char *argv[]) {
    (void)argc; (void)argv;
    kprintf("Heap: %llu used, %llu free\n",
            (uint64_t)kheap_get_used(), (uint64_t)kheap_get_free());
    kprintf("PMM:  %llu/%llu pages free\n",
            pmm_get_free_pages(), pmm_get_usable_pages());
}

static void cmd_reboot(int argc, char *argv[]) {
    (void)argc; (void)argv;
    kprintf("Reboot not implemented yet\n");
}

static void cmd_matrix(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (matrix_is_running()) {
        matrix_stop();
        kprintf("Matrix rain stopped.\n");
    } else {
        kprintf("Starting Matrix rain... (press any key to stop)\n");
        matrix_start();

        /* Block until any key is pressed */
        keyboard_read();

        /* Stop the demo */
        matrix_stop();
        kprintf("Matrix rain stopped.\n");
    }
}

/* =============================================================================
 * Spinlock Test
 *
 * Two threads increment a shared counter 1000 times each.
 * With spinlock: final count = 2000
 * Without: likely less due to race conditions
 * =============================================================================
 */

static volatile int spin_counter;
static spinlock_t spin_test_lock;
static volatile int spin_threads_done;

static void spin_worker(void *arg) {
    int id = (int)(uint64_t)arg;
    for (int i = 0; i < 1000; i++) {
        spin_lock(&spin_test_lock);
        spin_counter++;
        spin_unlock(&spin_test_lock);
    }
    kprintf("  Thread %d finished\n", id);
    spin_threads_done++;
}

static void cmd_spintest(int argc, char *argv[]) {
    (void)argc; (void)argv;

    kprintf("Spinlock Test: 2 threads, 1000 increments each\n");
    kprintf("Expected result: 2000\n\n");

    spin_counter = 0;
    spin_threads_done = 0;
    spin_init(&spin_test_lock);

    kthread_create("spin-worker-1", spin_worker, (void*)1);
    kthread_create("spin-worker-2", spin_worker, (void*)2);

    /* Wait for both threads to finish */
    while (spin_threads_done < 2) {
        kthread_sleep(10);
    }

    kprintf("\nFinal counter value: %d\n", spin_counter);
    if (spin_counter == 2000) {
        kprintf("SUCCESS: Spinlock protected the counter!\n");
    } else {
        kprintf("FAILURE: Race condition detected\n");
    }
}

/* =============================================================================
 * Mutex Test
 *
 * Similar to spinlock, but threads sleep instead of spinning.
 * We add a small delay inside the critical section to show the difference.
 * =============================================================================
 */

static volatile int mutex_counter;
static mutex_t mutex_test_lock;
static volatile int mutex_threads_done;

static void mutex_worker(void *arg) {
    int id = (int)(uint64_t)arg;
    for (int i = 0; i < 100; i++) {
        mutex_lock(&mutex_test_lock);
        mutex_counter++;
        /* Small work inside critical section */
        for (volatile int j = 0; j < 100; j++);
        mutex_unlock(&mutex_test_lock);
    }
    kprintf("  Thread %d finished\n", id);
    mutex_threads_done++;
}

static void cmd_mutextest(int argc, char *argv[]) {
    (void)argc; (void)argv;

    kprintf("Mutex Test: 2 threads, 100 increments each\n");
    kprintf("Expected result: 200\n\n");

    mutex_counter = 0;
    mutex_threads_done = 0;
    mutex_init(&mutex_test_lock);

    kthread_create("mutex-worker-1", mutex_worker, (void*)1);
    kthread_create("mutex-worker-2", mutex_worker, (void*)2);

    /* Wait for both threads to finish */
    while (mutex_threads_done < 2) {
        kthread_sleep(10);
    }

    kprintf("\nFinal counter value: %d\n", mutex_counter);
    if (mutex_counter == 200) {
        kprintf("SUCCESS: Mutex protected the counter!\n");
    } else {
        kprintf("FAILURE: Race condition detected\n");
    }
}

/* =============================================================================
 * Condition Variable Test
 *
 * Producer/consumer with a bounded buffer of size 5.
 * Producer adds 10 items, consumer takes them.
 * =============================================================================
 */

#define BUFFER_SIZE 5
static int buffer[BUFFER_SIZE];
static int buffer_count;
static int buffer_in;
static int buffer_out;
static mutex_t buffer_mutex;
static cond_t buffer_not_empty;
static cond_t buffer_not_full;
static volatile int producer_done;
static volatile int consumer_done;

static void producer_thread(void *arg) {
    (void)arg;
    for (int i = 1; i <= 10; i++) {
        mutex_lock(&buffer_mutex);

        /* Wait while buffer is full */
        while (buffer_count == BUFFER_SIZE) {
            kprintf("  Producer: buffer full, waiting...\n");
            cond_wait(&buffer_not_full, &buffer_mutex);
        }

        /* Add item */
        buffer[buffer_in] = i;
        buffer_in = (buffer_in + 1) % BUFFER_SIZE;
        buffer_count++;
        kprintf("  Producer: added item %d (count=%d)\n", i, buffer_count);

        /* Signal consumer */
        cond_signal(&buffer_not_empty);

        mutex_unlock(&buffer_mutex);

        /* Small delay between productions */
        kthread_sleep(50);
    }
    kprintf("  Producer: done!\n");
    producer_done = 1;
}

static void consumer_thread(void *arg) {
    (void)arg;
    int consumed = 0;
    while (consumed < 10) {
        mutex_lock(&buffer_mutex);

        /* Wait while buffer is empty */
        while (buffer_count == 0 && !producer_done) {
            kprintf("  Consumer: buffer empty, waiting...\n");
            cond_wait(&buffer_not_empty, &buffer_mutex);
        }

        if (buffer_count > 0) {
            /* Remove item */
            int item = buffer[buffer_out];
            buffer_out = (buffer_out + 1) % BUFFER_SIZE;
            buffer_count--;
            consumed++;
            kprintf("  Consumer: got item %d (count=%d)\n", item, buffer_count);

            /* Signal producer */
            cond_signal(&buffer_not_full);
        }

        mutex_unlock(&buffer_mutex);

        /* Small delay between consumptions */
        kthread_sleep(80);
    }
    kprintf("  Consumer: done! Got %d items\n", consumed);
    consumer_done = 1;
}

static void cmd_condtest(int argc, char *argv[]) {
    (void)argc; (void)argv;

    kprintf("Condition Variable Test: Producer/Consumer\n");
    kprintf("Buffer size: %d, Items to produce: 10\n\n", BUFFER_SIZE);

    /* Initialize state */
    buffer_count = 0;
    buffer_in = 0;
    buffer_out = 0;
    producer_done = 0;
    consumer_done = 0;
    mutex_init(&buffer_mutex);
    cond_init(&buffer_not_empty);
    cond_init(&buffer_not_full);

    kthread_create("producer", producer_thread, NULL);
    kthread_create("consumer", consumer_thread, NULL);

    /* Wait for both to finish */
    while (!producer_done || !consumer_done) {
        kthread_sleep(50);
    }

    kprintf("\nSUCCESS: Producer/Consumer completed!\n");
}

/* =============================================================================
 * Command Parsing
 * =============================================================================
 */

static int parse_command(char *input, char *argv[], int max_args) {
    int argc = 0;
    char *p = input;

    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p == ' ') p++;
        if (*p == '\0') break;

        /* Mark start of argument */
        argv[argc++] = p;

        /* Find end of argument */
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }

    return argc;
}

static void execute_command(void) {
    char *argv[KSHELL_MAX_ARGS];
    int argc = parse_command(input_buffer, argv, KSHELL_MAX_ARGS);

    if (argc == 0) return;

    /* Search for matching command */
    for (int i = 0; commands[i].name != NULL; i++) {
        if (str_equal(argv[0], commands[i].name)) {
            commands[i].handler(argc, argv);
            return;
        }
    }

    /* Command not found */
    terminal_set_color(terminal_get_active(), KSHELL_ERROR_COLOR);
    kprintf("Unknown command: %s\n", argv[0]);
    terminal_set_color(terminal_get_active(), KSHELL_INPUT_COLOR);
}

/* =============================================================================
 * History Management
 * =============================================================================
 */

static void add_to_history(void) {
    if (input_len == 0) return;

    /* Copy input to history at current position */
    str_copy(history[history_pos], input_buffer, KSHELL_MAX_INPUT);

    /* Advance position circularly */
    history_pos = (history_pos + 1) % KSHELL_HISTORY_SIZE;

    /* Track total count (max at HISTORY_SIZE) */
    if (history_count < KSHELL_HISTORY_SIZE) {
        history_count++;
    }
}

static int get_history_index(int offset) {
    /*
     * offset = 0: most recent command
     * offset = 1: second most recent
     * etc.
     *
     * history_pos points to the NEXT write position.
     * Most recent is at (history_pos - 1 + SIZE) % SIZE.
     */
    if (offset >= history_count) return -1;

    int idx = (history_pos - 1 - offset + KSHELL_HISTORY_SIZE) % KSHELL_HISTORY_SIZE;
    return idx;
}

static void clear_input_line(void) {
    /* Erase current input from screen by backspacing */
    for (int i = 0; i < input_len; i++) {
        kprintf("\b \b");
    }
}

static void history_prev(void) {
    if (history_count == 0) return;

    if (history_browse == -1) {
        /* Start browsing - save current input */
        str_copy(saved_input, input_buffer, KSHELL_MAX_INPUT);
        history_browse = 0;
    } else {
        /* Move to older entry */
        if (history_browse < history_count - 1) {
            history_browse++;
        } else {
            return;  /* Already at oldest */
        }
    }

    /* Get history entry and display it */
    int idx = get_history_index(history_browse);
    if (idx >= 0) {
        clear_input_line();
        str_copy(input_buffer, history[idx], KSHELL_MAX_INPUT);
        input_len = 0;
        while (input_buffer[input_len]) input_len++;
        kprintf("%s", input_buffer);
    }
}

static void history_next(void) {
    if (history_browse == -1) return;  /* Not browsing */

    if (history_browse > 0) {
        /* Move to newer entry */
        history_browse--;

        int idx = get_history_index(history_browse);
        if (idx >= 0) {
            clear_input_line();
            str_copy(input_buffer, history[idx], KSHELL_MAX_INPUT);
            input_len = 0;
            while (input_buffer[input_len]) input_len++;
            kprintf("%s", input_buffer);
        }
    } else {
        /* Restore saved input */
        clear_input_line();
        str_copy(input_buffer, saved_input, KSHELL_MAX_INPUT);
        input_len = 0;
        while (input_buffer[input_len]) input_len++;
        kprintf("%s", input_buffer);
        history_browse = -1;  /* Stop browsing */
    }
}

/* =============================================================================
 * Line Editing
 * =============================================================================
 */

static void handle_backspace(void) {
    if (input_len > 0) {
        input_len--;
        input_buffer[input_len] = '\0';
        kprintf("\b \b");  /* Erase character on screen */
    }
}

static void clear_input(void) {
    input_len = 0;
    input_buffer[0] = '\0';
    history_browse = -1;
}

static void print_prompt(void) {
    terminal_set_color(terminal_get_active(), KSHELL_PROMPT_COLOR);
    kprintf("%s", KSHELL_PROMPT);
    terminal_set_color(terminal_get_active(), KSHELL_INPUT_COLOR);
}

/* =============================================================================
 * Public API
 * =============================================================================
 */

void kshell_init(void) {
    /* Clear input buffer */
    input_len = 0;
    input_buffer[0] = '\0';

    /* Clear history */
    history_count = 0;
    history_pos = 0;
    history_browse = -1;

    /* Clear saved input */
    saved_input[0] = '\0';
}

void kshell_run(void) {
    kprintf("\nWelcome to SeedOS!\n");
    kprintf("Type 'help' for available commands.\n\n");
    print_prompt();

    for (;;) {
        int c = keyboard_getchar();
        if (c == -1) {
#if CONFIG_KTHREAD_PREEMPTIVE == 0
            /* Cooperative mode: yield to let other threads run */
            kthread_yield();
#else
            /* Preemptive mode: just wait for interrupt */
            __asm__ volatile ("hlt");
#endif
            continue;
        }

        /* Handle Page Up/Down for console scrolling */
        if (c == KEY_PAGEUP) {
            int cols, rows;
            console_get_dimensions(&cols, &rows);
            console_scroll_back(rows - 1);
            continue;
        } else if (c == KEY_PAGEDOWN) {
            int cols, rows;
            console_get_dimensions(&cols, &rows);
            console_scroll_forward(rows - 1);
            continue;
        }

        /* Return to live view on any other key when scrolled back */
        if (console_is_scrolled_back()) {
            console_scroll_to_bottom();
        }

        if (c == KEY_ENTER) {
            kprintf("\n");
            if (input_len > 0) {
                /* Add to history before parsing (which modifies the buffer) */
                add_to_history();
                execute_command();
            }
            clear_input();
            print_prompt();
        } else if (c == KEY_BACKSPACE) {
            handle_backspace();
        } else if (c == KEY_UP) {
            history_prev();
        } else if (c == KEY_DOWN) {
            history_next();
        } else if (c >= 32 && c < 127) {
            /* Printable character */
            if (input_len < KSHELL_MAX_INPUT - 1) {
                input_buffer[input_len++] = (char)c;
                input_buffer[input_len] = '\0';
                kprintf("%c", c);
            }
        }
        /* Ignore other special keys */
    }
}
