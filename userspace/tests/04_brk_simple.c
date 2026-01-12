/*
 * 04_brk_simple.c - Simple brk test
 *
 * Just test brk return value with minimal code
 */

#include <syscall.h>

int main(void)
{
    /* Get current brk */
    long b = brk(0);

    /* Write a simple message first */
    const char msg[] = "brk returned: ";
    write(1, msg, sizeof(msg) - 1);

    /* Convert to hex and print */
    char hex[18];
    hex[0] = '0';
    hex[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int digit = (b >> (60 - i * 4)) & 0xF;
        hex[2 + i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
    }
    write(1, hex, 18);

    const char nl[] = "\n";
    write(1, nl, 1);

    /* Also print what we expect */
    const char msg2[] = "Expected ~0x403000\n";
    write(1, msg2, sizeof(msg2) - 1);

    return 0;
}
