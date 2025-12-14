/*
 * wc.c - Word, line, and byte count
 *
 * Usage: wc [-lwc] [FILE ...]
 *        -l  lines
 *        -w  words
 *        -c  bytes
 *        Default: all three
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char **argv) {
    int show_lines = 0;
    int show_words = 0;
    int show_bytes = 0;
    int file_start = 1;

    /* Parse options */
    while(file_start < argc && argv[file_start][0] == '-') {
        char *opt = argv[file_start] + 1;
        while(*opt) {
            switch(*opt) {
                case 'l': show_lines = 1; break;
                case 'w': show_words = 1; break;
                case 'c': show_bytes = 1; break;
                default:
                    fprintf(stderr, "wc: invalid option '%c'\n", *opt);
                    return 1;
            }
            opt++;
        }
        file_start++;
    }

    /* Default: show all */
    if(!show_lines && !show_words && !show_bytes) {
        show_lines = show_words = show_bytes = 1;
    }

    if(argc <= file_start) {
        fprintf(stderr, "Usage: wc [-lwc] FILE [FILE ...]\n");
        return 1;
    }

    unsigned long total_lines = 0;
    unsigned long total_words = 0;
    unsigned long total_bytes = 0;
    int multiple_files = (argc - file_start) > 1;
    int ret = 0;

    for(int i = file_start; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if(fp == NULL) {
            fprintf(stderr, "wc: cannot open '%s'\n", argv[i]);
            ret = 1;
            continue;
        }

        unsigned long lines = 0;
        unsigned long words = 0;
        unsigned long bytes = 0;
        int in_word = 0;
        int c;

        while((c = fgetc(fp)) != EOF) {
            bytes++;
            if(c == '\n') {
                lines++;
            }
            if(isspace(c)) {
                in_word = 0;
            }else {
                if(!in_word) {
                    words++;
                    in_word = 1;
                }
            }
        }

        fclose(fp);

        /* Print results */
        if(show_lines) printf("%7lu ", lines);
        if(show_words) printf("%7lu ", words);
        if(show_bytes) printf("%7lu ", bytes);
        printf("%s\n", argv[i]);

        total_lines += lines;
        total_words += words;
        total_bytes += bytes;
    }

    /* Print totals if multiple files */
    if(multiple_files) {
        if(show_lines) printf("%7lu ", total_lines);
        if(show_words) printf("%7lu ", total_words);
        if(show_bytes) printf("%7lu ", total_bytes);
        printf("total\n");
    }

    return ret;
}
