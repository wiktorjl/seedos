/*
 * shutdown.c - Halt the system
 *
 * Usage: shutdown
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("Shutting down...\n");
    shutdown();

    /* Never reached */
    return 0;
}
