/*
 * ctest.c - Concurrency test / Scheduler demo
 *
 * Usage: ctest [label1] [label2]
 *
 * Spawns two bgcount processes in the background to demonstrate
 * the preemptive scheduler with interleaved output.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *label1 = argc > 1 ? argv[1] : "A";
    const char *label2 = argc > 2 ? argv[2] : "B";

    printf("=== Scheduler Demo ===\n");
    printf("Spawning two background counters...\n\n");

    /* Build argv for bgcount */
    char *args1[] = {"bgcount", (char *)label1, "5", NULL};
    char *args2[] = {"bgcount", (char *)label2, "5", NULL};

    /* Spawn both in background */
    int pid1 = spawn_async("/bin/bgcount", args1);
    printf("Started bgcount %s (PID %d)\n", label1, pid1);

    int pid2 = spawn_async("/bin/bgcount", args2);
    printf("Started bgcount %s (PID %d)\n", label2, pid2);

    printf("\nWatch for interleaved output below:\n");
    printf("----------------------------------\n");

    /* Wait for both to finish */
    int status;
    waitpid(pid1, &status, 0);
    waitpid(pid2, &status, 0);

    printf("----------------------------------\n");
    printf("Both processes finished!\n");

    return 0;
}
