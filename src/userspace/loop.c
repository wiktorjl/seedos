/*
 * loop.c - Infinite loop for testing
 *
 * Used for testing preemption and process termination.
 * This program never exits on its own.
 */

#include <stdio.h>

int main(int argc, char **argv) {
    printf("Looping forever.......\n");

    while (1) {
        /* Spin forever */
    }

    return 0;  /* Never reached */
}
