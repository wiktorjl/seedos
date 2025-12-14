/*
 * yes.c - Repeatedly output a string
 *
 * Usage: yes [string]
 *        Outputs "y" if no string specified
 */

#include <stdio.h>

int main(int argc, char **argv) {
    const char *str = (argc > 1) ? argv[1] : "y";

    while(1) {
        puts(str);
    }

    return 0;
}
