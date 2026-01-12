/*
 * 02_memory.c - Test memory management syscalls
 *
 * Tests: brk, mmap, munmap
 * Expected: "brk works!", "mmap works!" printed, then clean exit
 *
 * This validates that:
 * - brk() can extend the heap
 * - mmap() can allocate anonymous memory
 * - munmap() can free mapped memory
 */

#include <syscall.h>

/* Simple strlen */
static size_t my_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* Print a string */
static void print(const char *s)
{
    write(1, s, my_strlen(s));
}

/* Print a number in hex - inline to avoid register clobbering */
static void print_hex(unsigned long n)
{
    char hex[18];
    hex[0] = '0';
    hex[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int digit = (n >> (60 - i * 4)) & 0xF;
        hex[2 + i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
    }
    write(1, hex, 18);
}

int main(void)
{
    /* Test brk */
    print("Testing brk syscall...\n");

    /* Get current brk - use volatile to prevent register clobbering */
    volatile long current_brk = brk(0);
    print("  Initial brk: ");
    print_hex((unsigned long)current_brk);
    print("\n");

    /* Extend brk by 4KB */
    volatile long new_brk = brk((void *)(current_brk + 4096));
    print("  After +4KB:  ");
    print_hex((unsigned long)new_brk);
    print("\n");

    if (new_brk >= current_brk + 4096) {
        print("  brk works!\n");

        /* Try to write to the new memory */
        volatile char *ptr = (volatile char *)current_brk;
        *ptr = 'X';
        if (*ptr == 'X') {
            print("  Heap memory accessible!\n");
        }
    } else {
        print("  brk FAILED\n");
        return 1;
    }

    /* Test mmap */
    print("\nTesting mmap syscall...\n");

    volatile void *mapped = mmap(0, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    print("  mmap returned: ");
    print_hex((unsigned long)mapped);
    print("\n");

    if ((long)mapped > 0) {
        /* Try to use the mapped memory */
        volatile char *p = (volatile char *)mapped;
        p[0] = 'H';
        p[1] = 'i';
        p[2] = '\0';

        if (p[0] == 'H' && p[1] == 'i') {
            print("  mmap works!\n");
        }

        /* Test munmap */
        int ret = munmap((void *)mapped, 4096);
        print("  munmap returned: ");
        print_hex(ret);
        print("\n");
        if (ret == 0) {
            print("  munmap works!\n");
        }
    } else {
        print("  mmap FAILED\n");
        return 1;
    }

    print("\nAll memory tests passed!\n");
    return 0;
}
