/*
 * stars.c - Print 20 asterisks
 *
 * Simple loop demonstration.
 */

#include <stdio.h>

int main(int argc, char **argv) {
    for (int i = 0; i < 20; i++) {
        putchar('*');
    }
    putchar('\n');

    return 0;
}
