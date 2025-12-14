/*
 * count.c - Count from 0 to 9
 *
 * Prints each digit 0-9 on a single line.
 */

#include <stdio.h>

int main(int argc, char **argv) {
    for (int i = 0; i < 10; i++) {
        putchar('0' + i);
    }
    putchar('\n');

    return 0;
}
