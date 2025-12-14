/*
 * info.c - Display process info using syscalls
 *
 * Demonstrates getpid() and uptime() syscalls.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    printf("PID: %d\n", getpid());
    printf("Uptime: %lu ms\n", uptime());
    return 0;
}
