/*
 * clear.c - Clear the terminal screen
 *
 * Usage: clear
 *
 * Uses ANSI escape sequences:
 *   \033[2J - Clear entire screen
 *   \033[H  - Move cursor to home position (top-left)
 */

#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("\033[2J\033[H");
    return 0;
}
