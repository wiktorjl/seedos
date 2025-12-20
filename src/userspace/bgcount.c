/*
 * bgcount.c - Background counter for scheduler demo
 *
 * Usage: bgcount <label> [count]
 *
 * Counts from 1 to count (default 10), printing "<label>: N" each iteration.
 * Uses a delay between prints to make interleaving visible when multiple
 * instances run concurrently.
 *
 * Example:
 *   bgcount A &
 *   bgcount B &
 *   # Output will show interleaved: "A: 1", "B: 1", "A: 2", "B: 2", ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Simple busy-wait delay using uptime */
static void delay(unsigned long ms) {
    unsigned long start = uptime();
    while(uptime() - start < ms) {
        /* Busy wait - scheduler will preempt us */
    }
}

int main(int argc, char **argv) {
    const char *label = "?";
    int count = 10;

    if(argc >= 2) {
        label = argv[1];
    }
    if(argc >= 3) {
        count = atoi(argv[2]);
        if(count <= 0) count = 10;
    }

    printf("[%s] started (PID %d), counting to %d\n", label, getpid(), count);

    for(int i = 1; i <= count; i++) {
        printf("%s: %d\n", label, i);
        delay(500);  /* 500ms between each print */
    }

    printf("[%s] done\n", label);
    return 0;
}
