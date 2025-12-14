/*
 * cat.c - Concatenate and display files
 *
 * Simple implementation of the cat command.
 * Reads files and outputs their contents to stdout.
 *
 * Usage:
 *   cat file1 [file2 ...]  - Display contents of files
 *   cat                     - Copy stdin to stdout (not implemented)
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: cat <file> [file2 ...]\n");
        return 1;
    }

    int exit_code = 0;

    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if (fp == NULL) {
            fprintf(stderr, "cat: %s: No such file or directory\n", argv[i]);
            exit_code = 1;
            continue;
        }

        /* Read and output file contents */
        int c;
        while ((c = fgetc(fp)) != EOF) {
            putchar(c);
        }

        fclose(fp);
    }

    return exit_code;
}
