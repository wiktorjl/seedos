/*
 * sort.c - Sort lines of text
 *
 * Usage: sort [-r] [-n] [FILE ...]
 *        -r  reverse order
 *        -n  numeric sort
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 1000
#define MAX_LINE_LEN 1024

static int reverse_mode = 0;
static int numeric_mode = 0;

/* Comparison function for qsort */
static int compare_lines(const void *a, const void *b) {
    const char *line_a = *(const char **)a;
    const char *line_b = *(const char **)b;
    int result;

    if (numeric_mode) {
        long num_a = atol(line_a);
        long num_b = atol(line_b);
        if (num_a < num_b) result = -1;
        else if (num_a > num_b) result = 1;
        else result = 0;
    } else {
        result = strcmp(line_a, line_b);
    }

    return reverse_mode ? -result : result;
}

int main(int argc, char **argv) {
    int file_start = 1;

    /* Parse options */
    while (file_start < argc && argv[file_start][0] == '-') {
        char *opt = argv[file_start] + 1;
        while (*opt) {
            switch (*opt) {
                case 'r': reverse_mode = 1; break;
                case 'n': numeric_mode = 1; break;
                default:
                    fprintf(stderr, "sort: invalid option '%c'\n", *opt);
                    return 1;
            }
            opt++;
        }
        file_start++;
    }

    /* Allocate line storage */
    char **lines = malloc(MAX_LINES * sizeof(char *));
    if (!lines) {
        fprintf(stderr, "sort: out of memory\n");
        return 1;
    }

    int num_lines = 0;

    /* Read lines from files or stdin */
    if (argc <= file_start) {
        /* Read from stdin */
        char buf[MAX_LINE_LEN];
        while (fgets(buf, sizeof(buf), stdin) != NULL && num_lines < MAX_LINES) {
            lines[num_lines] = strdup(buf);
            if (!lines[num_lines]) {
                fprintf(stderr, "sort: out of memory\n");
                break;
            }
            num_lines++;
        }
    } else {
        /* Read from files */
        for (int i = file_start; i < argc && num_lines < MAX_LINES; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (fp == NULL) {
                fprintf(stderr, "sort: cannot open '%s'\n", argv[i]);
                continue;
            }

            char buf[MAX_LINE_LEN];
            while (fgets(buf, sizeof(buf), fp) != NULL && num_lines < MAX_LINES) {
                lines[num_lines] = strdup(buf);
                if (!lines[num_lines]) {
                    fprintf(stderr, "sort: out of memory\n");
                    break;
                }
                num_lines++;
            }

            fclose(fp);
        }
    }

    /* Sort the lines */
    qsort(lines, num_lines, sizeof(char *), compare_lines);

    /* Print sorted lines */
    for (int i = 0; i < num_lines; i++) {
        printf("%s", lines[i]);
        free(lines[i]);
    }

    free(lines);
    return 0;
}
