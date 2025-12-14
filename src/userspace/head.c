/*
 * head.c - Output the first part of files
 *
 * Usage: head [-n NUM] [FILE ...]
 *        Default: 10 lines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int num_lines = 10;
    int file_start = 1;

    /* Parse -n option */
    if(argc > 2 && strcmp(argv[1], "-n") == 0) {
        num_lines = atoi(argv[2]);
        file_start = 3;
    }

    if(argc <= file_start) {
        fprintf(stderr, "Usage: head [-n NUM] FILE [FILE ...]\n");
        return 1;
    }

    int ret = 0;
    int multiple_files = (argc - file_start) > 1;

    for(int i = file_start; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if(fp == NULL) {
            fprintf(stderr, "head: cannot open '%s'\n", argv[i]);
            ret = 1;
            continue;
        }

        if(multiple_files) {
            if(i > file_start) {
                putchar('\n');
            }
            printf("==> %s <==\n", argv[i]);
        }

        int lines = 0;
        int c;
        while((c = fgetc(fp)) != EOF && lines < num_lines) {
            putchar(c);
            if(c == '\n') {
                lines++;
            }
        }

        fclose(fp);
    }

    return ret;
}
