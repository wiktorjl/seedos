/*
 * ctest.c - Test C program for userspace
 *
 * This is a test program that demonstrates:
 * - C userspace programs work
 * - argc/argv are passed correctly
 * - Syscalls via libc
 *
 * Usage from shell:
 *   run ctest              -> argc=1, argv[0]="ctest"
 *   run ctest hello world  -> argc=3, shows all arguments
 */

#include <stdio.h>

int main(int argc, char **argv) {
    printf("=== C Program Test ===\n");

    /* Test 0: Print char directly */
    printf("Test 0: chars = ");
    putchar('A');
    putchar('B');
    putchar('C');
    printf("\n");

    /* Test 1: Print a simple number */
    printf("Test 1: number 42 = %d\n", 42);

    /* Test 1b: Print number 0 */
    printf("Test 1b: number 0 = %d\n", 0);

    /* Test 2: Print argc */
    printf("Test 2: argc = %d\n", argc);

    /* Test 3: Print argv pointer value */
    printf("Test 3: argv ptr = %p\n", (void *)argv);

    /* Test 4: Try to access argv[0] if argc > 0 */
    if (argc > 0) {
        printf("Test 4: accessing argv[0]...\n");
        printf("argv[0] = \"%s\"\n", argv[0]);
    }

    /* Test 5: Print all arguments if more than 1 */
    if (argc > 1) {
        printf("Test 5: all arguments:\n");
        for (int i = 0; i < argc; i++) {
            printf("  argv[%d] = \"%s\"\n", i, argv[i]);
        }
    }

    printf("======================\n");
    return argc;
}
