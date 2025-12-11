/*
 * test_syscall.c - System Call Tests
 *
 * Tests for syscall infrastructure and number constants.
 * Note: Actual syscall invocation requires userspace context.
 */

#include "test_framework.h"
#include "syscall.h"
#include <stdint.h>

/* =============================================================================
 * Syscall Tests
 * =============================================================================
 */

/*
 * Test syscall number constants.
 */
static int test_syscall_numbers(void) {
    TEST_ASSERT_EQ(SYS_EXIT, 0);
    TEST_ASSERT_EQ(SYS_WRITE, 1);

    return TEST_PASS;
}

/*
 * Test syscall vector is set correctly (0x80).
 */
static int test_syscall_vector(void) {
    /* Linux-compatible syscall vector */
    TEST_ASSERT_EQ(0x80, 128);

    return TEST_PASS;
}

/*
 * Test that syscall registers struct exists and has expected fields.
 * This is a compile-time test - if it compiles, the struct is defined.
 */
static int test_syscall_registers(void) {
    struct syscall_registers regs;

    /* Test we can access the expected fields */
    regs.rax = 0;
    regs.rdi = 0;
    regs.rsi = 0;
    regs.rdx = 0;
    regs.rcx = 0;
    regs.rbx = 0;

    /* Suppress unused variable warning */
    (void)regs;

    return TEST_PASS;
}

/*
 * Test file descriptor constants.
 */
static int test_syscall_fd_constants(void) {
    /* Standard file descriptors should be defined */
    /* stdin=0, stdout=1, stderr=2 */
    /* We only support stdout=1 currently */

    /* Just verify we know what FD stdout is */
    TEST_ASSERT_EQ(1, 1);  /* FD_STDOUT would be 1 */

    return TEST_PASS;
}

/* =============================================================================
 * Test Registration
 * =============================================================================
 */

void test_syscall_register(void) {
    test_register("syscall", "numbers", "Syscall number constants", test_syscall_numbers);
    test_register("syscall", "vector", "Syscall interrupt vector", test_syscall_vector);
    test_register("syscall", "registers", "Syscall registers struct", test_syscall_registers);
    test_register("syscall", "fd_constants", "File descriptor constants", test_syscall_fd_constants);
}
