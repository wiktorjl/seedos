/*
 * 03_sysinfo.c - Test system info syscalls
 *
 * Tests: uname, getpid, getppid
 * Expected: System info and PID printed
 *
 * This validates that:
 * - uname() returns system information
 * - getpid() returns the process ID
 * - getppid() returns the parent process ID
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

/* Print a number */
static void print_num(long n)
{
    char buf[20];
    char *p = buf + 19;
    int neg = 0;

    *p = '\0';

    if (n == 0) {
        print("0");
        return;
    }

    if (n < 0) {
        neg = 1;
        n = -n;
    }

    while (n > 0) {
        *--p = '0' + (n % 10);
        n /= 10;
    }

    if (neg) *--p = '-';

    print(p);
}

int main(void)
{
    /* Test uname */
    print("Testing uname syscall...\n");

    struct utsname info;
    int ret = uname(&info);

    if (ret == 0) {
        print("  sysname:  ");
        print(info.sysname);
        print("\n");

        print("  nodename: ");
        print(info.nodename);
        print("\n");

        print("  release:  ");
        print(info.release);
        print("\n");

        print("  version:  ");
        print(info.version);
        print("\n");

        print("  machine:  ");
        print(info.machine);
        print("\n");
    } else {
        print("  uname FAILED\n");
        return 1;
    }

    /* Test getpid */
    print("\nTesting getpid/getppid...\n");

    long pid = getpid();
    print("  PID:  ");
    print_num(pid);
    print("\n");

    long ppid = getppid();
    print("  PPID: ");
    print_num(ppid);
    print("\n");

    if (pid > 0) {
        print("\nAll sysinfo tests passed!\n");
    }

    return 0;
}
