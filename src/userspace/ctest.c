/*
 * ctest.c - Concurrency test / Scheduler demo
 *
 * Usage: ctest [label1] [label2]
 *
 * Demonstrates running two programs in sequence.
 * (True concurrency requires per-process kernel stacks which we don't have yet)
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *label1 = argc > 1 ? argv[1] : "A";
    const char *label2 = argc > 2 ? argv[2] : "B";

    printf("=== Scheduler Demo (Sequential) ===\n");
    printf("Running two bgcount processes...\n\n");

    /* Build argv for bgcount */
    char *args1[] = {"bgcount", (char *)label1, "3", NULL};
    char *args2[] = {"bgcount", (char *)label2, "3", NULL};

    /* Run first counter */
    printf("Starting bgcount %s...\n", label1);
    spawn("/bin/bgcount", args1);

    printf("\n");

    /* Run second counter */
    printf("Starting bgcount %s...\n", label2);
    spawn("/bin/bgcount", args2);

    printf("\nBoth processes finished!\n");

    return 0;
}
