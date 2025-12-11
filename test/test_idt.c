/*
 * test_idt.c - Interrupt Descriptor Table Tests
 *
 * Tests for IDT setup and interrupt handling.
 */

#include "test_framework.h"
#include "idt.h"
#include <stdint.h>

/* =============================================================================
 * IDT Tests
 * =============================================================================
 */

/*
 * Test IDTR contains valid values.
 */
static int test_idt_idtr(void) {
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr;

    __asm__ volatile ("sidt %0" : "=m"(idtr));

    /* IDTR should have non-zero base */
    TEST_ASSERT_NOT_NULL(idtr.base);

    /* Limit should be 256 entries * 16 bytes - 1 = 4095 */
    TEST_ASSERT_EQ(idtr.limit, 256 * 16 - 1);

    return TEST_PASS;
}

/*
 * Test interrupt flag state (should be enabled in normal operation).
 */
static int test_idt_interrupts_enabled(void) {
    uint64_t rflags;
    __asm__ volatile ("pushfq; pop %0" : "=r"(rflags));

    /* IF (bit 9) should be set */
    TEST_ASSERT((rflags & (1 << 9)) != 0);

    return TEST_PASS;
}

/*
 * Test IRQ base constant.
 */
static int test_idt_irq_base(void) {
    /* IRQ base should be 32 (after CPU exceptions 0-31) */
    TEST_ASSERT_EQ(IRQ_BASE, 32);

    return TEST_PASS;
}

/*
 * Test exception number constants.
 */
static int test_idt_exceptions(void) {
    TEST_ASSERT_EQ(EXCEPTION_DIVIDE_ERROR, 0);
    TEST_ASSERT_EQ(EXCEPTION_DEBUG, 1);
    TEST_ASSERT_EQ(EXCEPTION_BREAKPOINT, 3);
    TEST_ASSERT_EQ(EXCEPTION_INVALID_OPCODE, 6);
    TEST_ASSERT_EQ(EXCEPTION_DOUBLE_FAULT, 8);
    TEST_ASSERT_EQ(EXCEPTION_GENERAL_PROTECTION, 13);
    TEST_ASSERT_EQ(EXCEPTION_PAGE_FAULT, 14);

    return TEST_PASS;
}

/*
 * Test gate type constants.
 */
static int test_idt_gate_types(void) {
    /* Interrupt gate: P=1, DPL=0, Type=0xE */
    TEST_ASSERT_EQ(IDT_GATE_INTERRUPT, 0x8E);

    /* Trap gate: P=1, DPL=0, Type=0xF */
    TEST_ASSERT_EQ(IDT_GATE_TRAP, 0x8F);

    /* User gate: P=1, DPL=3, Type=0xE */
    TEST_ASSERT_EQ(IDT_GATE_USER, 0xEE);

    return TEST_PASS;
}

/*
 * Test that INT3 (breakpoint) works and returns.
 * This tests the IDT handler for exception 3.
 */
static int test_idt_int3(void) {
    /* INT3 should trigger breakpoint exception and return */
    __asm__ volatile ("int3");

    /* If we get here, the handler returned successfully */
    return TEST_PASS;
}

/* =============================================================================
 * Test Registration
 * =============================================================================
 */

void test_idt_register(void) {
    test_register("idt", "idtr", "IDTR validation", test_idt_idtr);
    test_register("idt", "interrupts", "Interrupts enabled", test_idt_interrupts_enabled);
    test_register("idt", "irq_base", "IRQ base constant", test_idt_irq_base);
    test_register("idt", "exceptions", "Exception number constants", test_idt_exceptions);
    test_register("idt", "gate_types", "Gate type constants", test_idt_gate_types);
    test_register("idt", "int3", "INT3 breakpoint handling", test_idt_int3);
}
