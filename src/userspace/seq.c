/*
 * seq.c - Print a sequence of numbers
 *
 * Usage: seq LAST
 *        seq FIRST LAST
 *        seq FIRST INCREMENT LAST
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    long first = 1;
    long increment = 1;
    long last;

    if (argc < 2) {
        fprintf(stderr, "Usage: seq [FIRST [INCREMENT]] LAST\n");
        return 1;
    }

    if (argc == 2) {
        /* seq LAST */
        last = atol(argv[1]);
    } else if (argc == 3) {
        /* seq FIRST LAST */
        first = atol(argv[1]);
        last = atol(argv[2]);
    } else {
        /* seq FIRST INCREMENT LAST */
        first = atol(argv[1]);
        increment = atol(argv[2]);
        last = atol(argv[3]);
    }

    if (increment == 0) {
        fprintf(stderr, "seq: increment cannot be zero\n");
        return 1;
    }

    if (increment > 0) {
        for (long i = first; i <= last; i += increment) {
            printf("%ld\n", i);
        }
    } else {
        for (long i = first; i >= last; i += increment) {
            printf("%ld\n", i);
        }
    }

    return 0;
}
