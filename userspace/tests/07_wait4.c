/*
 * Test wait4() syscall
 *
 * Since fork() isn't implemented yet, this tests error cases:
 * - wait4(-1, ...) should return -ECHILD when no children exist
 * - wait4 with WNOHANG should return 0 immediately
 *
 * Full parent/child testing will be possible after fork() is implemented.
 */
#include <syscall.h>

#define ECHILD  10  /* No child processes */

/* Simple string output helper */
static void puts(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

/* Print a number */
static void print_num(long n)
{
    char buf[20];
    int i = 0;
    int neg = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    if (neg) {
        write(1, "-", 1);
    }

    /* Reverse */
    while (i > 0) {
        write(1, &buf[--i], 1);
    }
}

int main(void)
{
    int status = 0;
    long ret;

    puts("=== wait4() syscall test ===\n");

    /* Test 1: wait4(-1, ...) with no children should return -ECHILD */
    puts("Test 1: wait4(-1) with no children... ");
    ret = wait4(-1, &status, 0, (void *)0);

    if (ret == -ECHILD) {
        puts("OK (returned -ECHILD as expected)\n");
    } else {
        puts("FAIL (expected -ECHILD, got ");
        print_num(ret);
        puts(")\n");
        return 1;
    }

    /* Test 2: wait4 with WNOHANG should also return -ECHILD (no children) */
    puts("Test 2: wait4(-1, WNOHANG) with no children... ");
    ret = wait4(-1, &status, WNOHANG, (void *)0);

    if (ret == -ECHILD) {
        puts("OK (returned -ECHILD as expected)\n");
    } else {
        puts("FAIL (expected -ECHILD, got ");
        print_num(ret);
        puts(")\n");
        return 2;
    }

    /* Test 3: wait4 for specific non-existent PID */
    puts("Test 3: wait4(9999) for non-existent PID... ");
    ret = wait4(9999, &status, WNOHANG, (void *)0);

    if (ret == -ECHILD) {
        puts("OK (returned -ECHILD as expected)\n");
    } else {
        puts("FAIL (expected -ECHILD, got ");
        print_num(ret);
        puts(")\n");
        return 3;
    }

    /* Test 4: waitpid wrapper */
    puts("Test 4: waitpid(-1, 0) wrapper... ");
    ret = waitpid(-1, (int *)0, 0);

    if (ret == -ECHILD) {
        puts("OK (returned -ECHILD as expected)\n");
    } else {
        puts("FAIL (expected -ECHILD, got ");
        print_num(ret);
        puts(")\n");
        return 4;
    }

    puts("\nAll wait4 tests passed!\n");
    puts("(Full parent/child tests require fork() implementation)\n");

    return 0;
}
