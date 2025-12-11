/*
 * test_console.c - Console Output Tests
 *
 * Tests for the unified console output system.
 */

#include "test_framework.h"
#include "console.h"
#include <stdint.h>

/* =============================================================================
 * Console Tests
 * =============================================================================
 */

/*
 * Test puts outputs without crashing.
 */
static int test_console_puts(void) {
    /* Should not crash */
    puts("    [console test output]\n");

    return TEST_PASS;
}

/*
 * Test putc outputs a character.
 */
static int test_console_putc(void) {
    /* Should not crash */
    putc('[');
    putc('c');
    putc(']');
    putc('\n');

    return TEST_PASS;
}

/*
 * Test put_hex outputs hex values correctly.
 */
static int test_console_put_hex(void) {
    /* Should output hex format */
    puts("    hex: ");
    put_hex(0xDEADBEEF);
    puts("\n");

    return TEST_PASS;
}

/*
 * Test put_dec outputs decimal values correctly.
 */
static int test_console_put_dec(void) {
    /* Should output decimal format */
    puts("    dec: ");
    put_dec(12345);
    puts("\n");

    return TEST_PASS;
}

/*
 * Test put_dec with zero.
 */
static int test_console_put_dec_zero(void) {
    puts("    zero: ");
    put_dec(0);
    puts("\n");

    return TEST_PASS;
}

/*
 * Test put_hex with zero.
 */
static int test_console_put_hex_zero(void) {
    puts("    hex zero: ");
    put_hex(0);
    puts("\n");

    return TEST_PASS;
}

/*
 * Test put_dec with large number.
 */
static int test_console_put_dec_large(void) {
    puts("    large: ");
    put_dec(0xFFFFFFFFFFFFFFFF);  /* Max uint64 */
    puts("\n");

    return TEST_PASS;
}

/* =============================================================================
 * Test Registration
 * =============================================================================
 */

void test_console_register(void) {
    test_register("console", "puts", "String output", test_console_puts);
    test_register("console", "putc", "Character output", test_console_putc);
    test_register("console", "put_hex", "Hexadecimal output", test_console_put_hex);
    test_register("console", "put_dec", "Decimal output", test_console_put_dec);
    test_register("console", "dec_zero", "Decimal zero", test_console_put_dec_zero);
    test_register("console", "hex_zero", "Hexadecimal zero", test_console_put_hex_zero);
    test_register("console", "dec_large", "Large decimal number", test_console_put_dec_large);
}
