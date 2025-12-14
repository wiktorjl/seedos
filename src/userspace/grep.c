/*
 * grep.c - Search for patterns in files
 *
 * Usage: grep [-i] [-v] [-c] [-n] PATTERN [FILE ...]
 *        -i  case insensitive
 *        -v  invert match (print non-matching lines)
 *        -c  count matches only
 *        -n  print line numbers
 *
 * Note: This is simple substring matching, not regex.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 1024

static int ignore_case = 0;
static int invert_match = 0;
static int count_only = 0;
static int show_line_numbers = 0;

/* Case-insensitive substring search */
static char *stristr(const char *haystack, const char *needle) {
    if(!*needle) return (char *)haystack;

    for(; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;

        while(*h && *n && (tolower((unsigned char)*h) == tolower((unsigned char)*n))) {
            h++;
            n++;
        }

        if(!*n) return (char *)haystack;
    }

    return NULL;
}

static int process_file(FILE *fp, const char *filename, const char *pattern, int show_filename) {
    char line[MAX_LINE];
    int line_num = 0;
    int match_count = 0;

    while(fgets(line, sizeof(line), fp) != NULL) {
        line_num++;

        /* Check for match */
        int matched;
        if(ignore_case) {
            matched = (stristr(line, pattern) != NULL);
        }else {
            matched = (strstr(line, pattern) != NULL);
        }

        if(invert_match) {
            matched = !matched;
        }

        if(matched) {
            match_count++;

            if(!count_only) {
                if(show_filename) {
                    printf("%s:", filename);
                }
                if(show_line_numbers) {
                    printf("%d:", line_num);
                }
                printf("%s", line);
            }
        }
    }

    if(count_only) {
        if(show_filename) {
            printf("%s:", filename);
        }
        printf("%d\n", match_count);
    }

    return match_count > 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    int file_start = 1;
    const char *pattern = NULL;

    /* Parse options */
    while(file_start < argc && argv[file_start][0] == '-') {
        char *opt = argv[file_start] + 1;
        while(*opt) {
            switch(*opt) {
                case 'i': ignore_case = 1; break;
                case 'v': invert_match = 1; break;
                case 'c': count_only = 1; break;
                case 'n': show_line_numbers = 1; break;
                default:
                    fprintf(stderr, "grep: invalid option '%c'\n", *opt);
                    return 2;
            }
            opt++;
        }
        file_start++;
    }

    /* Get pattern */
    if(file_start >= argc) {
        fprintf(stderr, "Usage: grep [-ivcn] PATTERN [FILE ...]\n");
        return 2;
    }
    pattern = argv[file_start++];

    if(file_start >= argc) {
        fprintf(stderr, "grep: no input files\n");
        return 2;
    }

    int ret = 1;  /* 1 = no matches found */
    int show_filename = (argc - file_start) > 1;

    for(int i = file_start; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if(fp == NULL) {
            fprintf(stderr, "grep: cannot open '%s'\n", argv[i]);
            continue;
        }

        if(process_file(fp, argv[i], pattern, show_filename) == 0) {
            ret = 0;
        }

        fclose(fp);
    }

    return ret;
}
