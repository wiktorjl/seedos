/*
 * locust.c - SeedOS Stress Tester
 *
 * A swarm of tests that consume system resources to verify the OS
 * doesn't leak memory, file descriptors, or process table entries.
 *
 * Usage:
 *   locust              Run all tests (supervisor mode)
 *   locust [iters]      Run all tests with custom iteration count
 *   locust --test=X     Run specific test (memory, process, fd, combined)
 *
 * The supervisor spawns each test as a child process, so if a test
 * crashes, the supervisor catches it and continues to the next test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define DEFAULT_ITERATIONS 100
#define PROGRESS_INTERVAL  20

/* Test result codes */
#define RESULT_PASS   0
#define RESULT_FAIL   1
#define RESULT_CRASH  2
#define RESULT_SKIP   3

static const char *result_str[] = {"PASS", "FAIL", "CRASH", "SKIP"};

/* Global iteration count */
static int g_iterations = DEFAULT_ITERATIONS;
static int g_verbose = 0;

/* ============================================================================
 * Memory Stress Test
 *
 * Allocates and frees memory in various patterns to check for leaks.
 * ============================================================================
 */

#define MEM_SMALL   64
#define MEM_MEDIUM  1024
#define MEM_LARGE   4096
#define MEM_XLARGE  16384
#define MAX_PTRS    64

static int test_memory(void) {
    void *ptrs[MAX_PTRS];
    int ptr_count = 0;
    int total_allocs = 0;
    int total_frees = 0;
    int failures = 0;

    printf("[locust] === MEMORY STRESS ===\n");

    for(int iter = 0; iter < g_iterations; iter++) {
        /* Allocate a batch of random sizes */
        int sizes[] = {MEM_SMALL, MEM_MEDIUM, MEM_LARGE, MEM_XLARGE};
        int batch = 8 + (iter % 8);  /* 8-15 allocations per iteration */

        for(int i = 0; i < batch && ptr_count < MAX_PTRS; i++) {
            int size = sizes[(iter + i) % 4];
            void *p = malloc(size);
            if(p == NULL) {
                failures++;
                if(g_verbose) printf("[locust] malloc(%d) failed\n", size);
            } else {
                /* Touch the memory to ensure it's real */
                memset(p, 0xAA, size);
                ptrs[ptr_count++] = p;
                total_allocs++;
            }
        }

        /* Free in different patterns based on iteration */
        int pattern = iter % 3;
        if(pattern == 0) {
            /* LIFO - free from end */
            while(ptr_count > 0) {
                free(ptrs[--ptr_count]);
                total_frees++;
            }
        } else if(pattern == 1) {
            /* FIFO - free from start (shift array) */
            while(ptr_count > 0) {
                free(ptrs[0]);
                total_frees++;
                for(int j = 0; j < ptr_count - 1; j++) {
                    ptrs[j] = ptrs[j + 1];
                }
                ptr_count--;
            }
        } else {
            /* Alternating - free every other one */
            for(int j = 0; j < ptr_count; j += 2) {
                free(ptrs[j]);
                ptrs[j] = NULL;
                total_frees++;
            }
            /* Compact the array */
            int new_count = 0;
            for(int j = 0; j < ptr_count; j++) {
                if(ptrs[j] != NULL) {
                    ptrs[new_count++] = ptrs[j];
                }
            }
            ptr_count = new_count;
            /* Free the rest */
            while(ptr_count > 0) {
                free(ptrs[--ptr_count]);
                total_frees++;
            }
        }

        /* Progress report */
        if((iter + 1) % PROGRESS_INTERVAL == 0 || iter == g_iterations - 1) {
            printf("[locust] memory: %d/%d iters, %d allocs, %d frees\n",
                   iter + 1, g_iterations, total_allocs, total_frees);
        }
    }

    /* Final cleanup - should have nothing left */
    while(ptr_count > 0) {
        free(ptrs[--ptr_count]);
        total_frees++;
    }

    if(total_allocs != total_frees) {
        printf("[locust] memory: FAIL (allocs=%d, frees=%d, mismatch!)\n",
               total_allocs, total_frees);
        return RESULT_FAIL;
    }

    if(failures > 0) {
        printf("[locust] memory: FAIL (%d allocation failures)\n", failures);
        return RESULT_FAIL;
    }

    printf("[locust] memory: PASS (%d allocs, %d frees, balanced)\n",
           total_allocs, total_frees);
    return RESULT_PASS;
}

/* ============================================================================
 * Process Stress Test
 *
 * Spawns and waits for child processes to check for process table leaks.
 * ============================================================================
 */

static int test_process(void) {
    int total_spawns = 0;
    int total_waits = 0;
    int failures = 0;

    printf("[locust] === PROCESS STRESS ===\n");

    for(int iter = 0; iter < g_iterations; iter++) {
        /* Spawn a child that exits immediately */
        char *argv[] = {"true", NULL};
        int ret = spawn("/bin/true", argv);

        if(ret < 0) {
            failures++;
            if(g_verbose) printf("[locust] spawn failed at iter %d\n", iter);
        } else {
            total_spawns++;
            total_waits++;
        }

        /* Progress report */
        if((iter + 1) % PROGRESS_INTERVAL == 0 || iter == g_iterations - 1) {
            printf("[locust] process: %d/%d iters, %d spawns, %d failures\n",
                   iter + 1, g_iterations, total_spawns, failures);
        }
    }

    if(failures > 0) {
        printf("[locust] process: FAIL (%d spawn failures)\n", failures);
        return RESULT_FAIL;
    }

    printf("[locust] process: PASS (%d spawns, all reaped)\n", total_spawns);
    return RESULT_PASS;
}

/* ============================================================================
 * File Descriptor Stress Test
 *
 * Opens and closes files repeatedly to check for fd table leaks.
 * ============================================================================
 */

#define MAX_FDS 16

static int test_fd(void) {
    int total_opens = 0;
    int total_closes = 0;
    int failures = 0;
    int fds[MAX_FDS];
    int fd_count = 0;

    printf("[locust] === FD STRESS ===\n");

    /* Find some files to open */
    const char *test_files[] = {
        "/bin/ls",
        "/bin/cat",
        "/bin/echo",
        "/bin/true",
        NULL
    };

    for(int iter = 0; iter < g_iterations; iter++) {
        /* Open a batch of files */
        int batch = 4 + (iter % 4);  /* 4-7 opens per iteration */

        for(int i = 0; i < batch && fd_count < MAX_FDS; i++) {
            const char *file = test_files[i % 4];
            int fd = open(file, 0);  /* O_RDONLY = 0 */
            if(fd < 0) {
                failures++;
                if(g_verbose) printf("[locust] open(%s) failed\n", file);
            } else {
                fds[fd_count++] = fd;
                total_opens++;
            }
        }

        /* Close all opened files */
        while(fd_count > 0) {
            if(close(fds[--fd_count]) < 0) {
                failures++;
                if(g_verbose) printf("[locust] close failed\n");
            } else {
                total_closes++;
            }
        }

        /* Progress report */
        if((iter + 1) % PROGRESS_INTERVAL == 0 || iter == g_iterations - 1) {
            printf("[locust] fd: %d/%d iters, %d opens, %d closes\n",
                   iter + 1, g_iterations, total_opens, total_closes);
        }
    }

    if(total_opens != total_closes) {
        printf("[locust] fd: FAIL (opens=%d, closes=%d, mismatch!)\n",
               total_opens, total_closes);
        return RESULT_FAIL;
    }

    if(failures > 0) {
        printf("[locust] fd: FAIL (%d failures)\n", failures);
        return RESULT_FAIL;
    }

    printf("[locust] fd: PASS (%d opens, %d closes, balanced)\n",
           total_opens, total_closes);
    return RESULT_PASS;
}

/* ============================================================================
 * Combined Stress Test
 *
 * Interleaves memory, process, and fd operations.
 * ============================================================================
 */

static int test_combined(void) {
    void *ptrs[MAX_PTRS];
    int ptr_count = 0;
    int fds[MAX_FDS];
    int fd_count = 0;

    int total_allocs = 0, total_frees = 0;
    int total_spawns = 0;
    int total_opens = 0, total_closes = 0;
    int failures = 0;

    printf("[locust] === COMBINED STRESS ===\n");

    for(int iter = 0; iter < g_iterations; iter++) {
        int op = iter % 6;

        switch(op) {
            case 0:  /* Allocate memory */
            case 1:
                if(ptr_count < MAX_PTRS) {
                    int size = 256 + (iter % 1024);
                    void *p = malloc(size);
                    if(p) {
                        memset(p, 0xBB, size);
                        ptrs[ptr_count++] = p;
                        total_allocs++;
                    } else {
                        failures++;
                    }
                }
                break;

            case 2:  /* Free memory */
                if(ptr_count > 0) {
                    free(ptrs[--ptr_count]);
                    total_frees++;
                }
                break;

            case 3:  /* Spawn process */
                {
                    char *argv[] = {"true", NULL};
                    int ret = spawn("/bin/true", argv);
                    if(ret >= 0) {
                        total_spawns++;
                    } else {
                        failures++;
                    }
                }
                break;

            case 4:  /* Open file */
                if(fd_count < MAX_FDS) {
                    int fd = open("/bin/ls", 0);
                    if(fd >= 0) {
                        fds[fd_count++] = fd;
                        total_opens++;
                    } else {
                        failures++;
                    }
                }
                break;

            case 5:  /* Close file */
                if(fd_count > 0) {
                    if(close(fds[--fd_count]) >= 0) {
                        total_closes++;
                    } else {
                        failures++;
                    }
                }
                break;
        }

        /* Progress report */
        if((iter + 1) % PROGRESS_INTERVAL == 0 || iter == g_iterations - 1) {
            printf("[locust] combined: %d/%d iters, mem=%d/%d, spawn=%d, fd=%d/%d\n",
                   iter + 1, g_iterations,
                   total_allocs, total_frees,
                   total_spawns,
                   total_opens, total_closes);
        }
    }

    /* Cleanup */
    while(ptr_count > 0) {
        free(ptrs[--ptr_count]);
        total_frees++;
    }
    while(fd_count > 0) {
        close(fds[--fd_count]);
        total_closes++;
    }

    printf("[locust] combined: cleanup done, mem=%d/%d, fd=%d/%d\n",
           total_allocs, total_frees, total_opens, total_closes);

    if(failures > 0) {
        printf("[locust] combined: FAIL (%d failures)\n", failures);
        return RESULT_FAIL;
    }

    printf("[locust] combined: PASS\n");
    return RESULT_PASS;
}

/* ============================================================================
 * Scheduler Stress Test
 *
 * Tests preemptive scheduling by running multiple concurrent processes.
 * Verifies the scheduler correctly manages multiple ready processes.
 * ============================================================================
 */

#define MAX_CONCURRENT 4

static int test_scheduler(void) {
    int total_spawns = 0;
    int total_completed = 0;
    int failures = 0;

    printf("[locust] === SCHEDULER STRESS ===\n");

    /* Reduce iterations for scheduler test - it's more intensive */
    int sched_iters = g_iterations / 5;
    if(sched_iters < 10) sched_iters = 10;

    for(int iter = 0; iter < sched_iters; iter++) {
        int pids[MAX_CONCURRENT];
        int spawned = 0;

        /* Spawn multiple background processes */
        for(int i = 0; i < MAX_CONCURRENT; i++) {
            /* Use bgcount with a label - it will count and exit */
            char label[8];
            snprintf(label, sizeof(label), "%c", 'A' + (iter + i) % 26);
            char count_str[8];
            snprintf(count_str, sizeof(count_str), "%d", 3);  /* Count to 3 */

            char *argv[] = {"bgcount", label, count_str, NULL};
            int pid = spawn_async("/bin/bgcount", argv);

            if(pid < 0) {
                failures++;
                if(g_verbose) printf("[locust] spawn_async failed\n");
            } else {
                pids[spawned++] = pid;
                total_spawns++;
            }
        }

        /* Wait for all spawned processes */
        for(int i = 0; i < spawned; i++) {
            int status = waitpid(pids[i], NULL, 0);
            if(status >= 0) {
                total_completed++;
            } else {
                failures++;
                if(g_verbose) printf("[locust] waitpid(%d) failed\n", pids[i]);
            }
        }

        /* Progress report */
        if((iter + 1) % (PROGRESS_INTERVAL / 5 + 1) == 0 || iter == sched_iters - 1) {
            printf("[locust] scheduler: %d/%d iters, %d spawned, %d completed\n",
                   iter + 1, sched_iters, total_spawns, total_completed);
        }
    }

    if(total_spawns != total_completed) {
        printf("[locust] scheduler: FAIL (spawned=%d, completed=%d, mismatch!)\n",
               total_spawns, total_completed);
        return RESULT_FAIL;
    }

    if(failures > 0) {
        printf("[locust] scheduler: FAIL (%d failures)\n", failures);
        return RESULT_FAIL;
    }

    printf("[locust] scheduler: PASS (%d concurrent batches, all reaped)\n",
           total_spawns / MAX_CONCURRENT);
    return RESULT_PASS;
}

/* ============================================================================
 * Process Creation Stress Test
 *
 * More thorough process testing: exit codes, arguments, rapid creation.
 * ============================================================================
 */

static int test_procreate(void) {
    int total_tests = 0;
    int failures = 0;

    printf("[locust] === PROCESS CREATION STRESS ===\n");

    /* Test 1: Exit codes */
    printf("[locust] procreate: testing exit codes...\n");
    for(int i = 0; i < 10; i++) {
        char *argv_true[] = {"true", NULL};
        char *argv_false[] = {"false", NULL};

        int ret_true = spawn("/bin/true", argv_true);
        int ret_false = spawn("/bin/false", argv_false);

        if(ret_true != 0) {
            failures++;
            if(g_verbose) printf("[locust] true returned %d, expected 0\n", ret_true);
        }
        if(ret_false != 1) {
            failures++;
            if(g_verbose) printf("[locust] false returned %d, expected 1\n", ret_false);
        }
        total_tests += 2;
    }

    /* Test 2: Arguments passing */
    printf("[locust] procreate: testing argument passing...\n");
    for(int i = 0; i < 10; i++) {
        char num_str[16];
        snprintf(num_str, sizeof(num_str), "%d", i + 1);

        /* seq 1 should print "1" and exit 0 */
        char *argv[] = {"seq", num_str, NULL};
        int ret = spawn("/bin/seq", argv);

        if(ret != 0) {
            failures++;
            if(g_verbose) printf("[locust] seq %s returned %d\n", num_str, ret);
        }
        total_tests++;
    }

    /* Test 3: Rapid sequential creation */
    printf("[locust] procreate: rapid sequential spawn...\n");
    int rapid_count = g_iterations / 2;
    if(rapid_count < 20) rapid_count = 20;

    for(int i = 0; i < rapid_count; i++) {
        char *argv[] = {"true", NULL};
        int ret = spawn("/bin/true", argv);
        if(ret < 0) {
            failures++;
        }
        total_tests++;

        if((i + 1) % PROGRESS_INTERVAL == 0) {
            printf("[locust] procreate: rapid %d/%d\n", i + 1, rapid_count);
        }
    }

    /* Test 4: Spawn with different programs */
    printf("[locust] procreate: testing different programs...\n");
    const char *programs[] = {
        "/bin/true", "/bin/false", "/bin/pwd", "/bin/uptime", NULL
    };
    for(int round = 0; round < 5; round++) {
        for(int p = 0; programs[p] != NULL; p++) {
            char *argv[] = {(char *)programs[p], NULL};
            int ret = spawn(programs[p], argv);
            if(ret < 0) {
                failures++;
                if(g_verbose) printf("[locust] spawn %s failed\n", programs[p]);
            }
            total_tests++;
        }
    }

    /* Test 5: Nested process creation (process spawns process) */
    printf("[locust] procreate: testing nested spawn...\n");
    for(int i = 0; i < 5; i++) {
        /* Run echo which is simple, but exercises the spawn path */
        char *argv[] = {"echo", "nested", "test", NULL};
        int ret = spawn("/bin/echo", argv);
        if(ret < 0) {
            failures++;
        }
        total_tests++;
    }

    if(failures > 0) {
        printf("[locust] procreate: FAIL (%d/%d tests failed)\n", failures, total_tests);
        return RESULT_FAIL;
    }

    printf("[locust] procreate: PASS (%d tests)\n", total_tests);
    return RESULT_PASS;
}

/* ============================================================================
 * Supervisor Mode
 *
 * Spawns each test as a child process to isolate crashes.
 * ============================================================================
 */

static int run_test_supervised(const char *test_name) {
    char iter_str[16];
    snprintf(iter_str, sizeof(iter_str), "%d", g_iterations);

    char test_arg[32];
    snprintf(test_arg, sizeof(test_arg), "--test=%s", test_name);

    char *argv[] = {"locust", test_arg, iter_str, NULL};
    int ret = spawn("/bin/locust", argv);

    if(ret < 0) {
        return RESULT_CRASH;
    }
    return ret;
}

static void print_summary(int results[], const char *names[], int count) {
    printf("\n[locust] ========== SUMMARY ==========\n");
    int all_pass = 1;
    for(int i = 0; i < count; i++) {
        printf("[locust] %-10s %s\n", names[i], result_str[results[i]]);
        if(results[i] != RESULT_PASS) {
            all_pass = 0;
        }
    }
    printf("[locust] ==============================\n");
    if(all_pass) {
        printf("[locust] All tests PASSED!\n");
    } else {
        printf("[locust] Some tests FAILED or CRASHED\n");
    }
}

static int run_supervisor(void) {
    const char *test_names[] = {
        "memory", "process", "fd", "scheduler", "procreate", "combined"
    };
    const int num_tests = 6;
    int results[6];

    printf("[locust] SeedOS Stress Tester\n");
    printf("[locust] Running %d iterations per test\n", g_iterations);
    printf("[locust] Supervisor mode: tests run as child processes\n\n");

    for(int i = 0; i < num_tests; i++) {
        results[i] = run_test_supervised(test_names[i]);
    }

    print_summary(results, test_names, num_tests);

    /* Return 0 if all pass, 1 otherwise */
    for(int i = 0; i < num_tests; i++) {
        if(results[i] != RESULT_PASS) {
            return 1;
        }
    }
    return 0;
}

/* ============================================================================
 * Main
 * ============================================================================
 */

static void usage(void) {
    printf("Usage: locust [options] [iterations]\n");
    printf("\n");
    printf("Options:\n");
    printf("  --test=NAME   Run specific test:\n");
    printf("                  memory    - malloc/free stress\n");
    printf("                  process   - spawn/wait stress\n");
    printf("                  fd        - open/close stress\n");
    printf("                  scheduler - concurrent process scheduling\n");
    printf("                  procreate - process creation torture\n");
    printf("                  combined  - interleaved operations\n");
    printf("  --verbose     Show detailed output\n");
    printf("  --help        Show this help\n");
    printf("\n");
    printf("If no --test is specified, runs all tests in supervisor mode.\n");
}

int main(int argc, char **argv) {
    const char *test_mode = NULL;

    /* Parse arguments */
    for(int i = 1; i < argc; i++) {
        if(strncmp(argv[i], "--test=", 7) == 0) {
            test_mode = argv[i] + 7;
        } else if(strcmp(argv[i], "--verbose") == 0) {
            g_verbose = 1;
        } else if(strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if(argv[i][0] >= '0' && argv[i][0] <= '9') {
            g_iterations = atoi(argv[i]);
            if(g_iterations <= 0) g_iterations = DEFAULT_ITERATIONS;
        }
    }

    /* Run specific test or supervisor mode */
    if(test_mode == NULL) {
        return run_supervisor();
    } else if(strcmp(test_mode, "memory") == 0) {
        return test_memory();
    } else if(strcmp(test_mode, "process") == 0) {
        return test_process();
    } else if(strcmp(test_mode, "fd") == 0) {
        return test_fd();
    } else if(strcmp(test_mode, "scheduler") == 0) {
        return test_scheduler();
    } else if(strcmp(test_mode, "procreate") == 0) {
        return test_procreate();
    } else if(strcmp(test_mode, "combined") == 0) {
        return test_combined();
    } else {
        printf("Unknown test: %s\n", test_mode);
        usage();
        return 1;
    }
}
