/*
 * test_gdt.c - Global Descriptor Table Tests
 *
 * Tests for GDT setup and segment selectors.
 */

#include "test_framework.h"
#include "gdt.h"
#include <stdint.h>

/* =============================================================================
 * GDT Tests
 * =============================================================================
 */

/*
 * Test that GDT selectors have correct values.
 */
static int test_gdt_selectors(void) {
    /* Verify expected selector values */
    TEST_ASSERT_EQ(GDT_KERNEL_CODE, 0x08);
    TEST_ASSERT_EQ(GDT_KERNEL_DATA, 0x10);
    TEST_ASSERT_EQ(GDT_USER_CODE, 0x18);
    TEST_ASSERT_EQ(GDT_USER_DATA, 0x20);
    TEST_ASSERT_EQ(GDT_TSS, 0x28);

    return TEST_PASS;
}

/*
 * Test that CS register contains kernel code selector.
 */
static int test_gdt_cs_register(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    /* CS should be kernel code selector */
    TEST_ASSERT_EQ(cs, GDT_KERNEL_CODE);

    return TEST_PASS;
}

/*
 * Test that DS register contains kernel data selector.
 */
static int test_gdt_ds_register(void) {
    uint16_t ds;
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));

    /* DS should be kernel data selector */
    TEST_ASSERT_EQ(ds, GDT_KERNEL_DATA);

    return TEST_PASS;
}

/*
 * Test that SS register contains kernel data selector.
 */
static int test_gdt_ss_register(void) {
    uint16_t ss;
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));

    /* SS should be kernel data selector */
    TEST_ASSERT_EQ(ss, GDT_KERNEL_DATA);

    return TEST_PASS;
}

/*
 * Test selector privilege levels (DPL).
 */
static int test_gdt_privilege_levels(void) {
    /* Kernel selectors should have RPL=0 */
    TEST_ASSERT_EQ(GDT_KERNEL_CODE & 0x3, 0);
    TEST_ASSERT_EQ(GDT_KERNEL_DATA & 0x3, 0);

    /* User selectors should have RPL=3 */
    TEST_ASSERT_EQ(GDT_USER_CODE & 0x3, 0x3);
    TEST_ASSERT_EQ(GDT_USER_DATA & 0x3, 0x3);

    return TEST_PASS;
}

/*
 * Test GDTR contains valid values.
 */
static int test_gdt_gdtr(void) {
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;

    __asm__ volatile ("sgdt %0" : "=m"(gdtr));

    /* GDTR should have non-zero base */
    TEST_ASSERT_NOT_NULL(gdtr.base);

    /* Limit should be at least big enough for 5 entries + TSS */
    /* Each entry is 8 bytes, TSS is 16 bytes */
    TEST_ASSERT(gdtr.limit >= (5 * 8 + 16 - 1));

    return TEST_PASS;
}

/* =============================================================================
 * Test Registration
 * =============================================================================
 */

void test_gdt_register(void) {
    test_register("gdt", "selectors", "Selector value verification", test_gdt_selectors);
    test_register("gdt", "cs_register", "CS register value", test_gdt_cs_register);
    test_register("gdt", "ds_register", "DS register value", test_gdt_ds_register);
    test_register("gdt", "ss_register", "SS register value", test_gdt_ss_register);
    test_register("gdt", "privilege", "Privilege level bits", test_gdt_privilege_levels);
    test_register("gdt", "gdtr", "GDTR validation", test_gdt_gdtr);
}
