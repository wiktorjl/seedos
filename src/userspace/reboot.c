/*
 * reboot.c - Reboot the system
 *
 * Usage: reboot
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("Rebooting...\n");
    reboot();

    /* Never reached */
    return 0;
}
