/*
 * tail.c - Output the last part of files
 *
 * Usage: tail [-n NUM] [FILE ...]
 *        Default: 10 lines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LEN 1024
#define MAX_LINES 100

int main(int argc, char **argv) {
    int num_lines = 10;
    int file_start = 1;

    /* Parse -n option */
    if(argc > 2 && strcmp(argv[1], "-n") == 0) {
        num_lines = atoi(argv[2]);
        file_start = 3;
    }

    if(num_lines > MAX_LINES) {
        num_lines = MAX_LINES;
    }

    if(argc <= file_start) {
        fprintf(stderr, "Usage: tail [-n NUM] FILE [FILE ...]\n");
        return 1;
    }

    int ret = 0;
    int multiple_files = (argc - file_start) > 1;

    /* Circular buffer for lines */
    static char lines[MAX_LINES][MAX_LINE_LEN];

    for(int i = file_start; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if(fp == NULL) {
            fprintf(stderr, "tail: cannot open '%s'\n", argv[i]);
            ret = 1;
            continue;
        }

        if(multiple_files) {
            if(i > file_start) {
                putchar('\n');
            }
            printf("==> %s <==\n", argv[i]);
        }

        /* Read all lines into circular buffer */
        int head = 0;
        int count = 0;

        while(fgets(lines[head], MAX_LINE_LEN, fp) != NULL) {
            head = (head + 1) % num_lines;
            if(count < num_lines) {
                count++;
            }
        }

        fclose(fp);

        /* Print lines from circular buffer */
        int start = (count < num_lines) ? 0 : head;
        for(int j = 0; j < count; j++) {
            int idx = (start + j) % num_lines;
            printf("%s", lines[idx]);
        }
    }

    return ret;
}
