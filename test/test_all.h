/*
 * test_all.h - Test Registration Declarations
 *
 * Declares registration functions for all component tests.
 * Call these from kernel initialization to register all tests.
 */

#ifndef TEST_ALL_H
#define TEST_ALL_H

/* Test registration functions - one per component test file */
void test_pmm_register(void);
void test_vmm_register(void);
void test_gdt_register(void);
void test_idt_register(void);
void test_syscall_register(void);
void test_console_register(void);

/*
 * test_register_all - Register all tests with the framework.
 *
 * Call this after test_framework_init() to register all available tests.
 */
static inline void test_register_all(void) {
    test_pmm_register();
    test_vmm_register();
    test_gdt_register();
    test_idt_register();
    test_syscall_register();
    test_console_register();
}

#endif /* TEST_ALL_H */
