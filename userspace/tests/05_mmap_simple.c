/*
 * 05_mmap_simple.c - Simple mmap test
 */

#include <syscall.h>

int main(void)
{
    const char msg1[] = "mmap test: ";
    write(1, msg1, sizeof(msg1) - 1);

    void *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    /* Convert to hex and print */
    char hex[18];
    hex[0] = '0';
    hex[1] = 'x';
    unsigned long n = (unsigned long)p;
    for (int i = 0; i < 16; i++) {
        int digit = (n >> (60 - i * 4)) & 0xF;
        hex[2 + i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
    }
    write(1, hex, 18);

    const char nl[] = "\n";
    write(1, nl, 1);

    const char msg2[] = "Expected: ~0x100000000\n";
    write(1, msg2, sizeof(msg2) - 1);

    return 0;
}
