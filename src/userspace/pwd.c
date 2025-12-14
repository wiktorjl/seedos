/*
 * pwd.c - Print working directory
 *
 * Usage: pwd
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char buf[256];

    (void)argc;
    (void)argv;

    if (getcwd(buf, sizeof(buf)) == NULL) {
        fprintf(stderr, "pwd: cannot get current directory\n");
        return 1;
    }

    puts(buf);
    return 0;
}
