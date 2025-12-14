/*
 * uniq.c - Report or filter out repeated lines
 *
 * Usage: uniq [-c] [FILE]
 *        -c  prefix lines with occurrence count
 */

#include <stdio.h>
#include <string.h>

#define MAX_LINE 1024

int main(int argc, char **argv) {
    int count_mode = 0;
    const char *filename = NULL;

    /* Parse options */
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-c") == 0) {
            count_mode = 1;
        }else {
            filename = argv[i];
        }
    }

    FILE *fp;
    if(filename) {
        fp = fopen(filename, "r");
        if(fp == NULL) {
            fprintf(stderr, "uniq: cannot open '%s'\n", filename);
            return 1;
        }
    }else {
        fp = stdin;
    }

    char prev_line[MAX_LINE] = "";
    char curr_line[MAX_LINE];
    int count = 0;

    while(fgets(curr_line, sizeof(curr_line), fp) != NULL) {
        if(strcmp(curr_line, prev_line) == 0) {
            count++;
        }else {
            if(count > 0) {
                if(count_mode) {
                    printf("%7d %s", count, prev_line);
                }else {
                    printf("%s", prev_line);
                }
            }
            strcpy(prev_line, curr_line);
            count = 1;
        }
    }

    /* Print last line */
    if(count > 0) {
        if(count_mode) {
            printf("%7d %s", count, prev_line);
        }else {
            printf("%s", prev_line);
        }
    }

    if(filename) {
        fclose(fp);
    }

    return 0;
}
