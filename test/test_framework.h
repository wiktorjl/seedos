/*
 * test_framework.h - Kernel Test Framework
 *
 * A lightweight test framework for kernel unit tests.
 *
 * Features:
 *   - Test registration with component grouping
 *   - Run all tests, component tests, or specific tests
 *   - Pass/fail tracking with summary statistics
 *   - Assertion macros for common test patterns
 *
 * Usage from shell:
 *   test                     - Show available tests
 *   test all                 - Run all tests
 *   test <component>         - Run all tests for component (e.g., test pmm)
 *   test <component>.<name>  - Run specific test (e.g., test pmm.alloc_free)
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include <stddef.h>

/* =============================================================================
 * Test Result Types
 * =============================================================================
 */

/* Test function return values */
#define TEST_PASS  0
#define TEST_FAIL  1
#define TEST_SKIP  2

/* Test function signature */
typedef int (*test_func_t)(void);

/* =============================================================================
 * Test Registration
 *
 * Tests are registered at compile time using the TEST_REGISTER macro.
 * The framework collects all registered tests into a global registry.
 * =============================================================================
 */

/* Maximum limits */
#define MAX_TESTS           64
#define MAX_COMPONENT_LEN   16
#define MAX_TEST_NAME_LEN   32
#define MAX_TEST_DESC_LEN   64

/* Test entry structure */
struct test_entry {
    const char *component;    /* Component name (e.g., "pmm", "vmm") */
    const char *name;         /* Test name (e.g., "alloc_free") */
    const char *description;  /* Human-readable description */
    test_func_t func;         /* Test function pointer */
};

/* =============================================================================
 * Test Framework API
 * =============================================================================
 */

/*
 * test_framework_init - Initialize the test framework.
 *
 * @memmap: Pointer to Limine memory map (for memory tests).
 * @hhdm_offset: HHDM offset from bootloader.
 *
 * Must be called before running tests.
 */
struct limine_memmap_response;
void test_framework_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/*
 * test_register - Register a test with the framework.
 *
 * @component: Component name (e.g., "pmm").
 * @name: Test name (e.g., "alloc_free").
 * @description: Brief description of what the test verifies.
 * @func: Test function to call.
 *
 * Returns: 0 on success, -1 if registry is full.
 */
int test_register(const char *component, const char *name,
                  const char *description, test_func_t func);

/*
 * test_run_all - Run all registered tests.
 *
 * Runs every test in the registry and prints results.
 */
void test_run_all(void);

/*
 * test_run_component - Run all tests for a specific component.
 *
 * @component: Component name to filter by (e.g., "pmm").
 */
void test_run_component(const char *component);

/*
 * test_run_single - Run a specific test by component and name.
 *
 * @component: Component name (e.g., "pmm").
 * @name: Test name (e.g., "alloc_free").
 */
void test_run_single(const char *component, const char *name);

/*
 * test_list_all - List all registered tests without running them.
 */
void test_list_all(void);

/*
 * test_list_component - List tests for a specific component.
 *
 * @component: Component name to filter by.
 */
void test_list_component(const char *component);

/* =============================================================================
 * Test Context - Shared State for Tests
 * =============================================================================
 */

/*
 * Get the HHDM offset for memory tests.
 */
uint64_t test_get_hhdm_offset(void);

/*
 * Get the memory map for memory tests.
 */
struct limine_memmap_response *test_get_memmap(void);

/* =============================================================================
 * Assertion Macros
 *
 * Use these in test functions to check conditions.
 * On failure, they print diagnostic info and return TEST_FAIL.
 * =============================================================================
 */

/*
 * TEST_ASSERT - Assert that a condition is true.
 *
 * Usage: TEST_ASSERT(ptr != NULL);
 */
#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            test_print_failure(__FILE__, __LINE__, #cond); \
            return TEST_FAIL; \
        } \
    } while (0)

/*
 * TEST_ASSERT_EQ - Assert that two values are equal.
 *
 * Usage: TEST_ASSERT_EQ(result, expected);
 */
#define TEST_ASSERT_EQ(actual, expected) \
    do { \
        uint64_t _a = (uint64_t)(actual); \
        uint64_t _e = (uint64_t)(expected); \
        if (_a != _e) { \
            test_print_eq_failure(__FILE__, __LINE__, #actual, _a, _e); \
            return TEST_FAIL; \
        } \
    } while (0)

/*
 * TEST_ASSERT_NEQ - Assert that two values are not equal.
 *
 * Usage: TEST_ASSERT_NEQ(ptr, 0);
 */
#define TEST_ASSERT_NEQ(actual, not_expected) \
    do { \
        uint64_t _a = (uint64_t)(actual); \
        uint64_t _ne = (uint64_t)(not_expected); \
        if (_a == _ne) { \
            test_print_neq_failure(__FILE__, __LINE__, #actual, _a); \
            return TEST_FAIL; \
        } \
    } while (0)

/*
 * TEST_ASSERT_NULL - Assert that a pointer is NULL.
 */
#define TEST_ASSERT_NULL(ptr) TEST_ASSERT_EQ((ptr), 0)

/*
 * TEST_ASSERT_NOT_NULL - Assert that a pointer is not NULL.
 */
#define TEST_ASSERT_NOT_NULL(ptr) TEST_ASSERT_NEQ((ptr), 0)

/* Internal helper functions for assertion macros (don't call directly) */
void test_print_failure(const char *file, int line, const char *cond);
void test_print_eq_failure(const char *file, int line, const char *expr,
                           uint64_t actual, uint64_t expected);
void test_print_neq_failure(const char *file, int line, const char *expr,
                            uint64_t actual);

/* =============================================================================
 * Test Output Helpers
 * =============================================================================
 */

/*
 * test_print - Print a message during a test (for debugging/info).
 */
void test_print(const char *msg);

/*
 * test_print_hex - Print a hex value during a test.
 */
void test_print_hex(uint64_t value);

/*
 * test_print_dec - Print a decimal value during a test.
 */
void test_print_dec(uint64_t value);

#endif /* TEST_FRAMEWORK_H */
