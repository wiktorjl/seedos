/*
 * filetest.c - Test file syscalls (open, read, lseek, close)
 *
 * Demonstrates the VFS by reading a file from the initrd.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
    char buf[64];
    int fd;
    int n;

    printf("=== File Syscall Test ===\n\n");

    /* Test 1: Open a file */
    printf("Opening bin/hello.txt... ");
    fd = open("bin/hello.txt", O_RDONLY);
    if (fd < 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK (fd=%d)\n", fd);

    /* Test 2: Read the file */
    printf("Reading... ");
    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("FAILED\n");
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    printf("OK (%d bytes): \"%s\"\n", n, buf);

    /* Test 3: Seek back to start */
    printf("Seeking to start... ");
    long pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0) {
        printf("FAILED\n");
        close(fd);
        return 1;
    }
    printf("OK (pos=%ld)\n", pos);

    /* Test 4: Read again */
    printf("Reading again... ");
    n = read(fd, buf, 3);
    if (n < 0) {
        printf("FAILED\n");
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    printf("OK (%d bytes): \"%s\"\n", n, buf);

    /* Test 5: Close */
    printf("Closing... ");
    if (close(fd) < 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK\n");

    printf("\n=== All tests passed! ===\n");
    return 0;
}
