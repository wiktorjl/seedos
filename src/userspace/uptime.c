/*
 * uptime.c - Show system uptime
 *
 * Usage: uptime
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    unsigned long ms = uptime();
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;

    seconds %= 60;
    minutes %= 60;
    hours %= 24;

    printf("up ");
    if (days > 0) {
        printf("%lu day%s, ", days, days == 1 ? "" : "s");
    }
    if (hours > 0) {
        printf("%lu hour%s, ", hours, hours == 1 ? "" : "s");
    }
    if (minutes > 0) {
        printf("%lu minute%s, ", minutes, minutes == 1 ? "" : "s");
    }
    printf("%lu second%s\n", seconds, seconds == 1 ? "" : "s");

    return 0;
}
