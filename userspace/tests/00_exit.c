/*
 * 00_exit.c - Simplest userspace test
 *
 * Tests: sys_exit
 * Expected: Process exits with code 42
 *
 * This is the first milestone for userspace - if this works,
 * the basic syscall mechanism is functional.
 */

#include <syscall.h>

int main(void)
{
    exit(42);
    return 0;  /* Never reached */
}
