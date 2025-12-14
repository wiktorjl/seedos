/*
 * alpha.c - Print the alphabet A-Z
 *
 * Prints all uppercase letters on a single line.
 */

#include <stdio.h>

int main(int argc, char **argv) {
    for (char c = 'A'; c <= 'Z'; c++) {
        putchar(c);
    }
    putchar('\n');

    return 0;
}
