/*
 * crash.c - Trigger a page fault for testing
 *
 * Demonstrates that userspace exceptions are handled gracefully
 * by the kernel without crashing the system.
 */

#include <stdio.h>

int main(int argc, char **argv) {
    printf("About to crash...\n");

    /* Access invalid memory to trigger a page fault */
    volatile int *bad_ptr = (volatile int *)0xDEADBEEF;
    int x = *bad_ptr;  /* Page fault! */
    (void)x;

    /* Never reached */
    printf("This should not print\n");
    return 0;
}
