/*
 * 01_write.c - Test write syscall
 *
 * Tests: sys_write, user-to-kernel memory copy
 * Expected: "Hello from userspace!" printed to console
 *
 * This validates that:
 * - write() syscall works
 * - User memory can be read by kernel
 * - Console output from Ring 3 works
 */

#include <syscall.h>

int main(void)
{
    const char msg[] = "Hello from userspace!\n";
    write(1, msg, sizeof(msg) - 1);
    return 0;
}
