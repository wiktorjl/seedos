/*
 * 06_mmap_full.c - Full mmap test with write and munmap
 */

#include <syscall.h>

static size_t my_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void print(const char *s)
{
    write(1, s, my_strlen(s));
}

int main(void)
{
    print("Testing mmap...\n");

    /* Allocate memory */
    void *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    /* Print address */
    print("  addr: ");
    char hex[18];
    hex[0] = '0';
    hex[1] = 'x';
    unsigned long n = (unsigned long)p;
    for (int i = 0; i < 16; i++) {
        int digit = (n >> (60 - i * 4)) & 0xF;
        hex[2 + i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
    }
    write(1, hex, 18);
    print("\n");

    /* Check if valid */
    if ((long)p <= 0) {
        print("  mmap failed!\n");
        return 1;
    }

    /* Write to memory */
    print("  Writing to mapped memory...\n");
    char *ptr = (char *)p;
    ptr[0] = 'O';
    ptr[1] = 'K';
    ptr[2] = '\n';
    ptr[3] = '\0';

    /* Read back */
    print("  Read back: ");
    write(1, ptr, 3);

    /* Unmap */
    print("  Unmapping...\n");
    int ret = munmap(p, 4096);
    if (ret == 0) {
        print("  munmap OK\n");
    } else {
        print("  munmap failed\n");
    }

    print("All mmap tests passed!\n");
    return 0;
}
