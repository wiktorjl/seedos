/*
 * test_framework.c - Kernel Test Framework Implementation
 *
 * Provides test registration, execution, and reporting.
 */

#include "test_framework.h"
#include "console.h"
#include "limine.h"

/* =============================================================================
 * Test Registry
 * =============================================================================
 */

static struct test_entry test_registry[MAX_TESTS];
static int test_count = 0;

/* Shared test context */
static struct limine_memmap_response *saved_memmap = NULL;
static uint64_t saved_hhdm_offset = 0;

/* =============================================================================
 * String Helpers (no libc available)
 * =============================================================================
 */

static int str_equal(const char *a, const char *b) {
    while(*a && *b) {
        if(*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int str_len(const char *s) {
    int len = 0;
    while(*s++) len++;
    return len;
}

/* =============================================================================
 * Test Framework Initialization
 * =============================================================================
 */

void test_framework_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    saved_memmap = memmap;
    saved_hhdm_offset = hhdm_offset;
    test_count = 0;
}

uint64_t test_get_hhdm_offset(void) {
    return saved_hhdm_offset;
}

struct limine_memmap_response *test_get_memmap(void) {
    return saved_memmap;
}

/* =============================================================================
 * Test Registration
 * =============================================================================
 */

int test_register(const char *component, const char *name,
                  const char *description, test_func_t func) {
    if(test_count >= MAX_TESTS) {
        return -1;
    }

    test_registry[test_count].component = component;
    test_registry[test_count].name = name;
    test_registry[test_count].description = description;
    test_registry[test_count].func = func;
    test_count++;

    return 0;
}

/* =============================================================================
 * Test Output Helpers
 * =============================================================================
 */

void test_print(const char *msg) {
    puts(msg);
}

void test_print_hex(uint64_t value) {
    put_hex(value);
}

void test_print_dec(uint64_t value) {
    put_dec(value);
}

/* Assertion failure helpers */
void test_print_failure(const char *file, int line, const char *cond) {
    puts("    ASSERT FAILED: ");
    puts(cond);
    puts("\n    at ");
    puts(file);
    puts(":");
    put_dec(line);
    puts("\n");
}

void test_print_eq_failure(const char *file, int line, const char *expr,
                           uint64_t actual, uint64_t expected) {
    puts("    ASSERT_EQ FAILED: ");
    puts(expr);
    puts("\n    expected: ");
    put_hex(expected);
    puts("\n    actual:   ");
    put_hex(actual);
    puts("\n    at ");
    puts(file);
    puts(":");
    put_dec(line);
    puts("\n");
}

void test_print_neq_failure(const char *file, int line, const char *expr,
                            uint64_t actual) {
    puts("    ASSERT_NEQ FAILED: ");
    puts(expr);
    puts(" == ");
    put_hex(actual);
    puts("\n    at ");
    puts(file);
    puts(":");
    put_dec(line);
    puts("\n");
}

/* =============================================================================
 * Test Execution
 * =============================================================================
 */

/* Print test result with formatting */
static void print_result(const char *component, const char *name, int result) {
    puts("  ");
    puts(component);
    puts(".");
    puts(name);

    /* Pad to align results */
    int len = str_len(component) + str_len(name) + 1;
    while(len < 30) {
        puts(" ");
        len++;
    }

    if(result == TEST_PASS) {
        puts(" [PASS]\n");
    }else if(result == TEST_SKIP) {
        puts(" [SKIP]\n");
    }else {
        puts(" [FAIL]\n");
    }
}

/* Run a single test entry and return result */
static int run_test_entry(struct test_entry *entry, int verbose) {
    if(verbose) {
        puts("  Running ");
        puts(entry->component);
        puts(".");
        puts(entry->name);
        puts("...\n");
    }

    int result = entry->func();
    print_result(entry->component, entry->name, result);
    return result;
}

void test_run_all(void) {
    if(test_count == 0) {
        puts("\nNo tests registered.\n\n");
        return;
    }

    puts("\n");
    puts("========================================\n");
    puts("  Running All Tests\n");
    puts("========================================\n\n");

    int passed = 0, failed = 0, skipped = 0;

    for(int i = 0; i < test_count; i++) {
        int result = run_test_entry(&test_registry[i], 0);
        if(result == TEST_PASS) passed++;
        else if(result == TEST_SKIP) skipped++;
        else failed++;
    }

    puts("\n========================================\n");
    puts("  Results: ");
    put_dec(passed);
    puts(" passed, ");
    put_dec(failed);
    puts(" failed, ");
    put_dec(skipped);
    puts(" skipped\n");
    puts("========================================\n\n");
}

void test_run_component(const char *component) {
    int found = 0;
    int passed = 0, failed = 0, skipped = 0;

    /* First pass: check if component exists */
    for(int i = 0; i < test_count; i++) {
        if(str_equal(test_registry[i].component, component)) {
            found++;
        }
    }

    if(found == 0) {
        puts("\nNo tests found for component: ");
        puts(component);
        puts("\n\n");
        test_list_all();
        return;
    }

    puts("\n");
    puts("========================================\n");
    puts("  Running Tests: ");
    puts(component);
    puts("\n========================================\n\n");

    /* Second pass: run matching tests */
    for(int i = 0; i < test_count; i++) {
        if(str_equal(test_registry[i].component, component)) {
            int result = run_test_entry(&test_registry[i], 0);
            if(result == TEST_PASS) passed++;
            else if(result == TEST_SKIP) skipped++;
            else failed++;
        }
    }

    puts("\n========================================\n");
    puts("  Results: ");
    put_dec(passed);
    puts(" passed, ");
    put_dec(failed);
    puts(" failed, ");
    put_dec(skipped);
    puts(" skipped\n");
    puts("========================================\n\n");
}

void test_run_single(const char *component, const char *name) {
    for(int i = 0; i < test_count; i++) {
        if(str_equal(test_registry[i].component, component) &&
            str_equal(test_registry[i].name, name)) {

            puts("\n");
            puts("========================================\n");
            puts("  Running Test: ");
            puts(component);
            puts(".");
            puts(name);
            puts("\n========================================\n\n");

            int result = run_test_entry(&test_registry[i], 1);

            puts("\n========================================\n");
            puts("  Result: ");
            if(result == TEST_PASS) puts("PASS");
            else if(result == TEST_SKIP) puts("SKIP");
            else puts("FAIL");
            puts("\n========================================\n\n");
            return;
        }
    }

    puts("\nTest not found: ");
    puts(component);
    puts(".");
    puts(name);
    puts("\n\n");
    test_list_all();
}

/* =============================================================================
 * Test Listing
 * =============================================================================
 */

void test_list_all(void) {
    if(test_count == 0) {
        puts("\nNo tests registered.\n\n");
        return;
    }

    puts("\nRegistered tests (");
    put_dec(test_count);
    puts(" total):\n\n");

    const char *current_component = NULL;

    for(int i = 0; i < test_count; i++) {
        /* Print component header when it changes */
        if(current_component == NULL ||
            !str_equal(current_component, test_registry[i].component)) {
            current_component = test_registry[i].component;
            puts("  [");
            puts(current_component);
            puts("]\n");
        }

        puts("    ");
        puts(test_registry[i].name);

        /* Pad for alignment */
        int len = str_len(test_registry[i].name);
        while(len < 20) {
            puts(" ");
            len++;
        }

        puts(" - ");
        puts(test_registry[i].description);
        puts("\n");
    }

    puts("\nUsage:\n");
    puts("  test all                - Run all tests\n");
    puts("  test <component>        - Run component tests (e.g., test pmm)\n");
    puts("  test <component>.<name> - Run specific test (e.g., test pmm.alloc_free)\n");
    puts("\n");
}

void test_list_component(const char *component) {
    int found = 0;

    puts("\nTests for [");
    puts(component);
    puts("]:\n\n");

    for(int i = 0; i < test_count; i++) {
        if(str_equal(test_registry[i].component, component)) {
            found++;
            puts("  ");
            puts(test_registry[i].name);

            int len = str_len(test_registry[i].name);
            while(len < 20) {
                puts(" ");
                len++;
            }

            puts(" - ");
            puts(test_registry[i].description);
            puts("\n");
        }
    }

    if(found == 0) {
        puts("  (no tests found)\n");
    }

    puts("\n");
}
