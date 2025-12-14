/*
 * input.c - Interactive keyboard input test
 *
 * Reads characters from stdin and echoes them back.
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    printf("Type (q=quit): ");

    char buf[1];
    while (1) {
        /* Read one character from stdin */
        int n = read(0, buf, 1);
        if (n <= 0) {
            continue;  /* No input yet, keep polling */
        }

        char c = buf[0];

        /* Check for quit */
        if (c == 'q') {
            break;
        }

        /* Handle Enter key */
        if (c == '\n') {
            putchar('\n');
            continue;
        }

        /* Echo the character */
        putchar(c);
    }

    printf("\nBye!\n");
    return 0;
}
