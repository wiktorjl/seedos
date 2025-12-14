/*
 * echo.c - Print arguments to stdout
 *
 * Usage: echo [string ...]
 */

#include <stdio.h>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            putchar(' ');
        }
    }
    putchar('\n');
    return 0;
}
